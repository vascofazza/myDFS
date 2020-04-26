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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include "read_utilities.h"
#include "sem_utilities.h"
#include "mydfs_commons.h"
#include <pthread.h>
#include "configuration_manager.h"
#include <fcntl.h>
#include <syslog.h>

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
    int sock_fd; // server0 (connessione di controllo). Usato per il heart-beating thread
    pthread_t tid; //thread id
    int pid;
    char path[SHARED_PATH_LEN];
    sem_t *semaphore; //semaforo assegnato al processo/thread, heart-beating
    read_struct read_struct;
    ssize_t size; // size del file in locale
    int locked; // 1 se un lock esclusivo e' stato richeisto
} shared_struct;

typedef struct mydfs //client-specific struct / child process-thread side
{
    int command_sock;
    int data_sock;
    int mode;
    int file;
    ssize_t size;
    unsigned long n_bloks;
    unsigned long block_size;
    sem_t *semaphore; //on mac you need to reopen this, not returining the same fd
    int process_id; //unique id 0-PARALLELISM
    read_struct read_struct;
} mydfs;

static serverconfig configuration;

static int sockfd, new_fd, master_pid;  // listen on sock_fd, new connection on new_fd

static int active_threads = 0;

static int PARALLELISM = 0;

static void *shr_mem; //puntatore alla memoria condivisa

static int shr_mem_id; //identificatore memoria condivisa

static sem_t *shared_semaphore; //id del semaforo per l'accesso alla struttura condivisa

static sem_t *backlog_semaphore;

static sem_t *main_semaphore; //id del semaforo creato dal server0

static key_t main_key; //chiave per accedere alle risorse

int get_free_id();

int main_loop(int sock_fd);

int parse_command(mydfs *id, char *string, client_query *res);

int execute_command(mydfs *id, client_query *command);

int open_data_sock(mydfs *id);

ssize_t send_response(mydfs *id, int op_code, char *msg);

ssize_t send_response_sock(int fd, int op_code, char *msg);

ssize_t send_err_response(mydfs *id, int err_code, char *msg);

ssize_t send_open_message(mydfs *id, int port);

ssize_t send_block(mydfs *id, unsigned long block_no);

ssize_t receive_block(mydfs *id, unsigned long block_no, ssize_t size);

int open_local_file(mydfs *id, char *path, int mode);

int check_command(char *vptr, int len, char const *command);

shared_struct *get_shared_struct(unsigned int i);

int *get_process_table(unsigned int i);

int init_child_process(int *process_id);

int cleanup()
{
    int res = 0;
    destroy_sem(shared_semaphore);
    destroy_sem(backlog_semaphore);
    if(getpid() == master_pid) destroy_sem(main_semaphore);
    res += shmdt(shr_mem); //0 on completition
    if (shr_mem_id > 0) res += shmctl(shr_mem_id, IPC_RMID, NULL); //0 on completition
    closelog();
    return res;
}

void invalidate_handler(int s)
{
    semwait(shared_semaphore);
    int i;
    for (i = 0; i < PARALLELISM; i++)
    {
        shared_struct *curr_struct = get_shared_struct(i);
        if (*get_process_table(i) == 0)
        {
            continue;
        }

        if(pthread_equal(curr_struct->tid, pthread_self()))
        {
            syslog(LOG_DEBUG, "invalidation -> id: %d, tid: %p", i, pthread_self());
            char msg[100] = "CACHE_INVALIDATION";
            char size[50];
            sprintf(size, "%zd", curr_struct->size);
            strcat(msg, DELIMITER);
            strcat(msg, size);
            if (send_response_sock(curr_struct->sock_fd, DFS_INVD_CODE, msg) <0)
            {
                syslog(LOG_ERR, "send invalidation message: %m");
            }
        }
    }
    semsignal(shared_semaphore); // libero il semaforo allocato dalle procedure di invalidazione
}

