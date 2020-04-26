//
//  poll_helper.c
//  myDFS
//
//  Created by Federico Scozzafava on 13/08/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "poll_helper.h"

int poll_single(int fd, int mode)
{
    struct pollfd poll_list[1];
    int retval;
    poll_list[0].fd = fd;
    poll_list[0].events = mode;
    while(1)
    {
        retval = poll(poll_list,(nfds_t) 1,POLL_TIMEOUT);
        if(retval < 0 && errno == EINTR)
        {
            continue;
        }
        else if (retval < 0)
        {
            perror("MyDFS - Error while polling: %s\n");
            return -1;
        }
        return retval;
    }
}

int poll_write(int fd)
{
    return poll_single(fd, POLLOUT);
}

int poll_read(int fd)
{
    return poll_single(fd, POLLIN);
}