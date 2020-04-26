//
//  test_client.c
//
//
//  Created by Federico Scozzafava on 15/02/15.
//
//
/*
 ** client.c -- a stream socket client demo
 */

#include "client.h"

unsigned int DEBUG_HTBT = 0;

int nonblocking_connect(MyDFSId *id, int fd, int timeout);

int nonblocking_send_query(MyDFSId *id, const char *com, const char *path, long *param1, unsigned long *param2);

int mydfs_connect(const char *ip, const char *port, MyDFSId *client);

int nonblocking_receive_response(MyDFSId *id);

ssize_t file_resize(MyDFSId *id, ssize_t size);

void heart_beating_thread(MyDFSId *id);

int mydfs_connect(const char *ip, const char *port, MyDFSId *id)
{
    int sockfd = 0;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(ip, port, &hints, &servinfo)) != 0) {
//        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
//            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close_handled(&sockfd);
            continue;
        }

        id->ctrl_fd = sockfd;
        id->server = *((struct sockaddr_in *)p->ai_addr); //effettuo fisicamente una copia
        break;
    }

    if (p == NULL) {
        if(errno == EINTR)
        {
            return mydfs_connect(ip, port, id);
        }
//        perror("client: connect");
//        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
              s, sizeof s);
//    printf("client: connecting to %s\n", s);

    freeaddrinfo(servinfo); // all done with this structure

    return 0;
}

