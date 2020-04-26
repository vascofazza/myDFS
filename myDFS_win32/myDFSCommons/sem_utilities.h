//
//  sem_utilities.h
//  myDFSServer
//
//  Created by Federico Scozzafava on 17/02/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#ifndef __myDFSServer__sem_utilities__
#define __myDFSServer__sem_utilities__

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <windows.h>

#define MAX_SEM_COUNT 1

HANDLE open_sem(int semID);

HANDLE init_sem(int semID); /* crea o prende un sem_set inizializzato a 1 */

HANDLE init_sem_anonym();

//int get_sem_set(key_t key);
//
void semwait(HANDLE semid);

BOOL semtrywait(HANDLE semid);

void semsignal(HANDLE semid);

void destroy_sem(HANDLE *semid);

#endif /* defined(__myDFSServer__sem_utilities__) */
