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
#include <semaphore.h>

sem_t *open_sem(int semID);

sem_t *init_sem(int semID); /* crea o prende un sem_set inizializzato a 1 */

sem_t *init_sem_anonym();

//int get_sem_set(key_t key);
//
void semwait(sem_t *semid);
//
void semsignal(sem_t *semid);

void destroy_sem(sem_t *semid);

void unlink_sem(int id);

#endif /* defined(__myDFSServer__sem_utilities__) */