int nonblocking_receive_response(MyDFSId *id)
{
    int MASTER_op_code = 0;
    int sock_fd = id->ctrl_fd;
    char string[512];
    int repeat = 0;
    int op_code = 0;
    do{
        memset(string, 0, 512);

        ssize_t numbytes = readline_nb(sock_fd, string, sizeof(string), &(id->read_line_s));
        if (numbytes == -1)
        {
            //perror("recv");
            return(-2);
        }
        else if(numbytes == -3)
        {
            return -2; //timeout
        }
        else if (numbytes == 0) //il client ha chiuso la connessione esplicitamente
        {
            return -2; //errore di connessione / host ha chiuso la connessione
        }
        repeat = 0;
        char *token;
        char msg[256];
        memset(&msg, 0, 256);
        char *saveptr;
        op_code = 0;
        int current_token = 0;
        char *string_ptr = string;


        /* walk through other tokens */
        while((token = strtok_r(string_ptr, DELIMITER, &saveptr)) != NULL)    {
//            printf( "%s\n", token );
            string_ptr = NULL;
            if(op_code == 0)
            {
                op_code = (int)strtol(token, (char **)NULL, 10);
                if(op_code == 0 && (errno == EINVAL || errno == ERANGE))
                    return -1;
            }
            else if(msg[0] == '\0')
            {
                strcpy(msg, token);
            }
            else
            {
                switch (op_code) {
                    case DFS_OPEN_CODE:
                    {
                        MASTER_op_code = op_code;
                        int converted = (int)strtol(token, (char **)NULL, 10);
                        if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                            return -1;
                        switch (current_token++) {
                            case 0:
                            {
                                id->server.sin_port = htons(converted);
                            }
                                break;
                            case 1:
                            {
                                id->size = converted;
                            }
                                break;
                            case 2:
                            {
                                id->n_blocks = converted;

                                int fd = socket(AF_INET,SOCK_STREAM,0);
                                if(fd < 0)
                                {
                                    perror("data socket");
                                    return -1;
                                }

                                if (nonblocking_connect(id, fd, 100) < 0)
                                {
                                    perror("data socket connect");
                                    return -1;
                                }

                                if(nonblocking_receive_response(id) != DFS_OK_CODE)
                                {
                                    perror("receive_response");
                                    return -1;
                                }

                                id->data_fd = fd;
                                //apro il mapping
                                void *map = mmap(NULL, id->n_blocks == 0? id->config.block_size:id->n_blocks*id->config.block_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
                                if(map == MAP_FAILED)
                                {
                                    perror("mmap");
                                    return -1;
                                }
                                void *cache = calloc(id->n_blocks == 0? 1: id->n_blocks, sizeof(char));
                                id->cache = (char *)cache;
                                id->mapping = map; //salvo la mappa
                                if(id->size == 0)
                                {
                                    id->cache[0] = 1;
                                }
                                id->allocated_blocks = id->n_blocks == 0? 1: id->n_blocks; //TODO VERIFICARE I VECCHI TEST
                            }
                                break;
                            default:
                                return -1;
                                break;
                        }
                        break;
                    }
                    case DFS_SEND_CODE:
                    {
                        MASTER_op_code = op_code;
                        switch (current_token++) {
                            case 0:
                            {
                                char *cache = id->cache;
                                long converted = strtol(token, (char **)NULL, 10);
                                if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                                    return -1;
                                unsigned long block = converted;
                                if (cache[block] > 0) // il blocco e' gia presente in memoria
                                {
                                    return -1;
                                }
                                int receive_sock = id->data_fd;
                                char blk[id->config.block_size];
                                char *block_ptr = blk;
                                memset(block_ptr, 0, id->config.block_size);
                                ssize_t r = (block+1 == id->n_blocks) ? id->size - ((id->n_blocks-1) * id->config.block_size) : id->config.block_size;
                                //MI SERVE PER IMPEDIRE CHE IL FILE VENGA AMPLIATO ED AVERE CASINI
                                ssize_t res = readn_nb(receive_sock, block_ptr, r);
                                if (res != r)
                                {
                                    if (res == -3)
                                        return -3; //timeout
//                                    perror("receive_block");
                                    return -1;
                                }
                                cache[block] = 1;
                                memcpy(id->mapping+(block*id->config.block_size), block_ptr, r);

                            }
                                break;

                            default:
                                return -1;
                                break;
                        }
                        break;
                    }

                    case DFS_RECV_CODE:
                    {
                        MASTER_op_code = op_code;
                        switch (current_token++) {
                            case 0:
                            {
                                char *cache = id->cache;
                                unsigned long converted = strtoul(token, (char **)NULL, 10);
                                if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                                    return -1;
                                unsigned long block = converted;
                                if (cache[block] < 1)
                                {
                                    return -1;
                                }
                                int send_sock = id->data_fd;

                                char *block_ptr = id->cache + (block*id->config.block_size);
                                ssize_t r = (block+1 == id->n_blocks) ? id->size - ((id->n_blocks-1) * id->config.block_size) : id->config.block_size;
                                ssize_t res = writen_nb(send_sock, block_ptr, r);
                                if (res != r)
                                {
                                    if(res != -3)
                                    {
//                                        perror("send_block");
                                        return -1;
                                    }
                                    return -3; //timeout
                                }
                                cache[block] = 1;//adesso e' da considerarsi non modificato

                            }
                                break;

                            default:
                                return -1;
                                break;
                        }
                        break;
                    }

                    case DFS_ERRN_CODE:
                    {
                        MASTER_op_code = op_code;
                        switch (current_token++) {
                            case 0:
                            {
                                int converted = (int)strtol(token, (char **)NULL, 10);
                                if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                                    return -1;
                                int err_code = converted;
                                errno = err_code;
                                //DEBUG
                                dprintf(STDERR_FILENO, "ERROR: %s\n", msg);
                                //DEBUG
                                return -1;
                            }
                                break;

                            default:
                                return -1;
                                break;
                        }
                        break;
                    }

                    case DFS_INVD_CODE://invalidation message
                    {
                        errno = 0;
                        unsigned int converted = (unsigned int)strtoul(token, (char **)NULL, 10);
                        if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                            return -1;
                        unsigned int new_size = converted;
//                        printf("new size: %d\n", new_size);
                        if(file_resize(id, new_size)< 0) return -1;
                        id->size = new_size;
                        //printf("--CHACHE INVALIDATION--");
                        long i = 0;
                        for (i = 0; i < id->n_blocks; i++)
                        {
                            id->cache[i] = 0;
                        }
                        char buf[10]; // e se dopo c'è un heart beating?
                        if (peek(id->ctrl_fd, &buf, 10, &id->read_line_s) >= 3)
                        {
                            repeat = 1;
                            break; //break inner while
                        }
                    }



                    default: // DFS_OK_CODE
                        break;
                }
            }
        }
        if(op_code == DFS_CLSE_CODE)
        {
            //non deve succedere, la close parte dal client
        }
        else if (op_code == DFS_HTBT_CODE)//heart beating message
        {
            if(DEBUG_HTBT)
            {
                char message[] = "--BEAT--\n";
                write(STDOUT_FILENO, message, strlen(message));
            }
            if(nonblocking_send_query(id, DFS_HTBT_COMMAND, NULL, NULL, NULL) < 0)
                return -1;
            char buf[10];
            if (peek(id->ctrl_fd, &buf, 10, &id->read_line_s) > 3)
            {
                repeat = 1;
            }
        }
    }
    while(repeat);
    if (op_code != DFS_HTBT_CODE && op_code != DFS_INVD_CODE)
        return op_code;
    return MASTER_op_code;
}

int nonblocking_connect(MyDFSId *id, int fd, int timeout)
{
    int res, valopt;
    long arg;
    socklen_t lon;
    // Set non-blocking
    if( (arg = fcntl(fd, F_GETFL, NULL)) < 0) {
        perror("fcntl get");
        return -1;
    }
    arg |= O_NONBLOCK;
    if( fcntl(fd, F_SETFL, arg) < 0) {
        perror("fcntl set");
        return -1;
    }
    // Trying to connect with timeout
    res = connect(fd, (struct sockaddr *)&id->server, sizeof(id->server));
    if (res < 0) {
        if (errno == EINPROGRESS) {
            res = poll_write(fd);
            if (res < 0 && errno != EINTR) { //so che e' un solo socket, quindi se il risultato e' < 0 (0 e' timeout)
                perror("connect-select");
                return -1;
            }
            else if (res > 0) {
                // Socket selected for write
                //controllo se ci sono stati errori
                lon = sizeof(int);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) {
                    //                    fprintf(stderr, "Error in getsockopt() %d - %s\n", errno, strerror(errno));
                    return -1;
                }
                // Check the returned value...
                if (valopt) {
                    //                    fprintf(stderr, "Error in delayed connection() %d - %s\n", valopt, strerror(valopt));
                    return -1;
                }
            }
            else {
                //                fprintf(stderr, "Timeout in select() - Cancelling!\n");
                return -1;
            }

        }
        else {
            perror("connect");
            return -1;
        }
    }
    // Set to blocking mode again...
    if( (arg = fcntl(fd, F_GETFL, NULL)) < 0) {
        perror("fcntl get");
        return -1;
    }
    arg &= (~O_NONBLOCK);
    if( fcntl(fd, F_SETFL, arg) < 0) {
        perror("fcntl set");
        return -1;
    }
    return res;
}

