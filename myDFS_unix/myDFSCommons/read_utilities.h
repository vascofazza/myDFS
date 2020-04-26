//
//  read_utilities.h
//  
//
//  Created by Federico Scozzafava on 16/02/15.
//
//

#ifndef ____read_utilities__
#define ____read_utilities__

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "poll_helper.h"

#define MAXLINE 20 //ridurre per evitare di memorizzare più linee?

typedef struct read_struct
{
    ssize_t read_cnt;
    char *read_ptr;
    char read_buf[MAXLINE];
} read_struct;

ssize_t readn_nb(int fd, void *vptr, size_t n);

ssize_t	writen_nb(int fd, const void *vptr, size_t n);

ssize_t readline_nb(int fd, void *vptr, size_t maxlen, read_struct *r);

ssize_t read_block(int fd, void *vptr, unsigned long block_no, size_t block_size);

ssize_t write_block(int fd, void *vptr, unsigned long block_no, size_t block_size, size_t size);

ssize_t getFileSize(int fd);

ssize_t peek(int fd, void *ptr, size_t size, read_struct *r);

#endif /* defined(____read_utilities__) */
