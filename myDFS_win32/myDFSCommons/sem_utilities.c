//
//  sem_utilities.c
//  myDFSServer
//
//  Created by Federico Scozzafava on 17/02/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "sem_utilities.h"

HANDLE open_sem(int semID)
{
    char buf[7];
    sprintf(buf, "sem%03d", semID);
    buf[6] = 0;
    HANDLE sem = OpenSemaphore(SEMAPHORE_ALL_ACCESS, TRUE, buf);
    if(sem == NULL)
    {
        perror("MyDFS - sem_open");
        exit(EXIT_FAILURE);
    }
    return sem;
}

HANDLE init_sem(int semID)
{
    char buf[7];
    sprintf(buf, "sem%03d", semID);
    buf[6] = 0;
    HANDLE sem = CreateSemaphore(NULL, MAX_SEM_COUNT, MAX_SEM_COUNT, buf);
    if(sem == NULL) //semaforo gia' esiste
    {
        perror("MyDFS - sem_init");
        exit(EXIT_FAILURE);
    }
    return sem;
}

HANDLE init_sem_anonym()
{
//    char buf[L_tmpnam] = "mydfsXXXXXX";
//    mktemp(buf);
    HANDLE sem = CreateSemaphore(NULL, MAX_SEM_COUNT, MAX_SEM_COUNT, NULL); //unnamed semaphore
    if(sem == NULL) {
        perror("sem_init_anonym");
        exit(EXIT_FAILURE);
    }
    return sem;
}

void semwait(HANDLE semid)
{
    DWORD dwWaitResult = WaitForSingleObject(semid, INFINITE);
    switch (dwWaitResult) {
        case WAIT_OBJECT_0:
            break;
        case WAIT_FAILED: {
            perror("MyDFS - semwait");
            exit(EXIT_FAILURE);
        }
    }
}

void semsignal(HANDLE semid)
{
    if (!ReleaseSemaphore(semid, 1, NULL))
    {
        perror("MyDFS - semsignal");
        exit(EXIT_FAILURE);
    }
}

void destroy_sem(HANDLE *semid)
{
    if(*semid == INVALID_HANDLE_VALUE)
        return;
    BOOL res = CloseHandle(*semid);
    if(!res)
    {
        perror("MyDFS - sem_close");
        exit(EXIT_FAILURE);
    }
    *semid = INVALID_HANDLE_VALUE;
}

BOOL semtrywait(HANDLE semid)
{
    DWORD dwWaitResult = WaitForSingleObject(semid, 0);
    switch (dwWaitResult) {
        case WAIT_OBJECT_0:
            return TRUE;
        case WAIT_TIMEOUT:
            return FALSE;
        case WAIT_FAILED: {
            perror("MyDFS - semwait");
            exit(EXIT_FAILURE);
        }
    }
}