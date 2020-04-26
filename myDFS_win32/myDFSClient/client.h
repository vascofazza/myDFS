//
//  client.h
//  myDFS
//
//  Created by Federico Scozzafava on 10/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "read_utilities.h"
#include "mydfs_commons.h"
#include "sem_utilities.h"
#include "configuration_manager.h"
#include "sem_utilities.h"

extern unsigned int DEBUG_HTBT;

typedef struct mydfs
{
    SOCKET ctrl_fd;
    SOCKET data_fd;
    struct sockaddr_in server;
    int fmode;
    char *cache;
    void *mapping;
    long curr_pointer;
    ssize_t size;
    unsigned long n_blocks;
    unsigned long allocated_blocks;
    read_struct read_line_s;
    clientconfig config;
    HANDLE thread;
    HANDLE semaphore;
} MyDFSId;

MyDFSId *mydfs_open(const char *host, const char *nomefile, const int modo, int *err);

int mydfs_close(MyDFSId *id);

int mydfs_read(MyDFSId *id, const int pos, void *ptr, ssize_t size);

int mydfs_write(MyDFSId *id, const int pos, void *ptr, ssize_t size);