void sigchld_handler(int s) //segnale inviato dai processi figli al momento della terminazione, handler necessario per fare una wait e non lasciare processi zombie
{
    syslog(LOG_DEBUG, "Received signal %d", s);
    if (s == SIGINT)
    {
        exit(cleanup());
    }
    else if(s == SIGCHLD)
    {
        int status;
        while(waitpid(-1, &status, WNOHANG) > 0) // NECESSARIO PERCHE DATI NEL PROCESSO PRINCIPALE
        {
            status = status>>8; //bit 8-15 exit code
            if(status == (EXIT_FAILURE))
            {
                syslog(LOG_ERR, "Child terminated with error: %d", status);
                //                exit(EXIT_FAILURE);
            }
            active_threads--;
            semsignal(backlog_semaphore);
        }

    }
    //see http://www.tutorialspoint.com/unix_system_calls/wait.htm
}

uint16_t get_sock_port(int sock_fd) //usata per indicare al client su che porta connettersi
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa); /*
                                 The address_len parameter should be initialized to indicate the amount of
                                 space pointed to by address.  On return it contains the actual size of
                                 the address returned (in bytes).
                                 */
    int res = getsockname(sock_fd, (struct sockaddr *)&sa, &len);
    if (res < 0){ return -1; }
    return ntohs(sa.sin_port); //TODO dovrebbe essere uint16_t ?
}

void daemonize()
{
    /* Our process ID and Session ID */
    pid_t pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
     we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Open any logs here */

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }

    struct sigaction sa; //per installare l'handler
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask); //http://stackoverflow.com/questions/20684290/signal-handling-and-sigemptyset azzero la maschera
    sa.sa_flags = SA_RESTART; //system calls interrotte dai segnali sono rinizializzate automaticamente.
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction error: %m");
        exit(EXIT_FAILURE);
    }

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
     we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }

    /* Close out the standard file descriptors */
    if(close(STDIN_FILENO) < 0 || close(STDOUT_FILENO) < 0 || close(STDERR_FILENO) < 0)
    {
        syslog(LOG_ERR, "close std fds: %m");
        exit(EXIT_FAILURE);
    }
    int i;
    if ((i = open("/dev/null", O_RDONLY)) != 0) {
        dup2(i, STDIN_FILENO);
        close_handled(&i);
    }
    if ((i = open("/dev/null", O_WRONLY)) != 1) {
        dup2(i, STDOUT_FILENO);
        close_handled(&i);
    }
    if ((i = open("/dev/null", O_WRONLY)) != 2) {
        dup2(i, STDERR_FILENO);
        close_handled(&i);
    }
}

