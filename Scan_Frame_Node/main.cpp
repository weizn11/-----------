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

    file=fopen("node_log.txt","w");
    if(file==NULL)
    {
        return NULL;
    }

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

    gl_argv=argv;   //重启参数

    printf("v1.0.1\n");
    if(argc<7)
    {
        return -1;
    }

    config_init(options,argc,argv);     //解析命令行

    if(strcmp(options.key,CLIENT_NODE_AUTH_KEY)==0)
    {
        //node
        if(options.forground==0)
        {
            //启动后台进程
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
            setsid();   //建立新的会话id
            signal(SIGTERM,deal_sigterm);

            pthread_create(&threadID,NULL,trunc_log_thread,NULL);   //防止日志过大
            pthread_create(&threadID,NULL,output_log,NULL);     //输出重定向
        }

        //create comm thread.
        pgl_Server_Comm=new ServerComm(options);
reconn:
        //连接到server
        if(pgl_Server_Comm->connect_to_server()!=0)
        {
            if(options.reconn)
            {
                //失败重连
                sleep(5);
                goto reconn;
            }
            return -1;
        }
    }
    else
    {
        //manager
        //create comm thread.
        pgl_Server_Comm=new ServerComm(options);
        if(pgl_Server_Comm->connect_to_server()!=0)
            return -1;

        if(options.getRes)
        {
            //获取报告
            memset(&getResPacket,NULL,sizeof(getResPacket));
            pgl_Server_Comm->ex_send(&getResPacket,E_PROTO_GET_RESULTS);
        }
        if(options.upScanList || options.upScanMod)
        {
            //上传文件
            pgl_Server_Comm->upload_scan_module();
            exit(0);
        }
        if(options.launch && strlen(options.modName) && strlen(options.listName) && options.reportLevel)
        {
            //启动任务
            pgl_Server_Comm->launch_scan_task();
        }
        if(options.taskAbort)
        {
            //终止任务
            pgl_Server_Comm->abort_scan_task();
        }
    }

    while(1) sleep(1);

    return 0;
}
