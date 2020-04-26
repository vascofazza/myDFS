//
//  test_server.c
//
//
//  Created by Federico Scozzafava on 15/02/15.
//
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include "read_utilities.h"
#include "sem_utilities.h"
#include "mydfs_commons.h"
#include "configuration_manager.h"

#define BACKLOG 10     // how many pending connections queue will hold

#define SHARED_PATH_LEN 256

typedef struct client_query
{
    int command;
    char path[SHARED_PATH_LEN];
    int mode;
    unsigned long block_no;
    unsigned long block_size;
} client_query;

typedef struct shared_struct
{
    int wr; //1 se in scrittura, 0 sola lettura
    SOCKET sock_fd; // server0 (connessione di controllo). Usato per il heart-beating thread
    DWORD tid; //id thread processo
    char path[SHARED_PATH_LEN];
    HANDLE semaphore; //semaforo assegnato al processo/thread, heart-beating
    read_struct r_struct;
    ssize_t size; // size del file in locale
    int locked; // 1 se un lock esclusivo e' stato richeisto
} shared_struct;

typedef struct mydfs //client-specific struct / child process-thread side
{
    SOCKET command_sock;
    SOCKET data_sock;
    int mode;
    int file;
    ssize_t size;
    unsigned long n_bloks;
    unsigned long block_size;
    HANDLE semaphore; //on mac you need to reopen this, not returining the same fd
    unsigned int process_id; //unique id 0-PARALLELISM
    read_struct r_struct;
} mydfs;

static char* command = NULL;

static serverconfig configuration;

static SOCKET sockfd, new_fd;  // listen on sock_fd, new connection on new_fd

static int active_threads = 0;

static int PARALLELISM = 0;

static void *shr_mem = NULL; //puntatore alla memoria condivisa

static HANDLE shr_mem_H = NULL; //handle memoria condivisa

static HANDLE main_semaphore = NULL; //id del semaforo creato dal server0

static HANDLE shared_semaphore = NULL; //id del semaforo per l'accesso alla struttura condivisa

static HANDLE backlog_semaphore = NULL;

static HANDLE logger = NULL;

DWORD sig_thread(LPVOID param);

int get_free_id();

int main_loop(unsigned int process_id);

int parse_command(mydfs *id, char *string, client_query *res);

int execute_command(mydfs *id, client_query *command);

SOCKET open_data_sock(mydfs *id);

ssize_t send_response(mydfs *id, int op_code, char *msg);

ssize_t send_response_sock(SOCKET fd, int op_code, char *msg);

ssize_t send_err_response(mydfs *id, int err_code, char *msg);

ssize_t send_open_message(mydfs *id, int port);

ssize_t send_block(mydfs *id, unsigned long block_no);

ssize_t receive_block(mydfs *id, unsigned long block_no, ssize_t size);

int open_local_file(mydfs *id, char *path, int mode);

int check_command(char *vptr, int len, char const *command);

shared_struct *get_shared_struct(unsigned int i);

int *get_process_table(unsigned int i);

int init_child_process(unsigned int *process_id);

int cleanup()
{
    WSACleanup();
    int res = 0;
    if(main_semaphore != NULL) destroy_sem(&main_semaphore);
    if(shared_semaphore != NULL)destroy_sem(&shared_semaphore);
    if(backlog_semaphore != NULL)destroy_sem(&backlog_semaphore);
    if (shr_mem != NULL) res += !UnmapViewOfFile(shr_mem); //TRUE on completition
    if (shr_mem_H != NULL) close_handle(&shr_mem_H);
    //if (logger != NULL) close_handle(&logger);//se fallisce ormai...?
    return res;
}

DWORD sig_thread(LPVOID param)
{
    mydfs *id = (mydfs*)param;
    HANDLE event = getEvent(id->process_id);
    semsignal(id->semaphore); //thread inizializzato, sblocco la barriera
    while(TRUE)
    {
        switch(WaitForSingleObject(event, INFINITE))
        {
            case WAIT_ABANDONED:
                break;
            case WAIT_OBJECT_0:
            {
                ResetEvent(event);
                semwait(id->semaphore);
                if(id->mode == -1)
                {
                    close_handle(&event);
                    ExitThread(0);
                }
                id->size = getFileSize(id->file);
                syslog(logger, EVENTLOG_INFORMATION_TYPE, "invalidation -> id: %d, tid: %d, size: %d", id->process_id, GetCurrentThreadId(), id->size);
                char msg[100] = "CACHE_INVALIDATION";
                char size[50];
                sprintf(size, "%d", id->size);
                strcat(msg, DELIMITER);
                strcat(msg, size);
                if (send_response_sock(id->command_sock, DFS_INVD_CODE, msg) <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send invalidation message: %s", GetErrorMessage(WSAGetLastError()));
                }
                semsignal(id->semaphore);
                break;
            }
            case WAIT_TIMEOUT:
            {
                continue;
                break;
            }
            default:
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "signal thread - wait failed: %s", GetErrorMessage(GetLastError()));
                exit(EXIT_FAILURE);
            }
        }
    }
    return 0;
}