int main(int argc, char **argv)
{
    openlog("MyDFS Server", LOG_PID | LOG_PERROR | LOG_NDELAY, LOG_USER);
    load_server_config(&configuration);
    if (argc == 3 && strcmp("-p", argv[1]) == 0)
    {
        unsigned int converted = (int)strtoul(argv[2], (char **)NULL, 10);
        if(converted == 0 && (errno == EINVAL || errno == ERANGE))
        {
            syslog(LOG_ERR, "conversion error: %m");
            return -1;
        }
        configuration.port = converted;
    }

    //creo la chiave

    if ((main_key = ftok("mydfs_server.config", '8')) < 0) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    if(configuration.daemon)
        daemonize();

    master_pid = getpid();

    syslog(LOG_INFO, "starting with port %d", configuration.port);

    atexit(&cleanup);

    sigset_t mask;

    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigaddset(&mask, SIGCHLD);

    //master_pid = getpid();
    struct sockaddr_in server;
    struct sockaddr_storage their_addr; // connector's address information
    struct sigaction sa; //per installare l'handler
    int reuse=1;
    char s[INET6_ADDRSTRLEN]; //inidizzo ip testuale

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        syslog(LOG_ERR, "server socket: %m");
        exit(EXIT_FAILURE);
    }//crea un Socket in ascolto e memorizza il file descriptor in listen_fd con protocollo TCP

    //abilito riuso indirizzo
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(int)) == -1) {
        syslog(LOG_ERR, "setsockopt reuse address: %m");
        exit(EXIT_FAILURE);
    }

    PARALLELISM = configuration.n_processes;

    memset(&server, 0, sizeof(server)); //pulisco la struttura
    server.sin_family = AF_INET;   //tipo di socket internet
    server.sin_addr.s_addr = htonl(INADDR_ANY);  //accetta ogni ip in ingresso
    server.sin_port = htons(configuration.port);  //imposta la porta


    if(bind(sockfd, (struct sockaddr *) &server,
            sizeof(server))<0) {
        close_handled(&sockfd);
        syslog(LOG_ERR, "server socket bind: %m");
    }  //lego il file descriptor ai parametri impostati a servaddr

    if (listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen: %m");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask); //http://stackoverflow.com/questions/20684290/signal-handling-and-sigemptyset azzero la maschera
    sa.sa_flags = SA_RESTART; //system calls interrotte dai segnali sono rinizializzate automaticamente. TRANNE sem_wait/post
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction: %m");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction, %m"); // Should not happen
        exit(EXIT_FAILURE);
    }

    struct sigaction sa1;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags= SA_RESTART;
    sa1.sa_handler = invalidate_handler;
    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        syslog(LOG_ERR, "sigaction: %m"); // Should not happen
        exit(EXIT_FAILURE);
    }

    //creo memoria condivisa

    /* connect to (and possibly create) the segment: */
    if ((shr_mem_id = shmget(main_key, (PARALLELISM + 1) * sizeof(shared_struct) + (PARALLELISM +1) * sizeof(int), 0644 | IPC_CREAT)) == -1) {
        syslog(LOG_ERR, "shmget: %m");
        exit(EXIT_FAILURE);
    }

    /* attach to the segment to get a pointer to it: */
    shr_mem = shmat(shr_mem_id, (void *)0, 0);
    if (shr_mem == (char *)(-1)) {
        syslog(LOG_ERR, "shmat: %m");
        exit(EXIT_FAILURE);
    }

    //Azzero memoria
    memset(shr_mem, 0, (PARALLELISM + 1) * sizeof(shared_struct) + (PARALLELISM +1) * sizeof(int));

    //creo semafori, uno globale (#threads+1) per sharer memory, uno per ogni semaforo (#thread) per socket ERRORI GESTITI DALLA LIBRERIA sem_utilites
    backlog_semaphore = sem_open("backlog_semaphore", O_CREAT | O_EXCL, 0644,PARALLELISM);// init_sem(-1);//(main_key, PARALLELISM+1);
    if (backlog_semaphore < 0)
    {
        syslog(LOG_ERR, "backlog semaphore init: %m");
        exit(EXIT_FAILURE);
    }
    sem_unlink("backlog_semaphore");

    unlink_sem(-1); //se muore in maniera anomala..
    main_semaphore = init_sem(-1);

    if (main_semaphore < 0)
    {
        syslog(LOG_ERR, "main semaphore init: %m");
        exit(EXIT_FAILURE);
    }

    unlink_sem(-2);
    shared_semaphore = init_sem(-2);

    if (shared_semaphore < 0)
    {
        syslog(LOG_ERR, "shared semaphore init: %m");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "server: waiting for connections...");

    //DEBUG
    int z = 0;
    for (z = 0; z < PARALLELISM; z++)
        unlink_sem(z);
    //DEBUG

    while(1) {

        semwait(backlog_semaphore);
        semwait(main_semaphore);
        socklen_t sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd < 0) {
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

        //durante la fase di accepting disabilito i segnali, i segnali ricevuti nel frattempo verranno elaborati dopo
        if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
            syslog(LOG_ERR, "sigprocmask: %m");
            return -1;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        syslog(LOG_INFO, "server: got connection from %s", s);

        int process_id = 0; //id del processo attuale
        semwait(shared_semaphore);

        //prendo il primo id libero
        if((process_id = get_free_id()) < 0)
        {
            syslog(LOG_ERR, "no free ids"); //NON DOVREBBE ACCADERE
            close_handled(&new_fd);
            semsignal(backlog_semaphore);
            semsignal(main_semaphore);
            semsignal(shared_semaphore);
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) {
                syslog(LOG_ERR, "sigprocmask: %m");
                return -1;
            }
            continue;
        }

        semsignal(shared_semaphore);

        sem_t *process_semaphore = init_sem(process_id);
        //blocco il processo finche' non ho settato tutti i parametri

        semwait(process_semaphore);

        //non libero la sezione critica cosi' se anche il processo figlio termina prima della signal ho il tempo di impostare i gen...
        //update 2... questo lo sblocco nella init del processo / thread cosi' non ho sovrapposizione di ID
        if(configuration.threaded)
        {
            pthread_t thread;
            if (pthread_create(&thread, NULL, &init_child_process, &process_id) != 0)
            {
                syslog(LOG_ERR, "thread init: %m");
                exit(EXIT_FAILURE);
            }
            if(pthread_detach(thread) < 0)
            {
                syslog(LOG_ERR, "thread detach: %m");
                exit(EXIT_FAILURE);
            }
        }
        else{
            if (!fork()) // this is the child process
            {
                syslog(LOG_DEBUG, "Child with process id #%d spawned", process_id);
                close_handled(&sockfd); // child doesn't need the listener

                main_semaphore = open_sem(-1);
                if (main_semaphore < 0)
                {
                    syslog(LOG_ERR, "main semaphore open: %m");
                    exit(EXIT_FAILURE);
                }

                shared_semaphore = open_sem(-2);
                if (shared_semaphore < 0)
                {
                    semsignal(main_semaphore);
                    syslog(LOG_ERR, "shared semaphore open: %m");
                    exit(EXIT_FAILURE);
                }

                /* attach to the segment to get a pointer to it: */
                shr_mem = shmat(shr_mem_id, (void *)0, 0);
                if (shr_mem == (char *)(-1)) {
                    shr_mem_id = -1;
                    semsignal(main_semaphore);
                    syslog(LOG_ERR, "shmat child: %m");
                    exit(EXIT_FAILURE);
                }
                shr_mem_id = -1; // alla chiusura non cancello la memoria condivisa
                if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) { //attivo i segnali nel figlio
                    syslog(LOG_ERR, "sigprocmask child: %m");
                    return -1;
                }

                int return_code = init_child_process(&process_id);
                if(return_code < 0)
                {
                    syslog(LOG_ERR, "PROCESS EXITED WITH CODE: %d", return_code);
                    exit(EXIT_FAILURE);
                }
                //close_handled(&new_fd);
                //lo chiudo gia' nella close()

                exit(EXIT_SUCCESS);
            }
            else
                close_handled(&new_fd);
        }
        semwait(shared_semaphore);

        syslog(LOG_DEBUG, "Init current id: %d", process_id);
        active_threads++;
        shared_struct *curr_struct = get_shared_struct(process_id);
        if (curr_struct->semaphore != NULL)
            destroy_sem(curr_struct->semaphore);
        memset(curr_struct, 0, sizeof(shared_struct));
        curr_struct->sock_fd = new_fd; //idem qui!, ma se coincide? rcatroia..?
        curr_struct->semaphore = process_semaphore; //questo e' il mio semaforo.. ogni processo si ri-apre il suo
        *get_process_table(process_id) = 1;
        semsignal(shared_semaphore);
        semsignal(process_semaphore);

        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) {
            syslog(LOG_ERR, "sigprocmask: %m");
            return -1;
        }
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