ssize_t nonblocking_send_block(MyDFSId *id, long block_no)
{
    unsigned long size = (block_no == id->n_blocks-1) ? id->size -( (id->config.block_size)*(id->n_blocks-1)) : (id->config.block_size);
    if(nonblocking_send_query(id, DFS_WRTE_COMMAND, NULL, &block_no, &size) < 0)
    {
        perror("MyDFS - send_query");
        return -1;
    }
    if(nonblocking_receive_response(id) != DFS_OK_CODE)
    {
        perror("MyDFS - receive_response");
        return -1;
    }
    ssize_t r = writen_nb(id->data_fd, id->mapping+(id->config.block_size*block_no), size);
    if(r < 0)
    {
        if(r == -3)
            return -3; //timeout
        //perror("MyDFS - writen");
        return -1;
    }
    return r;
}

int nonblocking_send_query(MyDFSId *id, const char *com, const char *path, long *param1, unsigned long *param2)
{
    char msg[512];
    char param1s[20];
    char param2s[20];
    memset(msg, 0, 512);
    memset(param1s, 0, 20);
    memset(param2s, 0, 20);
    if (param1 != NULL)
        sprintf(param1s, "%ld", *param1);
    if (param2 != NULL)
        sprintf(param2s, "%lu", *param2);
    strcat(msg, com);
    if(path != NULL)
    {
        strcat(msg, DELIMITER);
        strcat(msg, path);
    }
    if(param1 != NULL)
    {
        strcat(msg, DELIMITER);
        strcat(msg, param1s);
    }
    if (param2 != NULL)
    {
        strcat(msg, DELIMITER);
        strcat(msg, param2s);
    }

    strcat(msg, "\n");
    unsigned long len = strlen(msg);
    ssize_t w = writen_nb(id->ctrl_fd, msg, len); //non invio il carattere termionatore
    if (w <= 0 || w < len)
    {
        if (w == -3)
            return -3; //timeout
        //        perror("send response");
        return -1;
    }
    return 0;

}

