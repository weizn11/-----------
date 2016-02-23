#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "cmdopt.h"
#include "comm.h"

using namespace std;

extern ServerComm *pgl_Server_Comm;
char **gl_argv=NULL;

FILE *file=NULL;

void *trunc_log_thread(void *para)
{
    struct stat st;

    while(1)
    {
        sleep(5);
        memset(&st,NULL,sizeof(st));
        if(stat("node_log.txt",&st)!=0)
        {
            continue;
        }
        if(st.st_size>10*1024*1024)
        {
            fseek(file,0,SEEK_SET);
            if(truncate("node_log.txt",0)!=0)
            {
                cout <<"truncate() error."<<endl;
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

    CMD_OPTIONS options;
    pthread_t threadID;
    PROTO_GET_RESULTS getResPacket;

    gl_argv=argv;

    printf("v1.0.1\n");
    if(argc<7)
    {
        return -1;
    }

    config_init(options,argc,argv);

    file=fopen("node_log.txt","w");
    if(file==NULL)
    {
        return NULL;
    }
    if(strcmp(options.key,CLIENT_NODE_AUTH_KEY)==0)
    {
        //node
        if(options.forground==0)
        {
            pid=fork();
            if(pid<0)
            {
                printf("fork() error\n");
                return -1;
            }
            else if(pid>0)
            {
                sleep(1);
                printf("Parent process exit\n");
                exit(0);
            }
            setsid();
            //freopen("node_log.out","w",stdout);    //redirect stdout
            pthread_create(&threadID,NULL,trunc_log_thread,NULL);
            pthread_create(&threadID,NULL,output_log,NULL);
        }

        //cout <<"create comm thread."<<endl;
        pgl_Server_Comm=new ServerComm(options);
reconn:
        if(pgl_Server_Comm->connect_to_server()!=0)
        {
            if(options.reconn)
            {
                sleep(5);
                goto reconn;
            }
            return -1;
        }
    }
    else
    {
        //manager
        //cout <<"create comm thread."<<endl;
        pgl_Server_Comm=new ServerComm(options);
        if(pgl_Server_Comm->connect_to_server()!=0)
            return -1;

        if(options.getRes)
        {
            memset(&getResPacket,NULL,sizeof(getResPacket));
            pgl_Server_Comm->ex_send(&getResPacket,E_PROTO_GET_RESULTS);
        }
        if(options.upScanList || options.upScanMod)
        {
            pgl_Server_Comm->upload_scan_module();
            exit(0);
        }
        if(options.launch && strlen(options.modName) && strlen(options.listName))
        {
            pgl_Server_Comm->launch_scan_task();
        }
        if(options.taskAbort)
        {
            pgl_Server_Comm->abort_scan_task();
        }
    }

    while(1) sleep(1);

    return 0;
}
