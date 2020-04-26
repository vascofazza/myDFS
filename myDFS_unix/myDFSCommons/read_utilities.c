//
//  read_utilities.c
//
//
//  Created by Federico Scozzafava on 16/02/15.
//
//

// TODO REINTRODURRE GESTIONE INTERRUZIONE - no, e' gia' impostato nel main..? solo per il server?

#include "read_utilities.h"

int tmout = 20;

/* Read "n" bytes from a descriptor. */
ssize_t readn_nb(int fd, void *vptr, size_t n)
{
    size_t	nleft;
    ssize_t nread;
    char	*ptr;
    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        int res = poll_read(fd);
        if (res < 0 && errno != EINTR) { //so che e' un solo socket, quindi se il risultato e' < 0 (0 e' timeout)
            perror("readn_nb - select");
            return -1;
        }
        else if (res > 0) {
            // Socket selected for read
            if ( (nread = read(fd, ptr, nleft)) < 0)
                return(nread);		/* error, return < 0 */
            else if (nread == 0)
                break;				/* EOF */
            
            nleft -= nread;
            ptr   += nread;
        }
        else {
            //            fprintf(stderr, "Timeout in select() - Cancelling!\n");
            //timeout
            return -3;
        }
    }
    return(n - nleft);		/* return >= 0 */
}

/* Write "n" bytes to a descriptor. */
ssize_t	writen_nb(int fd, const void *vptr, size_t n)
{
    size_t		nleft;
    ssize_t     nwritten;
    const char	*ptr;
    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        int res = poll_write(fd);
        if (res < 0 && errno != EINTR) {
            perror("writen_nb - select");
            return -1;
        }
        else if(res < 0 && errno == EINTR)
            continue;
        else if (res > 0) {
            // Socket selected for write
            if ( (nwritten = write(fd, ptr, nleft)) <= 0)
            {return(nwritten);}		/* error  <0*/
            
            nleft -= nwritten;
            ptr   += nwritten;
        }
        else {
            fprintf(stderr, "Timeout in select() - Cancelling!\n");
            return -3;
        }
    }
    return(n);
}

ssize_t my_read_nb(int fd, char *ptr, read_struct *r)
{
    if (r->read_cnt <= 0) {
        int res = poll_read(fd);
        if (res < 0 && errno != EINTR) {
            perror("my_read_nb - select");
            return -1;
        }
        else if (res > 0) {
            // Socket selected for read
            if ( (r->read_cnt = read(fd, r->read_buf, MAXLINE)) < 0) {
                //if (errno == EINTR){ DOVREI GIA' GESTIRLO
                return (-1);
            } else if (r->read_cnt == 0){
                return (0);}
            r->read_ptr = r->read_buf;
        }
        else {
            //            fprintf(stderr, "Timeout in select() - Cancelling!\n");
            return -3;
        }
    }
    r->read_cnt--;
    *ptr = *(r->read_ptr++); //ritorno il carattere
    return (1);
    
}

//ritorna la stringa terminante con \n (escluso) + \0
ssize_t readline_nb(int fd, void *vptr, size_t maxlen, read_struct *r)
{
    ssize_t n, rc;
    char    c, *ptr;
    
    ptr = vptr;
    for (n = 1; n < maxlen; n++) {
        if ( (rc = my_read_nb(fd, &c, r)) == 1) {
            *ptr++ = c;
            if (c  == '\n'){
                break;   }       /* newline is stored, like fgets() */
        } else if (rc == 0) {
            *ptr = 0;
            return (n - 1);     /* EOF, n - 1 bytes were read */
        } else
            return (rc);        /* error, errno set by read() */
    }
    *(ptr - 1)  = 0;                  /* null terminate like fgets()  instead of \n*/
    return (n);
}

ssize_t read_block(int fd, void *vptr, unsigned long block_no, size_t block_size)
{
    off_t pos = block_no * block_size;
    if(lseek(fd, pos, SEEK_SET) < 0)
        return -1;
    return read(fd, vptr, block_size);
}

ssize_t write_block(int fd, void *vptr, unsigned long block_no, size_t block_size, size_t size)
{
    off_t pos = block_no * block_size;
    if(lseek(fd, pos, SEEK_SET) < 0)
        return -1;
    return writen_nb(fd, vptr, size);
}

ssize_t getFileSize(int fd)
{
    struct stat st;
    fstat(fd, &st);
    return st.st_size;
}

ssize_t peek(int fd, void *ptr, size_t size, read_struct *r)
{
    return recv(fd, ptr, size, MSG_PEEK | MSG_DONTWAIT) + r->read_cnt;
}