void sig_handler(int s) //segnale inviato dai processi figli al momento della terminazione, handler necessario per fare una wait e non lasciare processi zombie
{
    syslog(logger, EVENTLOG_INFORMATION_TYPE, "Received signal %d", s);
    if (s == SIGINT)
    {
        exit(cleanup());
    }
}

uint16_t get_sock_port(SOCKET sock_fd) //usata per indicare al client su che porta connettersi
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa); /*
                                 The address_len parameter should be initialized to indicate the amount of
                                 space pointed to by address.  On return it contains the actual size of
                                 the address returned (in bytes).
                                 */
    int res = getsockname(sock_fd, (struct sockaddr *)&sa, &len);
    if (res < 0){ return -1; }
    return ntohs(sa.sin_port);
}

int main(int argc, char **argv)
{
    //init logger
    command = argv[0];
    logger = RegisterEventSource(NULL/*uses local computer*/, TEXT("MyDFS Server"));
    if(logger == NULL)
    {
        perror("logger init failed");
        exit(EXIT_FAILURE);
    }

    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "WSAStartup failed: %d\n", iResult);
        return 1;
    }

    atexit(&cleanup);

    load_server_config(&configuration);

    PARALLELISM = configuration.n_processes;

    if (argc == 3 && strcmp("--child", argv[1]) == 0)
    {
        int process_id = (int)strtol(argv[2], (char **)NULL, 10);
        if(process_id == 0 && (errno == EINVAL || errno == ERANGE))
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
            return -1;
        }
        syslog(logger, EVENTLOG_INFORMATION_TYPE, "Child #%d starting up...", process_id);
        shr_mem_H = OpenFileMapping(FILE_MAP_ALL_ACCESS, TRUE, "MyDFSshm");
        if (shr_mem_H == NULL)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "error creating file mapping (shr_mem): %s", GetErrorMessage(GetLastError()));
            exit(EXIT_FAILURE);
        }
        shr_mem = MapViewOfFile(shr_mem_H, FILE_MAP_ALL_ACCESS, 0,0,0);
        if(shr_mem == NULL)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "MapViewOfFile error: %s", GetErrorMessage(GetLastError()));
            exit(EXIT_FAILURE);
        }
        main_semaphore = open_sem(-1);
        shared_semaphore = open_sem(-2);
        backlog_semaphore = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, "backlog_semaphore");// sem_open("backlog_semaphore", O_CREAT | O_EXCL, 0644,PARALLELISM);// init_sem(-1);//(main_key, PARALLELISM+1);
        if (backlog_semaphore == NULL)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "backlog semaphore init: %s", GetErrorMessage(GetLastError()));
            exit(EXIT_FAILURE);
        }

        init_child_process(&process_id);
        return 0;
    }

    if (argc == 3 && strcmp("-p", argv[1]) == 0)
    {
        unsigned int converted = (int)strtoul(argv[2], (char **)NULL, 10);
        if(converted == 0 && (errno == EINVAL || errno == ERANGE))
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
            return -1;
        }
        configuration.port = converted;
    }

    syslog(logger, EVENTLOG_INFORMATION_TYPE, "starting with port %u", configuration.port);

    struct sockaddr_in server;
    struct sockaddr_storage their_addr; // connector's address information
    BOOL reuse=TRUE;
    char s[INET6_ADDRSTRLEN]; //inidizzo ip testuale

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "server socket: %s", GetErrorMessage(WSAGetLastError()));
        exit(EXIT_FAILURE);
    }//crea un Socket in ascolto e memorizza il file descriptor in listen_fd con protocollo TCP

    //abilito riuso indirizzo
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(BOOL)) == SOCKET_ERROR) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "setsockopt reuse address: %s", GetErrorMessage(WSAGetLastError()));
        exit(EXIT_FAILURE);
    }

    memset(&server, 0, sizeof(server)); //pulisco la struttura
    server.sin_family = AF_INET;   //tipo di socket internet
    server.sin_addr.s_addr = htonl(INADDR_ANY);  //accetta ogni ip in ingresso
    server.sin_port = htons(configuration.port);  //imposta la porta


    if(bind(sockfd, (struct sockaddr *) &server,
            sizeof(server)) == SOCKET_ERROR) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "server socket bind: %s", GetErrorMessage(WSAGetLastError()));
        close_socket(&sockfd);
    }  //lego il file descriptor ai parametri impostati a servaddr

    if (listen(sockfd, BACKLOG) == SOCKET_ERROR) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "listen: %s", GetErrorMessage(WSAGetLastError()));
        exit(EXIT_FAILURE);
    }

    //installo l'handler
    signal(SIGINT, sig_handler);

    //creo memoria condivisa -> memory mapping

    shr_mem_H = CreateFileMapping(INVALID_HANDLE_VALUE, //use paging file
            NULL, //no security attributes
            PAGE_READWRITE, //Access
            0, // hi-size
            (PARALLELISM + 1) * sizeof(shared_struct) + (PARALLELISM +1) * sizeof(int), //low-size
            "MyDFSshm"
    );
    if (shr_mem_H == NULL)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "error creating file mapping (shr_mem): %s", GetErrorMessage(GetLastError()));
        exit(EXIT_FAILURE);
    }

    shr_mem = MapViewOfFile(shr_mem_H, FILE_MAP_ALL_ACCESS, 0,0,0);
    if(shr_mem == NULL)
    {
        close_handle(shr_mem_H);
        syslog(logger, EVENTLOG_ERROR_TYPE, "MapViewOfFile error: %s", GetErrorMessage(GetLastError()));
        exit(EXIT_FAILURE);
    }

    //Azzero memoria
    memset(shr_mem, 0, (PARALLELISM + 1) * sizeof(shared_struct) + (PARALLELISM +1) * sizeof(int));

    //creo semafori, uno globale (#threads+1) per sharer memory, uno per ogni semaforo (#thread) per socket ERRORI GESTITI DALLA LIBRERIA sem_utilites
    backlog_semaphore = CreateSemaphore(NULL, PARALLELISM, PARALLELISM, "backlog_semaphore");// sem_open("backlog_semaphore", O_CREAT | O_EXCL, 0644,PARALLELISM);// init_sem(-1);//(main_key, PARALLELISM+1);
    if (backlog_semaphore == NULL)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "backlog semaphore init: %s", GetErrorMessage(GetLastError()));
        exit(EXIT_FAILURE);
    }

