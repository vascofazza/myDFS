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
#include <string.h>
#include "poll_helper.h"
#include "mydfs_commons.h"

#define MAXLINE 20 //ridurre per evitare di memorizzare più linee?

typedef struct _read_struct
{
    ssize_t read_cnt;
    char *read_ptr;
    char read_buf[MAXLINE];
} read_struct;

ssize_t readn_nb(SOCKET fd, void *vptr, size_t n);

ssize_t	writen_nb(SOCKET fd, const void *vptr, size_t n);

ssize_t readline_nb(SOCKET fd, void *vptr, size_t maxlen, read_struct *r);

ssize_t readline(int fd, void *vptr, size_t maxlen, read_struct *r);

ssize_t read_block(int fd, void *vptr, unsigned long block_no, size_t block_size);

ssize_t write_block(int fd, void *vptr, unsigned long block_no, size_t block_size, size_t size);

ssize_t getFileSize(int fd);

ssize_t peek(SOCKET fd, void *ptr, size_t size, read_struct *r);

char* strtok_r(char *str, const char *delim, char **nextp);

#endif /* defined(____read_utilities__) */
