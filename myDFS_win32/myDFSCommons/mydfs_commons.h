//
//  mydfs_commons.h
//  myDFSServer
//
//  Created by Federico Scozzafava on 07/03/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#ifndef __myDFSServer__mydfs_commons__
#define __myDFSServer__mydfs_commons__

#define _WIN32_WINNT 0x0501

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fcntl.h>

#define DELIMITER "\t"  //query delimiter

#define BEAT_SLEEP 10

#define DFS_OK_CODE 800

#define DFS_OPEN_CODE 801

#define DFS_SEND_CODE 802

#define DFS_RECV_CODE 803

#define DFS_CLSE_CODE 804

#define DFS_INVD_CODE 805

#define DFS_HTBT_CODE 806

#define DFS_ERRN_CODE 888 //in questo caso l'argomento e' il codice errore

#define DFS_OPEN_COMMAND "OPEN"

#define DFS_READ_COMMAND "READ"

#define DFS_WRTE_COMMAND "WRTE"

#define DFS_CLSE_COMMAND "CLSE"

#define DFS_HTBT_COMMAND "HTBT"

#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif

#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif

#ifndef O_RDWR
#define O_RDWR 0x0002
#endif

#ifndef O_CREAT
#define O_CREAT 0x0200
#endif

#ifndef O_TRUNC
#define O_TRUNC 0x0400
#endif

#ifndef O_EXCL
#define O_EXCL 0x0800
#endif

#ifndef O_EXLOCK
#define O_EXLOCK 0x0020
#endif

#define MDFSO_RDONLY 0x0004

#define MDFSO_WRONLY 0x0001

#define MDFSO_RDWR 0x0002

#define MDFSO_CREAT 0x0200

#define MDFSO_TRUNC 0x0400

#define MDFSO_EXCL 0x0800

#define MDFSO_EXLOCK 0x0020

void close_handle(HANDLE *handle);

void close_handled(int *fd);

void close_socket(SOCKET *sock);

void *get_in_addr(struct sockaddr *sa);

int sys2dfs_mode(int mode);

int dfs2sys_mode(int mode);

void syslog(HANDLE logger, WORD type, const char *message, ...);

int inet_pton(int af, const char *src, void *dst);

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

void kill(unsigned int process_id);

HANDLE getEvent(unsigned int process_id);

LPTSTR GetErrorMessage(DWORD err);

BOOL SetNonBlockingSocket(SOCKET fd, BOOL blocking);

void my_perror(const char *msg);

#endif /* defined(__myDFSServer__mydfs_commons__) */