//    unlink_sem(-1); //se muore in maniera anomala..
    main_semaphore = init_sem(-1);

//    unlink_sem(-2);
    shared_semaphore = init_sem(-2);

    syslog(logger, EVENTLOG_INFORMATION_TYPE, "server: waiting for connections...");

    while(1) {

        semwait(backlog_semaphore);
        semwait(main_semaphore);
        socklen_t sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == INVALID_SOCKET) {
            perror("accept");
            if(errno == EINTR)
            {
                semsignal(backlog_semaphore);
                semsignal(main_semaphore);
                continue;
            }
            else
                exit(EXIT_FAILURE);
        }


        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        syslog(logger, EVENTLOG_INFORMATION_TYPE, "server: got connection from %s", s);

        int process_id = 0; //id del processo attuale
        semwait(shared_semaphore);

        //prendo il primo id libero
        if((process_id = get_free_id()) < 0)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "no free ids"); //NON DOVREBBE ACCADERE
            close_socket(&new_fd);
            semsignal(backlog_semaphore);
            semsignal(main_semaphore);
            semsignal(shared_semaphore);
            continue;
        }

        semsignal(shared_semaphore);

        //non libero la sezione critica cosi' se anche il processo figlio termina prima della signal ho il tempo di impostare i gen...
        //update 2... questo lo sblocco nella init del processo / thread cosi' non ho sovrapposizione di ID
        DWORD child_pid = GetCurrentThreadId();//-1;
        HANDLE thread = NULL;
        if(configuration.threaded)
        {
            thread = CreateThread(NULL, 0, &init_child_process, &process_id, CREATE_SUSPENDED, NULL); //creo il thread sospeso... Grazie windows!
            if (thread == NULL)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "thread init: %s", GetErrorMessage(GetLastError()));
                exit(EXIT_FAILURE);
            }
        }
        else{
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            char argbuf[256];
            memset(&si,0,sizeof(si));
            wsprintf(argbuf,"%s --child %d", command, process_id);
            if (!CreateProcess(NULL,argbuf,NULL,NULL,
                               TRUE, // inherit handles
                               CREATE_SUSPENDED,NULL,NULL,&si,&pi) ){
                syslog(logger, EVENTLOG_ERROR_TYPE,"createprocess failed %d", GetErrorMessage(GetLastError()));
                return -1;
            }

            close_handle(&pi.hProcess);
            thread = pi.hThread;
            //DI DEFAULT IL SOCKET E' EREDITABILE...! <3 windows
            SOCKET s = new_fd;
            close_socket(&s);
        }
        semwait(shared_semaphore);

        syslog(logger, EVENTLOG_INFORMATION_TYPE, "Init current id: %d", process_id);
        active_threads++;
        shared_struct *curr_struct = get_shared_struct(process_id);
        memset(curr_struct, 0, sizeof(shared_struct));
        curr_struct->tid = child_pid;
        curr_struct->sock_fd = new_fd; //quello duplicato eventualmente
        *get_process_table(process_id) = 1;
        semsignal(shared_semaphore);
        ResumeThread(thread);
        close_handle(&thread);
        //ora il thread puo' partire
    }
    return 0;
}