MyDFSId *mydfs_open(const char *host, const char *nomefile, const int modo, int *err)
{
    MyDFSId *id = malloc(sizeof(MyDFSId));
    if(id == NULL)
    {
        *err = errno;
        return id;
    }
    memset(id, 0, sizeof(MyDFSId));
    load_client_config(&id->config);
    unsigned long msize = id->config.block_size;
    id->semaphore = init_sem_anonym();
    char c_port[20];
    memset(c_port, 0, 20);
    sprintf(c_port, "%d", id->config.port);
    int res = mydfs_connect(host, c_port, id);
    if (res != 0)
    {
        *err = res;
        return NULL;
    }
    long l_modo = sys2dfs_mode(modo);
    id->mode = (int)l_modo;
    if(nonblocking_send_query(id, DFS_OPEN_COMMAND, nomefile, &l_modo, &msize) < 0)
    {
        perror("send_query");
        *err = -3;
        return NULL;
    }
    if(nonblocking_receive_response(id) != DFS_OPEN_CODE)
    {
        perror("receive_response");
        *err = -3;
        return NULL;
    }
    if ((l_modo & (MDFSO_RDWR | MDFSO_WRONLY | MDFSO_EXLOCK)) != 0 )
    {
        if (pthread_create(&id->thread, NULL, &(heart_beating_thread), id) != 0)
        {
            *err = errno;
            perror("MyDFS - thread init");
            return NULL;
        }
        //non devo detacharlo perche' devo vedere se muore..
    }
    else
        id->thread = NULL;
    return id;
}

int mydfs_close(MyDFSId *id)
{
    sem_t *sem = id->semaphore;
    semwait(sem);
    long i = 0;
    if(id->size > 0)
    {
        for (i = 0; i < id->n_blocks; i++)
        {
            if(id->cache[i] == 2)
            {
                if(nonblocking_send_block(id, i) < 0)
                {
                    perror("MyDFS - send_block");
                    semsignal(id->semaphore);
                    return -1;
                }
            }
        }
    }
    if(nonblocking_send_query(id, DFS_CLSE_COMMAND, NULL, NULL, NULL) < 0)
    {
        semsignal(sem);
        return -1;
    }
    if (nonblocking_receive_response(id) != DFS_CLSE_CODE)
    {
        return -1;
    }
    free(id->cache);
    if (munmap(id->mapping, id->allocated_blocks*id->config.block_size) < 0)
    {
        perror("MyDFS - munmap");
        semsignal(sem);
        return -1;
    }
    close_handled(&id->ctrl_fd); //TODO PRIMA O DOPO?
    close_handled(&id->data_fd);
    if (id->thread != NULL)
    {
        pthread_t thread = id->thread;
        id->thread = NULL;
        //su linux se interrompeva semwait non veniva ucciso
        //        if(pthread_cancel(id->thread) < 0)
        if (pthread_join(thread, NULL) < 0)
        {
            semsignal(sem);
            return -1;
        }
    }

    free(id);
    //smappo
    //termino il thread
    semsignal(sem);
    destroy_sem(sem);
    return 0;
}

int mydfs_read(MyDFSId *id, int pos, void *ptr, ssize_t size)
{
    if (!(id->mode & (MDFSO_RDONLY | MDFSO_RDWR | MDFSO_CREAT)))// && id->mode != 0)
        return -1;

    semwait(id->semaphore);
    //check for invalidation
    char buf[3];
    if (peek(id->ctrl_fd, &buf, 3, &id->read_line_s) >= 3)
    {
        //check for opcode;
        if (nonblocking_receive_response(id) < 0)
        {
            semsignal(id->semaphore);
            return -1;
        }
    }
    long read_ptr;
    switch (pos) {
        case SEEK_CUR:
            read_ptr = id->curr_pointer;
            break;
        case SEEK_END:
            read_ptr = id->size;
            break;
        case SEEK_SET:
            read_ptr = 0;
        default:
            break;
    }
    long block_start = read_ptr / id->config.block_size;
    long block_end = (read_ptr + size) / id->config.block_size;
    block_end = id->n_blocks > block_end? block_end : id->n_blocks-1; //minimo tra la fine e quello che voglio
    size = (size + read_ptr) > id->size ? (id->size - read_ptr) : size;
    long block = 0;
    for(block = block_start; block<= block_end; block++)
    {
        if (id->cache[block] > 0)
            continue;
        if(nonblocking_send_query(id, DFS_READ_COMMAND, NULL, &block, NULL) < 0)
        {
            semsignal(id->semaphore);
            return -1;
        }
        int res = 0;
        if((res = nonblocking_receive_response(id)) != DFS_SEND_CODE)
        {
            semsignal(id->semaphore);
            return res;
        }
    }
    id->curr_pointer += size;
    if (ptr != NULL)
        memcpy(ptr, (id->mapping+read_ptr), size);
    semsignal(id->semaphore);
    return size;
}

