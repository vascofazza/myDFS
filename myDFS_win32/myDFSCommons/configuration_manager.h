//
//  configuration_manager.h
//  myDFSServer
//
//  Created by Federico Scozzafava on 24/03/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#ifndef __myDFSServer__configuration_manager__
#define __myDFSServer__configuration_manager__

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "read_utilities.h"

#define EQUAL "="

typedef struct sconfig
{
    unsigned int port;
    char path[256];
    unsigned int n_processes;
    unsigned int threaded;
    int daemon;
} serverconfig;

typedef struct cconfig
{
    unsigned int port;
    unsigned int block_size;
} clientconfig;

int load_server_config(serverconfig *config);

int load_client_config(clientconfig *config);

#endif /* defined(__myDFSServer__configuration_manager__) */