//controllo se il comando ricevuto corrisponde a command. Usato nel beat
int check_command(char *vptr, int len, char const *command)
{
    if (len < 6)
        return -1;
    vptr[5]=0;
    if (strcmp(command, vptr) == 0) return 0;
    return -1;
}

int init_child_process(unsigned int *process_id)
{
    unsigned int id = *process_id;

    int res = main_loop(id);

    semwait(shared_semaphore);
    active_threads--;
    *get_process_table(id) = 0;
    shared_struct *curr_struct = get_shared_struct(id); //invalidate prevention
    semsignal(shared_semaphore);
    semsignal(backlog_semaphore); //se e' un processo invece lo aumento nell'handler... NO non faccio la wait su windows
    return res;
}

int main_loop(unsigned int process)
{
    char buf[512];
    memset(&buf, 0, 512);
    mydfs id;
    memset(&id, 0, sizeof(mydfs));
    id.data_sock = INVALID_SOCKET;
    id.file = -1;
    id.mode = -1;
    id.process_id = process;

    //se esco allora tutta la struttura e' pronta, chiudo il semaforo e continuo
    semsignal(main_semaphore); //sblocco il thread principale, ho copiato l'id e tutto

    semwait(shared_semaphore);
    shared_struct *curr_struct = get_shared_struct(process);
    id.semaphore = init_sem(process);//curr_struct->semaphore;
    id.command_sock = curr_struct->sock_fd;// curr_struct->pid < 0 ? curr_struct->sock_fd : new_fd;
    curr_struct->r_struct = id.r_struct;
    semsignal(shared_semaphore);

    //Devo aspettare che il thread apra l'evento
    //barriera
    semwait(id.semaphore);
    HANDLE thread = CreateThread(NULL, 0, &sig_thread, &id, 0, NULL);
    if(thread == NULL)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "create sig_thread: %s", GetErrorMessage(GetLastError()));
        exit(EXIT_FAILURE);
    }
    int ret = 0;

    client_query res;
    memset(&res, 0, sizeof(client_query));

    while(1)
    {
        //ZONA CRITICA -- LETTURA/SCRITTURA SOCKET CONTROLLO
        //questo lo sblocco nel sig_thread
        ssize_t numbytes = readline_nb(id.command_sock, buf, sizeof(buf), &id.r_struct);
        semwait(id.semaphore);
        if (numbytes == -1)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "recv loop: %s", GetErrorMessage(WSAGetLastError()));
            ret = -1;
            break;
        }
        else if (numbytes == 0) //il client ha chiuso la connessione esplicitamente / TIMEOUT
        {
            ret = 0;
            break;
        }
        else if(numbytes == -3) //TIMEOUT
        {
            if((id.mode & (MDFSO_RDWR | MDFSO_WRONLY | MDFSO_EXLOCK)) != 0) {
                if (send_response_sock(curr_struct->sock_fd, DFS_HTBT_CODE, "BEAT") < 0) {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send beat: %s", GetErrorMessage(WSAGetLastError()));
                    ret = -1;
                    break;
                }
                char buf[10];
                long beat = readline_nb(curr_struct->sock_fd, buf, 10, &curr_struct->r_struct);
                if (beat < 0 || check_command(buf, 10, DFS_HTBT_COMMAND) < 0) {
                    //killo tutto
                    syslog(logger, EVENTLOG_INFORMATION_TYPE, "Client #%d killed", id.process_id);
                    ret = 0;
                    break;
                }
                syslog(logger, EVENTLOG_INFORMATION_TYPE, "client %d beated", id.process_id);
            }
            semsignal(id.semaphore);
            continue; // rilascio il semaforo
        }
        if(parse_command(&id, buf, &res) != 1)
        {
            if(send_response(&id, DFS_ERRN_CODE, "PARSING_ERROR")<0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "send error: %s", GetErrorMessage(WSAGetLastError()));
                ret = -1;
                break;
            }
            syslog(logger, EVENTLOG_ERROR_TYPE, "parse_command error");

            ret = -1;
            break;
        }
        int exec = execute_command(&id, &res);

        if (exec == 1) //closed
        {
            ret = process;
            break;
        }
        else if(exec < 0)
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "execute_command error");
            ret = -1;
            break;
        }

        if(!ReleaseSemaphore(id.semaphore, 1, NULL))
        {
            syslog(logger, EVENTLOG_ERROR_TYPE, "sem_post thread loop: %s", GetErrorMessage(GetLastError()));
            ret = -1;
            break;
        }
        memset(&buf, 0, sizeof(buf));
    }
