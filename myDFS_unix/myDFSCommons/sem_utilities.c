//
//  sem_utilities.c
//  myDFSServer
//
//  Created by Federico Scozzafava on 17/02/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "sem_utilities.h"
#include <unistd.h>

sem_t *open_sem(int semID)
{
    char buf[7];
    sprintf(buf, "sem%03d", semID);
    buf[6] = 0;
    sem_t *sem = sem_open(buf, O_EXCL);
    if(sem == SEM_FAILED)
    {
        perror("MyDFS - sem_open");
        exit(EXIT_FAILURE);
    }
    return sem;
}

sem_t *init_sem(int semID)
{
    char buf[7];
    sprintf(buf, "sem%03d", semID);
    buf[6] = 0;
    sem_t *sem = sem_open(buf, O_CREAT | O_EXCL, 0644,1);
    if(sem == SEM_FAILED)
    {
        perror("MyDFS - sem_init");
        exit(EXIT_FAILURE);
    }
    return sem;
}

sem_t *init_sem_anonym()
{
    char buf[L_tmpnam] = "mydfsXXXXXX";
    mktemp(buf);
    sem_t *sem = sem_open(buf, O_CREAT, 0644,1);
    if(sem == SEM_FAILED)
    {
        perror("sem_init_anonym");
        exit(EXIT_FAILURE);
    }
    if(sem_unlink(buf)<0)
    {
        perror("sem_unlink_anonym");
        exit(EXIT_FAILURE);
    }
    return sem;
}

void semwait(sem_t *semid)
{
    while(1)
    {
        if(sem_wait(semid) < 0)
        {
            if(errno == EINTR) //runconditions
            {
                errno = 0;
                continue;
            }
            perror("MyDFS - semwait");
            exit(EXIT_FAILURE);
        }
        break;
    }
}

void semsignal(sem_t *semid)
{
    while(1)
    {
        if(sem_post(semid) < 0)
        {
            if(errno == EINTR)
                continue;
            perror("MyDFS - semsignal");
            exit(EXIT_FAILURE);
        }
        break;
    }
}

void unlink_sem(int id)
{
    char buf[7];
    sprintf(buf, "sem%03d", id);
    if(sem_unlink(buf) <0)
    {
        //consentito
        //perror("unlink");
        //exit(-1);
    }
}

void destroy_sem(sem_t *semid)
{
    int res = sem_close(semid);
    if(res <0)
    {
        perror("MyDFS - sem_close");
        exit(EXIT_FAILURE);
    }
}
