//
// Created by Federico Scozzafava on 06/08/15.
//

//TEST1
/*
 Apro un file in ogni modalita' permessa e faccio test
 */

//TEST2
/*
 Faccio la copia di un file e verifico l'uguaglianza
 */

//TEST3
/*
 Multithread mode stress test
 */

//TEST4
/*
 Invalidation test
 */

#include <stdio.h>
#include "client.h"

char *host; //

int equal(const char *fname1, const char *fname2) {
    MyDFSId *id;
    int err = 0;
    id = mydfs_open(host, fname1, O_RDONLY, &err);
    if(id == NULL)
    {
        perror("open");
        return -1;
    }
    
    MyDFSId *id1;
    id1 = mydfs_open(host, fname2, O_RDONLY, &err);
    if(id1 == NULL)
    {
        perror("open");
        return -1;
    }
    
    char ch1[1024];
    char ch2[1024];
    int r = 0;
    int r1 = 0;
    while(1)
    {
        r = mydfs_read(id, SEEK_CUR, &ch1, 1024);
        r1 = mydfs_read(id1, SEEK_CUR, &ch2, 1024);
        if (r == r1 && r != 0 && memcmp(ch1, ch2, r) == 0) //if less than 1024 file is shorter
            continue;
        else if (r == r1 && r == 0)
            break;
        return -1;
    }
    if (mydfs_close(id) < 0 || mydfs_close(id1) < 0)
    {
        perror("CLOSE");
        return -1;
    }
    return 0;
}

void TEST1()
{
    int res = 1;
    
    MyDFSId *id;
    int err = 0;
    id = mydfs_open(host, "Desktop/dfs_test/file", O_RDONLY, &err);
    if(id == NULL)
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    char r[30];
    res &= mydfs_write(id, SEEK_CUR, r, 30) < 0;
    res &= mydfs_read(id, SEEK_CUR, r, 30) > 0;
    res &= mydfs_close(id)+1;
    
    id = mydfs_open(host, "Desktop/dfs_test/file", O_WRONLY, &err);
    if(id == NULL)
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    res &= mydfs_read(id, SEEK_CUR, r, 30) < 0;
    
    MyDFSId *id2;
    id2 = mydfs_open(host, "Desktop/dfs_test/file", O_RDONLY, &err);
    if(id2 == NULL)
    {
        res &= 1;
    }
    else
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    res &= mydfs_close(id)+1;
    
    id = mydfs_open(host, "Desktop/dfs_test/file", O_EXLOCK | O_RDONLY, &err);
    if(id == NULL)
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    res &= mydfs_read(id, SEEK_CUR, r, 30) < 0;
    
    id2 = mydfs_open(host, "Desktop/dfs_test/file", O_RDONLY, &err);
    if(id2 == NULL)
    {
        res &=1;
    }
    else
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    res &= mydfs_close(id)+1;
    
    id2 = mydfs_open(host, "Desktop/dfs_test/file", O_RDONLY, &err);
    if(id2 == NULL)
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    else
    {
        res &= 1;
    }
    res &= mydfs_close(id2)+1;
    
    id2 = mydfs_open(host, "Desktop/dfs_test/file", O_EXCL | O_CREAT, &err);
    if(id2 == NULL)
    {
        res &=1;
    }
    else
    {
        printf("TEST01 ERR: %d\n", err);
        res = 0;
        return;
    }
    
    if (res)
        printf("TEST01 OK\n");
    
}