//    semwait(id.semaphore);
    id.mode = -1;
    kill(id.process_id);
    semsignal(id.semaphore);
    //TerminateThread(thread, 0);
    if (WaitForSingleObject(thread, INFINITE) == WAIT_TIMEOUT)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "error terminating thread: %s", GetErrorMessage(GetLastError()));
        exit(EXIT_FAILURE);
    }
    close_handle(&thread);
    if(ret == process)
    {
        semsignal(id.semaphore);
        destroy_sem(&id.semaphore);
        return ret;
    }
    close_handled(&id.file);
    close_socket(&id.data_sock);
    close_socket(&id.command_sock);
    semsignal(id.semaphore);
    destroy_sem(&id.semaphore);
    return ret;
}

int open_local_file(mydfs *id, char *path, int mode)
{
    //check apertura in scrittura da un altro processo
    unsigned long len = strlen(path) + strlen(configuration.path) + 1;
    char n_path[len];
    memset(n_path, 0, len);
    strcat(n_path, configuration.path);
    strcat(n_path, path);
    semwait(shared_semaphore);
    unsigned int i;
    for (i = 0; i < PARALLELISM; i++)
    {
        if(*get_process_table(i) == 0) continue;
        shared_struct *curr_struct = get_shared_struct(i);
        int cmp = strcmp(path, curr_struct->path);
        if((curr_struct->wr || curr_struct->locked) && !cmp)
        {
            semsignal(shared_semaphore);
            syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d - file %s opened by child #%d", id->process_id, path, i);
            return -1; //-1 in caso il file è aperto in scrittura da un altro client;
        }
        else if (curr_struct->wr == 0 && ((id->mode&(MDFSO_RDWR|MDFSO_WRONLY)) != 0) && cmp == 0)
        {
            kill(i); //trigger
        }
    }
    int lock = ((mode & MDFSO_EXLOCK) == MDFSO_EXLOCK);
    if(lock)
    {
        shared_struct *this = get_shared_struct(id->process_id);
        this->locked = 1;
        mode &= ~MDFSO_EXLOCK; //tolgo il mio lock
        mode |= MDFSO_RDWR; //il lock in scrittura non puo' essere acquisito altrimenti
    }
    if((mode & MDFSO_WRONLY) != 0)
        mode |= MDFSO_RDWR; //perche' in scrittura devo comunque leggere
    mode &= (~MDFSO_WRONLY);

    mode = dfs2sys_mode(mode);

    int fd = open(n_path, mode | _O_BINARY, 0644); //apre un file, eventualmente lo crea -rw-r--r--
    //non e' necessario settare la umask (default 022 ottale)
    if (fd < 0) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "file open: %s", GetErrorMessage(GetLastError()));
        semsignal(shared_semaphore);
        return -1;
    }
    id->file = fd;
    id->size= getFileSize(id->file);
    id->n_bloks = (id->size / id->block_size);
        semsignal(shared_semaphore);
    return fd;
}

