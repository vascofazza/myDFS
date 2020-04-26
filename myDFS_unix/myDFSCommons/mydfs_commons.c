//
//  mydfs_commons.c
//  myDFS
//
//  Created by Federico Scozzafava on 31/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "mydfs_commons.h"

void close_handled(int *fd)
{
    if(*fd < 0)
        return;
    if(close(*fd) < 0)
    {
        perror("MyDFS - close handled");
        exit(EXIT_FAILURE);
    }
    *fd = -1;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int sys2dfs_mode(int mode)
{
    int converted = 0;
    if (mode & O_CREAT)
        converted |= MDFSO_CREAT;
    if (mode & O_EXCL)
        converted |= MDFSO_EXCL;
    if (mode & O_EXLOCK)
        converted |= MDFSO_EXLOCK;
    if (mode & O_RDONLY || (O_RDONLY == 0 && mode == 0))
        converted |= MDFSO_RDONLY;
    if (mode & O_RDWR)
        converted |= MDFSO_RDWR;
    if (mode & O_TRUNC)
        converted |= MDFSO_TRUNC;
    if (mode & O_WRONLY)
        converted |= MDFSO_WRONLY;
    return converted;
}

int dfs2sys_mode(int mode)
{
    int converted = 0;
    if (mode & MDFSO_CREAT)
        converted |= O_CREAT;
    if (mode & MDFSO_EXCL)
        converted |= O_EXCL;
    if (mode & MDFSO_EXLOCK)
        converted |= O_EXLOCK;
    if (mode & MDFSO_RDONLY)
        converted |= O_RDONLY;
    if (mode & MDFSO_RDWR)
        converted |= O_RDWR;
    if (mode & MDFSO_TRUNC)
        converted |= O_TRUNC;
    if (mode & MDFSO_WRONLY)
        converted |= O_WRONLY;
    return converted;
}