void TEST2()
{
    char file1[] = "Desktop/dfs_test/file";
    char file2[] = "Desktop/dfs_test/file.mydfs";
    MyDFSId *id;
    int err = 0;
    id = mydfs_open(host, file1, O_RDONLY, &err);
    if(id == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    MyDFSId *id1;
    int err1 = 0;
    id1 = mydfs_open(host, file2, O_CREAT | O_WRONLY | O_TRUNC, &err1);
    if(id1 == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    char buf[1024];
    memset(buf, 0, 1024);
    
    int r =0 ;
    while ((r= mydfs_read(id, SEEK_CUR, buf, 1024)) > 0)
    {
        mydfs_write(id1, SEEK_CUR, buf, r);
    }
    if(r<0)
    {
        perror("mydfs_read");
        exit(EXIT_FAILURE);
    }
    
    if(mydfs_close(id) < 0)
    {
        perror("mydfs_close");
        exit(EXIT_FAILURE);
    }
    if(mydfs_close(id1) < 0)
    {
        perror("mydfs_close");
        exit(EXIT_FAILURE);
    }
    
    if (equal(file1, file2) == 0)
        printf("TEST02 OK\n");
    else
        printf("ERROR: files differ\n");
}

int myfunc()
{
    int i = 1000;
    while(i-- > 0)
    {
        printf("THREAD %d\n", i);
        MyDFSId *id;
        int err = 0;
        id = mydfs_open(host, "Desktop/dfs_test/invalidation.txt", O_RDONLY, &err);
        if(id == NULL)
        {
            perror("open");
            return -1;
            //exit(EXIT_FAILURE);
        }
        char buf[200];
        memset(buf, 0, 200);
        
        int r =0 ;
        while ((r= mydfs_read(id, SEEK_CUR, buf, 200)) > 0)
        {
        }
        if(r<0)
        {
            perror("mydfs_read");
            return -1;
        }
        if(mydfs_close(id) < 0)
        {
            perror("mydfs_close");
            return -1;
        }
        //usleep(10);
    }
    return (0);
}

void TEST3()
{
    HANDLE thread1;
    HANDLE thread2;
    HANDLE thread3;
    PDWORD returnvalue;
    thread1 = CreateThread(NULL, 0, &myfunc, NULL, 0, NULL);
    if (thread1 == NULL)
    {
        perror("thread init");
        return;
    }
    thread2 = CreateThread(NULL, 0, &myfunc, NULL, 0, NULL);
    if (thread2 == NULL)
    {
        perror("thread init");
        return;
    }
    thread3 = CreateThread(NULL, 0, &myfunc, NULL, 0, NULL);
    if (thread3 == NULL)
    {
        perror("thread init");
        return;
    }
    if(WaitForSingleObject(thread1, INFINITE) == WAIT_OBJECT_0 && GetExitCodeThread(thread1, &returnvalue) && returnvalue >= 0)
    {;}
    else
    {
        perror("thread exec");
        return;
    }
    close_handle(&thread1);
    if(WaitForSingleObject(thread2, INFINITE) == WAIT_OBJECT_0 && GetExitCodeThread(thread2, &returnvalue) && returnvalue >= 0)
    {;}
    else
    {
        perror("thread exec");
        return;
    }
    close_handle(&thread2);
    if(WaitForSingleObject(thread3, INFINITE) == WAIT_OBJECT_0 && GetExitCodeThread(thread3, &returnvalue) && returnvalue >= 0)
    {;}
    else
    {
        perror("thread exec");
        return;
    }
    close_handle(&thread3);
    printf("TEST03 OK\n");
}

void beating()
{
    DEBUG_HTBT = 1;
    MyDFSId *id;
    MyDFSId *id1;
    char path[100];
//    unsigned seed = 0;
    sprintf(path, "Desktop/dfs_test/dfs_prova.mydfsWrite%d.pdf", rand());
    int err = 0;
    id = mydfs_open(host, path, O_CREAT | O_RDWR | O_TRUNC, &err); //ERRORE CON WRITE_ONLY
    if(id == NULL)
    {
        perror("open");
        //return -1;
        exit(EXIT_FAILURE);
        //break;
    }
    sprintf(path, "Desktop/dfs_test/prova.mydfsWrite%d.pdf", rand());
    int err1 = 0;
    id1 = mydfs_open(host, path, O_CREAT | O_RDWR | O_TRUNC, &err1); //ERRORE CON WRITE_ONLY
    if(id1 == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    sleep(30);
    mydfs_close(id);
    mydfs_close(id1);
    DEBUG_HTBT = 0;
}

void invalidation()
{
    char mat[200] = "test invalidation";
    
    MyDFSId *id_test;
    int err_test = 0;
    id_test = mydfs_open(host, "Desktop/dfs_test/invalidation.txt", O_CREAT | O_WRONLY | O_TRUNC, &err_test);
    if(id_test == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if(mydfs_write(id_test, SEEK_END, mat, strlen(mat)) < 0 )
    {
        perror("write");
        exit(EXIT_FAILURE);
    }
    if(mydfs_close(id_test) < 0)
    {
        perror("mydfs_close");
        exit(EXIT_FAILURE);
    }
    
    
    MyDFSId *id;
    int err = 0;
    id = mydfs_open(host, "Desktop/dfs_test/invalidation.txt", O_RDONLY|O_CREAT, &err);
    if(id == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    char buf[131073];
    memset(buf, 0, 131073);
    
    int r =0 ;
    r= mydfs_read(id, 0, buf, 131073);
    if (r< 0)
    {
        perror("read");
        return;
    }
    if (strcmp(mat, buf) != 0)
    {
        return;
    }
    strcat(mat, "CIAO");
    MyDFSId *id1;
    int err1 = 0;
    id1 = mydfs_open(host, "Desktop/dfs_test/invalidation.txt", O_CREAT | O_WRONLY, &err1);
    if(id1 == NULL)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    if(mydfs_write(id1, SEEK_END, "CIAO", 4) < 0)
    {
        perror("write");
        exit(EXIT_FAILURE);
    }
    if(mydfs_close(id1) < 0)
    {
        perror("mydfs_close");
        exit(EXIT_FAILURE);
    }
    sleep(2);
    r= mydfs_read(id, SEEK_SET, buf, 131073);

    if(r<0)
    {
        perror("mydfs_read");
        exit(EXIT_FAILURE);
    }
    if(mydfs_close(id) < 0)
    {
        perror("mydfs_close");
        exit(EXIT_FAILURE);
    }
    if (strcmp(mat, buf) != 0)
    {
        return;
    }
    printf("INVALIDATION TEST OK\n");
}

int main(int argc, char **argv) {
    if (argc > 1)
        host = argv[1];
    else
        host = "localhost";
    TEST3();
    sleep(5);
    invalidation();
    sleep(5);
    TEST1();
    sleep(5);
    TEST2();
    sleep(5);
    beating();
}
