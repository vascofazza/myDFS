//
//  poll_helper.h
//  myDFS
//
//  Created by Federico Scozzafava on 13/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#define POLL_TIMEOUT 15 //15 secs

#include <stdio.h>

#include <Winsock2.h>

#include <errno.h>

enum mode {READ, WRITE};

int poll_single(int fd, enum mode mode);

int poll_read(int fd);

int poll_write(int fd);