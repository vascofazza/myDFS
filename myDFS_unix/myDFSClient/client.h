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
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include "read_utilities.h"
#include "mydfs_commons.h"
#include <pthread.h>
#include "sem_utilities.h"
#include "configuration_manager.h"
#include <pthread.h>
#include "poll_helper.h"
#include "sem_utilities.h"

extern unsigned int DEBUG_HTBT;

typedef struct mydfs
{
    int ctrl_fd;
    int data_fd;
    struct sockaddr_in server;
    int mode;
    char *cache;
    void *mapping;
    long curr_pointer;
    ssize_t size;
    unsigned long n_blocks;
    unsigned long allocated_blocks;
    read_struct read_line_s;
    clientconfig config;
    pthread_t thread;
    sem_t *semaphore;
} MyDFSId;

MyDFSId *mydfs_open(const char *host, const char *nomefile, const int modo, int *err);

int mydfs_close(MyDFSId *id);

int mydfs_read(MyDFSId *id, const int pos, void *ptr, ssize_t size);

int mydfs_write(MyDFSId *id, const int pos, void *ptr, ssize_t size);