int execute_command(mydfs *id, client_query *command)
{
    int cmd = command->command;
    char *path = command->path;
    int mode = command->mode;
    long block_no = command->block_no;

    switch (cmd) {
        case 0: //open
        {

            if(id->file > 0)//fare check su file gia' aperto...
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - FILE ALREADY OPEN", id->process_id);
                if(send_err_response(id, -3, "ALREADY_OPEN") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }

                return -3;
            }
            id->block_size = command->block_size;
            id->mode = mode;
            if((id->file = open_local_file(id, path, mode)) < 0) //faccio roba tra cui alloco, lock cazzi e mazzi
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - FILE OPEN FAILED", id->process_id);
                if(send_err_response(id, -3, "OPEN_FILE_FAILED") < 0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                close_socket(&id->command_sock);
                return 1; //come se avesse chiuso
            }
            SOCKET listen_sock = open_data_sock(id);
            if(listen_sock == INVALID_SOCKET)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - OPEN DATA SOCK FAILED", id->process_id);
                if (send_err_response(id, -2, "OPEN_DSOCK_FAILED") < 0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -2;
            }
            int port = get_sock_port(listen_sock);
            if (port < 0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - GET DATA SOCK PORT FAILED: %s", id->process_id, GetErrorMessage(WSAGetLastError()));
                if(send_err_response(id, -2, "OPEN_DSOCK_FAILED") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -2;
            }
            if (listen(listen_sock, 1) == SOCKET_ERROR) {
                syslog(logger,
                       EVENTLOG_ERROR_TYPE,
                       "Child #%d open - LISTEN ON DATA SOCK FAILED : %s",
                       id->process_id,
                       GetErrorMessage(WSAGetLastError()));
                if (send_err_response(id, -2, "OPEN_DSOCK_FAILED") < 0) {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -2;
            }

            /* creo messaggio 800 OPEN_OK xxx */
            if(send_open_message(id, port) <0)
                return -1;
            //in attesa di connessione da parte dell'host

            if (poll_read(listen_sock))
            {
                id->data_sock = accept(listen_sock, NULL, NULL);
                if (id->data_sock == INVALID_SOCKET)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - ACCEPT DATA SOCK FAILED", id->process_id);
                    if(send_err_response(id, -2, "ACCEPT_DSOCK_FAILED") < 0)
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                        close_socket(&listen_sock);
                        return -1;
                    }
                    close_socket(&listen_sock);
                    return -1;
                }
            }
            else{
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d open - CLIENT CONNECT TIMEOUT", id->process_id);
                if(send_err_response(id, -1, "ACCEPT_DSOCK_TIMEOUT") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    close_socket(&listen_sock);
                    return -1;
                }
                close_socket(&listen_sock);
                return -1;
            }
            if(send_response(id, DFS_OK_CODE, "DATA_CONNECTION_ACCEPTED") <0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "send_response: %s", GetErrorMessage(WSAGetLastError()));
                close_socket(&listen_sock);
                return -1;
            }
            close_socket(&listen_sock); //ho accettato, chiudo la listensock
            //SEZIONE CRITICA
            semwait(shared_semaphore);
            shared_struct *curr_struct = get_shared_struct(id->process_id);
            strncpy(curr_struct->path, path, strlen(path)+1 > SHARED_PATH_LEN? SHARED_PATH_LEN : strlen(path)+1);
            curr_struct->wr = (mode & (MDFSO_WRONLY | MDFSO_RDWR)) != 0? 1 : 0;
            curr_struct->size = id->size;
            semsignal(shared_semaphore);
        }
            break;
        case 1:
        {//close
            //CHECK FILE
            if(id->file < 0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d close - FILE NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "FILE_NOT_OPENED") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -1;
            }
            //CHECK FDs
            if(id->data_sock == INVALID_SOCKET)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d close - DATA SOCK NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "DATASOCK_NOT_OPENED") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -1;
            }
            close_handled(&id->file);
            semwait(shared_semaphore);
            shared_struct *curr_struct = get_shared_struct(id->process_id);
            curr_struct->path[0] = '\0'; //devo comunicare agli altri processi che il file da adesso e' chiuso, senza attendere le procedure di cleaning up
            semsignal(shared_semaphore);
            //infine mando bye
            if(send_response(id, DFS_CLSE_CODE, "BYE") <0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "send_response: %s", GetErrorMessage(WSAGetLastError()));
                return -1;
            }
            close_socket(&id->command_sock);
            close_socket(&id->data_sock);
            return 1;
            //return 2; in teoria muore da solo?
        }
            break;
        case 2: //read
        {
            //CHECK FILE
            if(id->file <= 0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d read - FILE NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "FILE_NOT_OPENED")<0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -1;
            }
            //CHECK DATA SOCK
            if (id->data_sock == INVALID_SOCKET)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d read - DATA SOCK NOT OPENED", id->process_id);
                if(send_err_response(id, -2, "DATA_SOCK_NOT_OPENED")<0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -1;
            }
            if(send_block(id, block_no) < 0)
            {
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d read - SEND BLOCK ERROR", id->process_id);
                if (send_err_response(id, DFS_ERRN_CODE, "SEND BLOCK ERROR") < 0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -2; //connesione chiusa / errore socket
            }
            break;
        }

        case 3: //write
        {
            char block_msg[100];
            char block_s[100]; // per stare larghi
            memset(block_s, 0, 100);
            sprintf(block_s, "%lu", block_no); // STO GIA USANDO UN LONG UNSIGNED
            memset(block_msg, 0, 100);
            strcat(block_msg, "RECEIVING_BLOCK");
            strcat(block_msg, DELIMITER);
            strcat(block_msg, block_s);
            if(send_response(id, DFS_OK_CODE, block_msg)<0)
                return -1;

            if (poll_read(id->data_sock))
            {
                if(receive_block(id, block_no, command->block_size) <= 0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d write - RECEIVE BLOCK ERROR", id->process_id);
                    return -2; //connesione chiusa / errore socket
                }
            }
            else{
                syslog(logger, EVENTLOG_ERROR_TYPE, "Child #%d write - RECEIVE BLOCK TIMEOUT", id->process_id);
                if(send_err_response(id, -1, "RECEIVE_BLOCK_TIMEOUT") <0)
                {
                    syslog(logger, EVENTLOG_ERROR_TYPE, "send_err_response: %s", GetErrorMessage(WSAGetLastError()));
                    return -1;
                }
                return -1;
            }

            semwait(shared_semaphore);
            unsigned int i;
            for (i = 0; i < PARALLELISM; i++)
            {
                if(*get_process_table(i) == 0) continue;
                shared_struct *curr_struct = get_shared_struct(i);
                if(strcmp(path, curr_struct->path) == 0)
                {
                    curr_struct->size = getFileSize(id->file);
                    if (i != id->process_id)
                    {
                        kill(i); //event trigger
                    }
                }
            }
            semsignal(shared_semaphore);
            break;
        }
        default:;
    }
    return 0;
}

