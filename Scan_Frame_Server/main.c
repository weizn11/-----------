#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "concurrent/async.h"
#include "task.h"



int main(int argc,char *argv[])
{
    int i;
    int pid;
    pthread_t threadID;
/*
    pid=fork();
    if(pid<0)
        printf("fork() error\n");
    else if(pid>0)
    {
        printf("father process exit\n");
        exit(0);
    }
    setsid();
    */
    printf("v1.0.0\n");
    pthread_create(&threadID,NULL,distribute_task,NULL);
    listen_client();

    return 0;
}
