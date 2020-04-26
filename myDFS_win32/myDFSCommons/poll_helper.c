//
//  poll_helper.c
//  myDFS
//
//  Created by Federico Scozzafava on 13/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "poll_helper.h"

int poll_single(int fd, enum mode mode)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval timeout = {POLL_TIMEOUT, 0};
    int retval = 0;
    while(1)
    {
        switch(mode)
        {
            case READ: {
                retval = select(0, &set, NULL, NULL, &timeout);
                break;
            }
            case WRITE: {
                retval = select(0, NULL, &set, NULL, &timeout);
            }
        }
        if(retval < 0 && errno == EINTR)
        {
            continue;
        }
        else if (retval == SOCKET_ERROR)
        {
            perror("MyDFS - Error while polling: %s\n");
            return -1;
        }
        return retval;
    }
}

int poll_write(int fd)
{
    return poll_single(fd, WRITE);
}

int poll_read(int fd)
{
    return poll_single(fd, READ);
}