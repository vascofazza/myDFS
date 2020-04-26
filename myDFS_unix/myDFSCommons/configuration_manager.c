//
//  configuration_manager.c
//  myDFSServer
//
//  Created by Federico Scozzafava on 24/03/15.
//  Copyright (c) 2015 Federico Scozzafava. All rights reserved.
//

#include "configuration_manager.h"

unsigned int DEFAULT_DFS_PORT = 6020;
unsigned int DEFAULT_DFS_SIZE = 65536;
char DEFAULT_DFS_PATH[] = ".";

int load_server_config(serverconfig *config)
{
    config->port = DEFAULT_DFS_PORT;
    strcpy(config->path, DEFAULT_DFS_PATH);
    config->n_processes = 2;
    config->threaded = 0;
    config->daemon=1;
    int fd = open("mydfs_server.config", O_RDONLY, 0644);
    if(fd < 0)
    {
        if(errno != ENOENT)
        {
            perror("MyDFS - load_server_config");
            return -1;
        }
        else
        {
            printf("MyDFS - Starting with default configuration file");
            return 1;
        }
    }
    
    char string[512];
    memset(string, 0, 512);
    read_struct read_line_struct;
    memset(&read_line_struct, 0, sizeof(read_struct));
    ssize_t res = 0;
    while((res = readline_nb(fd, string, 512, &read_line_struct)) > 0)
    {
        char *string_ptr = string;
        char *token;
        char *saveptr;
        token = strtok_r(string_ptr, EQUAL, &saveptr);
        string_ptr = NULL; //per chiamate successive
        if(token == NULL)
            break;
        if(strcmp(token, "port") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                {
                    perror("conversion error");
                    return -1;
                }
                config->port = converted;
            }
        }
        else if(strcmp(token, "path") == 0)
        {
            char *path = strtok_r(string_ptr, EQUAL, &saveptr);
            if(path != NULL)
                strcpy(config->path, path);
        }
        else if(strcmp(token, "n_processes") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                if(p != NULL)
                {
                    unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        perror("conversion error");
                        return -1;
                    }
                    config->n_processes = converted;
                }
            }
        }
        else if(strcmp(token, "threaded") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                if(p != NULL)
                {
                    unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        perror("conversion error");
                        return -1;
                    }
                    config->threaded = converted;
                }
            }
        }
        else if(strcmp(token, "daemon") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                if(p != NULL)
                {
                    unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        perror("conversion error");
                        return -1;
                    }
                    config->daemon = converted;
                }
            }
        }
        
        memset(string, 0, 512);
    }
    int cls = close(fd);
    if(cls < 0)
    {
        perror("MyDFS - close configuration file");
        return -1;
    }
    if (res < 0)
    {
        perror("MyDFS - read config file");
        return -1;
    }
    return 0;
}

int load_client_config(clientconfig *config)
{
    config->port = DEFAULT_DFS_PORT;
    config->block_size = DEFAULT_DFS_SIZE;
    int fd = open("mydfs_client.config", O_RDONLY, 0644);
    if(fd < 0)
    {
        if(errno != ENOENT)
        {
            perror("MyDFS - load_server_config");
            //return -1; carico config di default
        }
        else
        {
            perror("MyDFS - open config file");
            return 1;
        }
    }
    
    char string[512];
    memset(string, 0, 512);
    read_struct read_line_struct;
    memset(&read_line_struct, 0, sizeof(read_struct));
    ssize_t res = 0;
    while((res = readline_nb(fd, string, 512, &read_line_struct)) > 0)
    {
        char *string_ptr = string;
        char *token;
        char *saveptr;
        token = strtok_r(string_ptr, EQUAL, &saveptr);
        string_ptr = NULL; //per chiamate successive
        if(token == NULL)
            break;
        if(strcmp(token, "port") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                if(p != NULL)
                {
                    unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        perror("conversion error");
                        return -1;
                    }
                    config->port = converted;
                }
            }
        }
        else if(strcmp(token, "block_size") == 0)
        {
            char *p = strtok_r(string_ptr, EQUAL, &saveptr);
            if(p != NULL)
            {
                if(p != NULL)
                {
                    unsigned int converted = (unsigned)strtoul(p, (char **)NULL, 10);
                    if(converted == 0 && (errno == EINVAL || errno == ERANGE))
                    {
                        perror("conversion error");
                        return -1;
                    }
                    config->block_size = converted;
                }
            }
        }
        
        memset(string, 0, 512);
    }
    int cls = close(fd);
    if(cls < 0)
    {
        perror("MyDFS - close configuration file");
        return -1;
    }
    if (res < 0)
    {
        perror("MyDFS - read config file");
        return -1;
    }
    return 0;
}