int mydfs_write(MyDFSId *id, const int pos, void *ptr, ssize_t size)
{
    if (!(id->mode & (MDFSO_WRONLY | MDFSO_RDWR)))
        return -1;

    semwait(id->semaphore);
    //check for invalidation
    char buf[3];
    if (peek(id->ctrl_fd, &buf, 3, &id->read_line_s) >= 3)
    {
        //check for opcode;
        if (nonblocking_receive_response(id) < 0)
        {
            semsignal(id->semaphore);
            return -1;
        }
    }
    //IL PUNTATORE CORRENTE AVRA IL VALORE DEL POS... SE ORA IMPOSTO END CUR SARA DA END IN POI..!
    ssize_t read_ptr;
    switch (pos) {
        case SEEK_CUR:
            read_ptr = id->curr_pointer;
            break;
        case SEEK_END:
            read_ptr = id->size;
            break;
        case SEEK_SET:
            read_ptr = 0;
        default:
            break;
    }
    long n_block_old = id->n_blocks; //cosi' se amplio il file in locale non altero la read..
    if((read_ptr + size) >= id->size)
    {
        if(file_resize(id, (read_ptr + size)) < 0)
        {
            semsignal(id->semaphore);
            return -1;
        }
    }
    long block_start = read_ptr / id->config.block_size;
    long block_end = (read_ptr + size) / id->config.block_size;
    long block = 0;
    for(block = block_start; block <= block_end; block++)
    {
        if(id->size > 0 && block < n_block_old && id->cache[block] == 0)
        {
            if(nonblocking_send_query(id, DFS_READ_COMMAND, NULL, &block, NULL) < 0)
            {
                semsignal(id->semaphore);
                return -1;
            }
            int res = 0;
            if((res = nonblocking_receive_response(id)) != DFS_SEND_CODE)
            {
                semsignal(id->semaphore);
                return -1;
            }
        }
        if (id->cache[block] > 0)
            id->cache[block] = 2;
        else
        {
            semsignal(id->semaphore);
            return -1; //impossibile
        }
    }
    memcpy((id->mapping+read_ptr), ptr, size);
    id->curr_pointer = read_ptr + size;
    if((read_ptr + size) >= id->size)
    {
        id->size+=size;
    }
    semsignal(id->semaphore);
    //ho gia' incrementanto ptr con la read
    return size;
}

ssize_t file_resize(MyDFSId *id, ssize_t size)
{
    long old_n_blocks = id->n_blocks;
    long new_n_blocks = (size / id->config.block_size) + 1;
    if(old_n_blocks == new_n_blocks)
    {
        return 1;
    }
    //remap di un blocco in piu'
    if(new_n_blocks > id->allocated_blocks)
    {
        long doubled = id->allocated_blocks;
        while (new_n_blocks > doubled)
        {
            doubled*=2;
        }
        void *new_map = mmap(NULL, doubled*id->config.block_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if(new_map == MAP_FAILED)
        {
            perror("MyDFS - mmap remap");
            return -1;
        }
        int i = 0;
        for(i = 0; i < old_n_blocks; i++)
        {
            if(id->cache[i] > 0)
                memcpy(new_map+(i*id->config.block_size), id->mapping+(i*id->config.block_size), id->config.block_size);
        }
        if(munmap(id->mapping, id->n_blocks == 0? id->config.block_size:id->n_blocks*id->config.block_size) < 0)
        {
            perror("MyDFS - mmap unmap");
            return -1;
        }
        id->mapping = new_map;
        void *cache = calloc(doubled, sizeof(char));
        if(cache == NULL)
            return -1;
        memset(cache+old_n_blocks, 1, doubled-old_n_blocks); //metto gli uni sui nuovi blocchi
        memcpy(cache, id->cache, old_n_blocks);
        free(id->cache);
        id->cache = (char *)cache;
        id->allocated_blocks = doubled;
    }
    //resize cache
    id->n_blocks = new_n_blocks;
    return 0;
}

void heart_beating_thread(MyDFSId *id)
{
    //se faccio operazioni onerose va bene... tanto quando chiamo receive response lo gestisco comunque!
    char buf[3];
    while(1)
    {
        if(id->thread == NULL)
            pthread_exit(NULL);
        if(!sem_trywait(id->semaphore))
        {
        if (peek(id->ctrl_fd, &buf, 3, &id->read_line_s) > 0 && nonblocking_receive_response(id) < 0)// (connessione chiusa, errore?)
        {
            if(pthread_detach(id->thread)<0)
                perror("thread_detach");//indico al padre che il thread e' terminato
            return;
        }
        semsignal(id->semaphore);
        }
        //unlock
        sleep(1); //dormi un secondo
    }
}
