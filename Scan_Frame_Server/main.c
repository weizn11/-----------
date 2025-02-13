#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#include "concurrent/async.h"
#include "task.h"

FILE *file=NULL;

void deal_sigterm(int sigNum)
{
    printf("proc recv SIGTERM.\n");

    return;
}

void *trunc_log_thread(void *para)
{
    struct stat st;

    while(1)
    {
        sleep(5);
        memset(&st,NULL,sizeof(st));
        if(stat("serv_log.txt",&st)!=0)
        {
            continue;
        }
        if(st.st_size>10*1024*1024)
        {
            fseek(file,0,SEEK_SET);
            if(truncate("serv_log.txt",0)!=0)
            {
                printf("truncate() error.\n");
                exit(-1);
            }
        }
    }

    return NULL;
}

void *output_log(void *para)
{
    fd_set fdRead;
    struct timeval timeo;
    char recvBuff[PIPE_BUF+1];
    int logPipe[2];
    int recvSize;

    pipe(logPipe);
    dup2(logPipe[1],STDOUT_FILENO);

    file=fopen("serv_log.txt","w");
    if(file==NULL)
    {
        return NULL;
    }

    while(1)
    {
        memset(&timeo,NULL,sizeof(timeo));
        timeo.tv_usec=1000;
        FD_ZERO(&fdRead);
        FD_SET(logPipe[0],&fdRead);
        if(select(logPipe[0]+1,&fdRead,NULL,NULL,&timeo)<=0)
        {
            continue;
        }
        if(FD_ISSET(logPipe[0],&fdRead))
        {
            memset(recvBuff,NULL,sizeof(recvBuff));
            if((recvSize=read(logPipe[0],recvBuff,sizeof(recvBuff)-1))<=0)
            {
                continue;
            }
            fwrite(recvBuff,sizeof(char),recvSize,file);
            fflush(file);
        }
    }

    return NULL;
}

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
        signal(SIGTERM,deal_sigterm);
        //redirect=freopen("server_log.out","w",stdout);    //redirect stdout
        pthread_create(&threadID,NULL,trunc_log_thread,NULL);
        pthread_create(&threadID,NULL,output_log,NULL);
    }

    pthread_create(&threadID,NULL,distribute_task,NULL);
    pthread_create(&threadID,NULL,clear_zombie_conn_thread,NULL);
    listen_client();

    return 0;
}