int init_child_process(int *process_id)
{
    int id = *process_id;
    int res = main_loop(id);
    semwait(shared_semaphore);
    active_threads--;
    *get_process_table(id) = 0;
    semsignal(shared_semaphore);
    if(configuration.threaded)
        semsignal(backlog_semaphore); //se e' un processo invece lo aumento nel chld handler
    return res;
}

int main_loop(int process)
{
    char buf[512];
    memset(&buf, 0, 512);
    mydfs id;
    memset(&id, 0, sizeof(mydfs));
    id.data_sock = -1;
    id.file = -1;
    id.mode = -1;
    id.process_id = process;

    //apro il semaforo
    sem_t *tempSem = open_sem(process);
    unlink_sem(process);
    semwait(tempSem);
    semsignal(tempSem); //piccola barriera
    //Creo copia locale, lo faccio qui dentro per essere sicuro di essere sincronizzato in qualche modo..

    //se esco allora tutta la struttura e' pronta, chiudo il semaforo e continuo
    semsignal(main_semaphore); //sblocco il thread principale, ho copiato l'id e tutto

    semwait(shared_semaphore);
    shared_struct *curr_struct = get_shared_struct(process);
    curr_struct->tid = pthread_self();
    curr_struct->pid = getpid();
    id.semaphore = tempSem;//curr_struct->semaphore;
    id.command_sock = configuration.threaded ? curr_struct->sock_fd : new_fd;
    if (!configuration.threaded)
        curr_struct->sock_fd = new_fd;
    curr_struct->read_struct = id.read_struct;
    semsignal(shared_semaphore);

    client_query res;
    memset(&res, 0, sizeof(client_query));

    int ret = 0;

    sigset_t mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGUSR1);
    sigaddset(&mask, SIGCHLD);

    while(1)
    {
        //ZONA CRITICA -- LETTURA/SCRITTURA SOCKET CONTROLLO
        ssize_t numbytes = readline_nb(id.command_sock, buf, sizeof(buf), &id.read_struct);
        semwait(id.semaphore);
        if (numbytes == -1)
        {
            syslog(LOG_ERR, "recv loop: %m");
            ret = -1;
            break;
        }
        else if (numbytes == 0) //il client ha chiuso la connessione esplicitamente / TIMEOUT
        {
            ret = 0;
            break;
        }
        //        //durante la fase di elaborazione disabilito i segnali, i segnali ricevuti nel frattempo verranno elaborati dopo
        if (pthread_sigmask(SIG_BLOCK, &mask, NULL) < 0) {
            syslog(LOG_ERR, "pthread_sigmask: %m");
            ret = -1;
            break;
        }
        if(numbytes == -3) //TIMEOUT
        {
            //solo se attivo lato client?
            if((id.mode & (MDFSO_RDWR | MDFSO_WRONLY | MDFSO_EXLOCK)) != 0)
            {
                if(send_response_sock(curr_struct->sock_fd, DFS_HTBT_CODE, "BEAT") < 0)
                {
                    syslog(LOG_ERR, "send beat: %m");
                    ret = -1;
                    break;
                }
                char buf[10];
                long beat = readline_nb(curr_struct->sock_fd, buf, 10, &curr_struct->read_struct);
                if(beat < 0 || check_command(buf, 10, DFS_HTBT_COMMAND) < 0)
                {
                    //killo tutto
                    syslog(LOG_INFO, "Client #%d killed", id.process_id);
                    ret = 0;
                    break;
                }
                syslog(LOG_INFO, "client %d beated", id.process_id);
            }

            semsignal(id.semaphore);
            continue; // rilascio il semaforo
        }
        if(parse_command(&id, buf, &res) != 1)
        {
            if(send_response(&id, DFS_ERRN_CODE, "PARSING_ERROR")<0)
            {
                syslog(LOG_ERR, "send error: %m");
                ret = -1;
                break;
            }
            syslog(LOG_ERR, "parse_command error");
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
            syslog(LOG_ERR, "execute_command error");
            ret = -1;
            break;
        }

        if(sem_post(id.semaphore) < 0)
        {
            syslog(LOG_ERR, "sem_post thread loop: %m");
            ret = -1;
            break;
        }
        if (pthread_sigmask(SIG_UNBLOCK, &mask, NULL) < 0) {
            syslog(LOG_ERR, "pthread_sigmask: %m");
            ret = -1;
            break;
        }
        memset(&buf, 0, sizeof(buf));
    }
    //interrompo il while e chiudo tutto
    if(ret == process)
    {
        semsignal(id.semaphore);
        destroy_sem(id.semaphore);
        return ret;
    }
    close_handled(&id.file);
    close_handled(&id.data_sock);
    close_handled(&id.command_sock);
    semsignal(id.semaphore);
    destroy_sem(id.semaphore);
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

    int i;
    for (i = 0; i < PARALLELISM; i++)
    {
        if(*get_process_table(i) == 0) continue;
        shared_struct *curr_struct = get_shared_struct(i);
        int cmp = strcmp(path, curr_struct->path);
        if((curr_struct->wr || curr_struct->locked) && !cmp)
        {
            semsignal(shared_semaphore);
            syslog(LOG_ERR, "Child #%d - file %s opened by child #%d", id->process_id, path, i);
            return -1; //-1 in caso il file è aperto in scrittura da un altro client;
        }
        else if (curr_struct->wr == 0 && ((id->mode&(MDFSO_RDWR|MDFSO_WRONLY)) != 0) && cmp == 0)
        {
            if(configuration.threaded)
            {
                if(pthread_kill(curr_struct->tid, SIGUSR1) != 0)
                {
                    syslog(LOG_ERR, "send invalidation error - pthread_kill: %m");
                }
            }
            else
            {
                if(kill(curr_struct->pid, SIGUSR1) != 0)
                {
                    syslog(LOG_ERR, "send invalidation error - kill: %m");
                }
            }
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

    int fd = open(n_path, mode, 0644); //apre un file, eventualmente lo crea -rw-r--r--
    //non e' necessario settare la umask (default 022 ottale)
    if (fd < 0) {
        syslog(LOG_ERR, "file open: %m");
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
                syslog(LOG_ERR, "Child #%d open - FILE ALREADY OPEN", id->process_id);
                if(send_err_response(id, -3, "ALREADY_OPEN") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }

                return -3;
            }
            id->block_size = command->block_size;
            id->mode = mode;
            if((id->file = open_local_file(id, path, mode)) < 0) //faccio roba tra cui alloco, lock e roba varia
            {
                syslog(LOG_ERR, "Child #%d open - FILE OPEN FAILED", id->process_id);
                if(send_err_response(id, -3, "OPEN_FILE_FAILED") < 0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                close_handled(&id->command_sock);
                return 1;
            }
            int listen_sock = open_data_sock(id);
            if(listen_sock < 0)
            {
                syslog(LOG_ERR, "Child #%d open - OPEN DATA SOCK FAILED", id->process_id);
                if (send_err_response(id, -2, "OPEN_DSOCK_FAILED") < 0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -2;
            }
            if (listen(listen_sock, 1) < 0)
            {
                syslog(LOG_ERR, "Child #%d open - LISTEN ON DATA SOCK FAILED", id->process_id);
                if(send_err_response(id, -2, "OPEN_DSOCK_FAILED") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -2;
            }
            int port = get_sock_port(listen_sock);
            if (port < 0)
            {
                syslog(LOG_ERR, "Child #%d open - GET DATA SOCK PORT FAILED", id->process_id);
                if(send_err_response(id, -2, "OPEN_DSOCK_FAILED") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
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
                if (id->data_sock < 0)
                {
                    syslog(LOG_ERR, "Child #%d open - ACCEPT DATA SOCK FAILED", id->process_id);
                    if(send_err_response(id, -2, "ACCEPT_DSOCK_FAILED") < 0)
                    {
                        syslog(LOG_ERR, "send_err_response: %m");
                        close_handled(&listen_sock);
                        return -1;
                    }
                    close_handled(&listen_sock);
                    return -1;
                }
            }
            else{
                syslog(LOG_ERR, "Child #%d open - CLIENT CONNECT TIMEOUT", id->process_id);
                if(send_err_response(id, -1, "ACCEPT_DSOCK_TIMEOUT") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    close_handled(&listen_sock);
                    return -1;
                }
                close_handled(&listen_sock);
                return -1;
            }
            if(send_response(id, DFS_OK_CODE, "DATA_CONNECTION_ACCEPTED") <0)
            {
                syslog(LOG_ERR, "send_response: %m");
                close_handled(&listen_sock);
                return -1;
            }
            close_handled(&listen_sock); //ho accettato, chiudo la listensock
            //SEZIONE CRITICA
            semwait(shared_semaphore);
            shared_struct *curr_struct = get_shared_struct(id->process_id);
            strncpy(curr_struct->path, path, strlen(path)+1 > SHARED_PATH_LEN? SHARED_PATH_LEN : strlen(path)+1); //TODO check lunghezza
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
                syslog(LOG_ERR, "Child #%d close - FILE NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "FILE_NOT_OPENED") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -1;
            }
            //CHECK FDs
            if(id->data_sock < 0)
            {
                syslog(LOG_ERR, "Child #%d close - DATA SOCK NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "DATASOCK_NOT_OPENED") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
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
                syslog(LOG_ERR, "send_response: %m");
                return -1;
            }
            close_handled(&id->command_sock);
            close_handled(&id->data_sock);
            return 1;
            //return 2; in teoria muore da solo?
        }
            break;
        case 2: //read
        {
            //CHECK FILE
            if(id->file <= 0)
            {
                syslog(LOG_ERR, "Child #%d read - FILE NOT OPENED", id->process_id);
                if(send_err_response(id, -3, "FILE_NOT_OPENED")<0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -1;
            }
            //CHECK DATA SOCK
            if (id->data_sock <= 0)
            {
                syslog(LOG_ERR, "Child #%d read - DATA SOCK NOT OPENED", id->process_id);
                if(send_err_response(id, -2, "DATA_SOCK_NOT_OPENED")<0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -1;
            }
            if(send_block(id, block_no) < 0)
            {
                syslog(LOG_ERR, "Child #%d read - SEND BLOCK ERROR", id->process_id);
                if (send_err_response(id, DFS_ERRN_CODE, "SEND BLOCK ERROR") < 0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
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
                    syslog(LOG_ERR, "Child #%d write - RECEIVE BLOCK ERROR", id->process_id);
                    return -2; //connesione chiusa / errore socket
                }
            }
            else{
                syslog(LOG_ERR, "Child #%d write - RECEIVE BLOCK TIMEOUT", id->process_id);
                if(send_err_response(id, -1, "RECEIVE_BLOCK_TIMEOUT") <0)
                {
                    syslog(LOG_ERR, "send_err_response: %m");
                    return -1;
                }
                return -1;
            }

            semwait(shared_semaphore);
            int i;
            for (i = 0; i < PARALLELISM; i++)
            {
                if(*get_process_table(i) == 0) continue;
                shared_struct *curr_struct = get_shared_struct(i);
                if(strcmp(path, curr_struct->path) == 0)
                {
                    curr_struct->size = getFileSize(id->file);
                    if (i != id->process_id)
                    {
                        if(configuration.threaded)
                        {
                            if(pthread_kill(curr_struct->tid, SIGUSR1) != 0)
                            {
                                syslog(LOG_ERR, "send invalidation error - pthread_kill: %m");
                            }
                        }
                        else
                        {
                            if(kill(curr_struct->pid, SIGUSR1) != 0)
                            {
                                syslog(LOG_ERR, "send invalidation error - kill: %m");
                            }
                        }
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
    syslog(LOG_DEBUG, "Child #%d command - %s", id->process_id, string);
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
                        syslog(LOG_ERR, "conversion error: %m");
                        return -1;
                    }
                    res->mode = converted;
                    break;}
                case 3:{
                    unsigned long converted = strtoul(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(LOG_ERR, "conversion error: %m");
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
                    long converted = strtol(token, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        syslog(LOG_ERR, "conversion error: %m");
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
                        syslog(LOG_ERR, "conversion error: %m");
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
                        syslog(LOG_ERR, "conversion error: %m");
                        return -1;
                    }
                    res->block_size = converted;//nel caso di una write block size assume questo significato
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

ssize_t send_response_sock(int fd, int op_code, char *msg)
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
        syslog(LOG_ERR, "send response: %m");
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
    sprintf(len, "%zd", id->size);
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
        syslog(LOG_ERR, "send block: %m");
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
        syslog(LOG_ERR, "send block: %m");
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
        syslog(LOG_ERR, "receive block: %m");
        return r;
    }
    ssize_t w = write_block(id->file, block, block_no, id->block_size, r);
    if (w < 0) //errore o socket chiuso
    {
        syslog(LOG_ERR, "receive block: %m");
    }
    return w;
}

int open_data_sock(mydfs *id)
{
    int sockfd;
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        syslog(LOG_ERR, "server data socket: %m");
        return (-1);
    }//crea un Socket in ascolto e memorizza il file descriptor in listen_fd con protocollo TCP
    return sockfd;
}

int get_free_id()
{
    int i = -1;
    int loc = 0;
    while(i++ < PARALLELISM && (loc = *(get_process_table(i))) != 0);
    if(i >= PARALLELISM) return -1;
    return i;
}

shared_struct *get_shared_struct(unsigned int i)
{
    if (i > PARALLELISM)
    {
        syslog(LOG_ERR, "get_shared_struct: invalid index #%d", i);
        exit(EXIT_FAILURE);
    }
    return ((shared_struct *)shr_mem) + i;
}

int *get_process_table(unsigned int i)
{
    if (i > PARALLELISM)
    {
        syslog(LOG_ERR, "get_process_table: invalid index #%d", i);
        exit(EXIT_FAILURE);
    }
    return (((void *)shr_mem) + ((PARALLELISM + 1) * sizeof(shared_struct))) + (i*sizeof(int));
}
