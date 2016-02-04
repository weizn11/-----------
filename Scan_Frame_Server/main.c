#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>

#include "concurrent/async.h"
#include "task.h"

int main(int argc,char *argv[])
{
    int pid;
    pthread_t threadID;
    FILE *redirect=NULL;

    printf("v1.0.1\n");

    if(argc==1)
    {
        pid=fork();
        if(pid<0)
            printf("fork() error\n");
        else if(pid>0)
        {
            sleep(1);
            printf("Parent process exit\n");
            exit(0);
        }

        setsid();
        redirect=freopen("server_log.out","w",stdout);    //redirect stdout
    }
    pthread_create(&threadID,NULL,distribute_task,NULL);
    pthread_create(&threadID,NULL,clear_zombie_conn_thread,NULL);
    listen_client();

    return 0;
}
