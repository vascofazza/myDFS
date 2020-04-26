//
//  poll_helper.h
//  myDFS
//
//  Created by Federico Scozzafava on 13/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#define POLL_TIMEOUT 10000 //10 secs

#include <stdio.h>

#include <sys/types.h>

#include <poll.h>

#include <errno.h>

int poll_single(int fd, int mode);

int poll_read(int fd);

int poll_write(int fd);