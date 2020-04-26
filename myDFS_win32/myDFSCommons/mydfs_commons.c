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
        my_perror("MyDFS - close handled");
        exit(EXIT_FAILURE);
    }
    *fd = -1;
}

void close_handle(HANDLE *handle)
{
    if(*handle == INVALID_HANDLE_VALUE)
        return;
    if(!CloseHandle(*handle))
    {
        my_perror("MyDFS - close handled");
        exit(EXIT_FAILURE);
    }
    *handle = INVALID_HANDLE_VALUE;
}

void close_socket(SOCKET *sock)
{
    if(*sock == INVALID_SOCKET)
    {
        return;
    }
//    if(shutdown(*sock, SD_BOTH) == SOCKET_ERROR)
//    {
//        fprintf(stderr, "shutdown socket: %s\r\n", GetErrorMessage(WSAGetLastError()));
//        exit(EXIT_FAILURE);
//    }
    if (closesocket(*sock) == SOCKET_ERROR){
        fprintf(stderr, "close socket: %s\r\n", GetErrorMessage(WSAGetLastError()));
        exit(EXIT_FAILURE);
    }
    *sock = INVALID_SOCKET;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int inet_pton(int af, const char *src, void *dst)
{
    struct sockaddr_storage ss;
    int size = sizeof(ss);
    char src_copy[INET6_ADDRSTRLEN+1];

    memset(&ss, 0, sizeof(ss));
    /* stupid non-const API */
    strncpy (src_copy, src, INET6_ADDRSTRLEN+1);
    src_copy[INET6_ADDRSTRLEN] = 0;

    if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0) {
        switch(af) {
            case AF_INET:
                *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
                return 1;
            case AF_INET6:
                *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
                return 1;
        }
    }
    return 0;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    struct sockaddr_storage ss;
    unsigned long s = size;

    memset(&ss, 0, sizeof(ss));
    ss.ss_family = af;

    //((struct sockaddr_in *) &ss)->sin_addr = get_in_addr(src);

    switch(af) {
        case AF_INET:
            ((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
            break;
        case AF_INET6:
            ((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
            break;
        default:
            return NULL;
    }
    /* cannot direclty use &size because of strict aliasing rules */
    return (WSAAddressToString((struct sockaddr *)&ss, sizeof(ss), NULL, dst, &s) == 0)?
           dst : NULL;
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

void syslog(HANDLE logger, WORD type, const char *message, ...)
{
    char buf[1024];
    memset(buf, 0, 1024);
    if (strlen(message) >= 1023)
        return;
    LPCSTR *str[] = {&buf};
    va_list arg;
    va_start(arg, message);
    vsprintf(buf, message, arg);
    va_end(arg);
    if (ReportEvent(logger, type, 0, 0, NULL, 1, 0, str, NULL) == 0)
    {
        my_perror("Report event");
        exit(EXIT_FAILURE);
    }
    switch(type) {
        case EVENTLOG_ERROR_TYPE:
            printf("MyDFS Server\tERRN: %s\n", &buf);
            break;
        case EVENTLOG_INFORMATION_TYPE:
            printf("MyDFS Server\tINFO: %s\n", &buf);
            break;
        default:
            printf("MyDFS Server\tMESG: %s\n", &buf);
    }
}

void kill(unsigned int process_id)
{
    char buf[7];
    sprintf(buf, "evn%03d", process_id);
    buf[6] = 0;
    HANDLE event = OpenEvent(EVENT_ALL_ACCESS, FALSE, buf);
    if (event == NULL)
    {
        my_perror("error while opening the event");
        exit(EXIT_FAILURE);
    }
    if(!SetEvent(event))
    {
        my_perror("error while pulsing event");
    }
    close_handle(&event);
}

HANDLE getEvent(unsigned int process_id)
{
    char buf[7];
    sprintf(buf, "evn%03d", process_id);
    buf[6] = 0;
    HANDLE evnt = CreateEvent(NULL, TRUE, FALSE, buf); //evento manual-reset. Pulse event lo resetta comunque... vabe..!
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        my_perror("getEvent: event already exists");
        exit(EXIT_FAILURE);
    }
    return evnt;
}

LPTSTR GetErrorMessage(DWORD err)
{
    LPTSTR lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        &lpMsgBuf,
        0, NULL );
    if(strlen(lpMsgBuf) > 2); //tolgo \r\n
        lpMsgBuf[strlen(lpMsgBuf)-2] = '\0';
    return lpMsgBuf;
}

BOOL SetNonBlockingSocket(SOCKET fd, BOOL nonblocking)
{
    if (ioctlsocket(fd, FIONBIO, &nonblocking) == SOCKET_ERROR)
    {
        return FALSE;
    }
    return TRUE;
}

void my_perror(const char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, GetErrorMessage(GetLastError()));
}