int parse_command(mydfs *id, char *string, client_query *res)
{
    syslog(logger, EVENTLOG_INFORMATION_TYPE, "Child #%d command - %s", id->process_id, string);
    char *token;
    int cur_tok = 0;
    char command[5];
    memset(&command, 0, 5);
    char *saveptr = NULL;

    /* walk through other tokens */
    while((token = strtok_r(string, DELIMITER, &saveptr)) != NULL)
    {
        errno = 0;
        if(cur_tok == 0)
        {
            if(strlen(token) > 5) {return -1;}
            strcpy((char *)&command, token);
            string = NULL; //per chiamate successive
        }
        if(strcmp(command, DFS_OPEN_COMMAND) == 0)
        {
            switch (cur_tok) {
                case 0:
                    res->command = 0;
                    break;
                case 1:
                    strcpy(res->path, token);
                    break;
                case 2:{
                    int converted = (int)strtol(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
                        return -1;
                    }
                    res->mode = converted;
                    break;}
                case 3:{
                    unsigned long converted = strtoul(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
                        return -1;
                    }
                    res->block_size = converted;
                    break;}
                default:return -1;
            }
        }
        else if(strcmp(command, DFS_CLSE_COMMAND) == 0)
        {
            if(cur_tok == 0)
            {res->command = 1;}
            else return -1;
        }
        else if(strcmp(command, DFS_READ_COMMAND) == 0)
        {
            switch (cur_tok) {
                case 0:
                    res->command = 2;
                    break;
                case 1:
                {
                    unsigned long converted = strtoul(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
                        return -1;
                    }
                    res->block_no = converted;
                    break;
                }
                default:return -1;
            }
        }
        else if(strcmp(command, DFS_WRTE_COMMAND) == 0)
        {
            switch (cur_tok) {
                case 0:
                    res->command = 3;
                    break;
                case 1:
                {
                    unsigned long converted = strtoul(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
                        return -1;
                    }
                    res->block_no = converted;
                    break;
                }

                case 2:
                {
                    unsigned int converted = (unsigned int)strtoul(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(logger, EVENTLOG_ERROR_TYPE, "conversion error: %s", GetErrorMessage(GetLastError()));
                        return -1;
                    }
                    res->block_size = converted;//nel caso di una write block size assume questo significato, tanto siamo in C
                    break;
                }
                default:return -1;
            }
        }
        else if(strcmp(command, DFS_HTBT_COMMAND) == 0)
        {
            res->command=4;
        }
        cur_tok++;
    }
    return 1;
}

ssize_t send_response_sock(SOCKET fd, int op_code, char *msg)
{
    char buf[SHARED_PATH_LEN];
    char op_code_char[10];
    memset(buf, 0, SHARED_PATH_LEN);
    memset(op_code_char, 0, 10);
    //costruisco la stringa con i parametri acquisiti
    sprintf(op_code_char, "%d", op_code);
    strcat(buf, op_code_char);
    strcat(buf, DELIMITER);
    strcat(buf, msg);
    strcat(buf, "\n");
    ssize_t w = writen_nb(fd, buf, strlen(buf)); //non invio il carattere termionatore
    if (w <= 0)
        syslog(logger, EVENTLOG_ERROR_TYPE, "send response: %s", GetErrorMessage(WSAGetLastError()));
    return w;
}

ssize_t send_response(mydfs *id, int op_code, char *msg)
{
    return send_response_sock(id->command_sock, op_code, msg);
}

ssize_t send_err_response(mydfs *id, int err_code, char *msg)
{
    char buf[SHARED_PATH_LEN];
    memset(buf, 0, SHARED_PATH_LEN);
    char op_code_char[10];
    memset(op_code_char, 0, 10);
    sprintf(op_code_char, "%d", err_code);
    strcpy(buf, msg);
    strcat(buf, DELIMITER);
    strcat(buf, op_code_char);
    return send_response(id, DFS_ERRN_CODE, buf);
}

ssize_t send_open_message(mydfs *id, int port)
{
    char connect_msg[100];
    char port_s[10];
    char len[20];
    char blocks[20];
    memset(len, 0, 10);
    memset(blocks, 0, 20);
    memset(connect_msg, 0, 100);
    memset(port_s, 0, 10);
    sprintf(port_s, "%d", port);
    sprintf(len, "%d", id->size);
    sprintf(blocks, "%lu", id->n_bloks+1);
    strcat(connect_msg, "OPEN_OK");
    strcat(connect_msg, DELIMITER);
    strcat(connect_msg, port_s);
    strcat(connect_msg, DELIMITER);
    strcat(connect_msg, len);
    strcat(connect_msg, DELIMITER);
    strcat(connect_msg, blocks);
    return send_response(id, DFS_OPEN_CODE, connect_msg);
}

ssize_t send_block(mydfs *id, unsigned long block_no)
{
    if (block_no > id->n_bloks)
        return -1;
    char block[id->block_size];
    memset(&block, 0, id->block_size);
    ssize_t r = read_block(id->file, &block, block_no, id->block_size);
    if (r<0) //errore file
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "send block: %s", GetErrorMessage(GetLastError()));
        return r;
    }
    else if (block_no != id->n_bloks && r != id->block_size)
    {
        return -1; // se non sono nell'ultimo blocco, tutti gli altri devono avere dimensione fissa
    }
    char block_msg[100];
    char block_s[100]; // per stare larghi
    memset(block_s, 0, 100);
    sprintf(block_s, "%lu", block_no); // STO GIA USANDO UN LONG UNSIGNED
    memset(block_msg, 0, 100);
    strcat(block_msg, "SENDING_BLOCK");
    strcat(block_msg, DELIMITER);
    strcat(block_msg, block_s);
    if(send_response(id, DFS_SEND_CODE, block_msg)<0)
        return -1;
    ssize_t w = writen_nb(id->data_sock, block, r);
    if (w < 0) //errore o socket chiuso
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "send block: %s", GetErrorMessage(WSAGetLastError()));
    }
    return w;
}

ssize_t receive_block(mydfs *id, unsigned long block_no, ssize_t size)
{
    char block[size];
    memset(&block, 0, size);
    ssize_t r = readn_nb(id->data_sock, &block, size);
    if (r<0) //errore o socket chiuso
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "receive block: %s", GetErrorMessage(WSAGetLastError()));
        return r;
    }
    ssize_t w = write_block(id->file, block, block_no, id->block_size, r);
    if (w < 0) //errore o socket chiuso
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "receive block: %s", GetErrorMessage(GetLastError()));
    }
    return w;
}

SOCKET open_data_sock(mydfs *id)
{
    BOOL reuse = TRUE;
    SOCKET sockfd;
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "server data socket: %s", GetErrorMessage(WSAGetLastError()));
        return (INVALID_SOCKET);
    }//crea un Socket in ascolto e memorizza il file descriptor in listen_fd con protocollo TCP
    //abilito riuso indirizzo
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(BOOL)) == SOCKET_ERROR) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "setsockopt reuse address: %s", GetErrorMessage(WSAGetLastError()));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server)); //pulisco la struttura
    server.sin_family = AF_INET;   //tipo di socket internet
    server.sin_addr.s_addr = htonl(INADDR_ANY);  //accetta ogni ip in ingresso
    server.sin_port = 0;

    if(bind(sockfd, (struct sockaddr *) &server,
            sizeof(server)) == SOCKET_ERROR) {
        syslog(logger, EVENTLOG_ERROR_TYPE, "server socket bind: %s", GetErrorMessage(WSAGetLastError()));
        close_socket(&sockfd);
    }  //lego il file descriptor ai parametri impostati a servaddr

    return sockfd;
}

int get_free_id()
{
    int i = -1;
    int loc = 0;
    while(i++ < PARALLELISM && (loc = *(get_process_table(i))) != 0);
    if(i >= PARALLELISM)
        return -1;
    return i;
}

shared_struct *get_shared_struct(unsigned int i)
{
    if (i > PARALLELISM)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "get_shared_struct: invalid index #%d", i);
        exit(EXIT_FAILURE);
    }
    return ((shared_struct *)shr_mem) + i;
}

int *get_process_table(unsigned int i)
{
    if (i > PARALLELISM)
    {
        syslog(logger, EVENTLOG_ERROR_TYPE, "get_process_table: invalid index #%d", i);
        exit(EXIT_FAILURE);
    }
    return (((void *)shr_mem) + ((PARALLELISM + 1) * sizeof(shared_struct))) + (i*sizeof(int));
}
