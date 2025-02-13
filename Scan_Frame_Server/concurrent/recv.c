#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>

#include "recv.h"
#include "async.h"
#include "cleanup.h"
#include "mempool.h"
#include "../protocol.h"
#include "../task.h"

extern unsigned long int TotalConnections;
extern unsigned long int ListenThreads;
extern pthread_rwlock_t LockIOData;
extern IO_OPERATION_DATA_NODE *IO_Operation_Data_Header;
extern LANUCH_TASK_INFO gl_Task;
extern pthread_rwlock_t gl_TaskRwlock;

int deal_protocol_data(IO_OPERATION_DATA *pIOData)
{
    char *pStr=NULL;
    int len;
    pthread_t threadID;
    PROTO_MESSAGE messagePacket;
    PROTO_CLIENT_AUTH *pClientAuth=NULL;
    PROTO_FILE_TRAN *pFileTran=NULL;
    PROTO_LAUNCH_TASK *pLaunchTask=NULL;
    PROTO_TASK_REPORT *pTaskReport=NULL;
    PROTO_TASK_NODE_INSTR *pNodeInstr=NULL;
    PROTO_TASK_NODE_INSTR tmpNodeInstr;

    //printf("proto code:0x%.2x\n",pIOData->protocol);
    switch(pIOData->protocol)
    {
    case E_PROTO_CLIENT_AUTH:
        //客户端类型认证
        pClientAuth=(PROTO_CLIENT_AUTH *)pIOData->recvBuffer;
        pClientAuth->authKey[sizeof(pClientAuth->authKey)-1]=NULL;
        if (!strcmp(pClientAuth->authKey,CLIENT_NODE_AUTH_KEY))
        {
            pIOData->clientType=1;
            printf("A node incoming\n");
            pClientAuth->authStatus=1;
            ex_send(pIOData,pClientAuth,E_PROTO_CLIENT_AUTH);
        }
        else if(!strcmp(pClientAuth->authKey,CLIENT_MANAGER_AUTH_KEY))
        {
            pIOData->clientType=2;
            printf("The manager incoming\n");
            pClientAuth->authStatus=2;
            ex_send(pIOData,pClientAuth,E_PROTO_CLIENT_AUTH);
        }
        else
        {
            ex_send(pIOData,pClientAuth,E_PROTO_CLIENT_AUTH);
            printf("Client type mismatch.\n");
            return -1;
        }
        return 0;
    case E_PROTO_FILE_TRAN:
        //接收文件
        pFileTran=(PROTO_FILE_TRAN *)pIOData->recvBuffer;
        if(pIOData->modFile==NULL)
        {
            pIOData->modFile=fopen(pFileTran->fileName,"wb");
            if(pIOData->modFile==NULL)
            {
                printf("create file failed\n");
                return -2;
            }
        }
        fwrite(pFileTran->fileData,sizeof(char),pFileTran->dataSize,pIOData->modFile);
        if(pFileTran->feof)
        {
            //recv file finish
            printf("recv '%s' completed.\n",pFileTran->fileName);
            fclose(pIOData->modFile);
            pIOData->modFile=NULL;
        }
        return 0;
    case E_PROTO_LAUNCH_TASK:
        //启动任务
        pLaunchTask=(PROTO_LAUNCH_TASK *)pIOData->recvBuffer;
        if(access(pLaunchTask->modName,0))
        {
            //检测模块文件是否存在
            memset(&messagePacket,NULL,sizeof(messagePacket));
            sprintf(messagePacket.message,"\'%s\' not found.",pLaunchTask->modName);
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
            printf("%s\n",messagePacket.message);
            break;
        }
        if(access(pLaunchTask->listName,0))
        {
            //检测列表文件是否存在
            memset(&messagePacket,NULL,sizeof(messagePacket));
            sprintf(messagePacket.message,"\'%s\' not found.",pLaunchTask->listName);
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
            printf("%s\n",messagePacket.message);
            break;
        }
        if(gl_Task.status==0)
        {
            //当前无任务在运行
            memset(&gl_Task,NULL,sizeof(gl_Task));

            //assign
            strcat(gl_Task.modName,pLaunchTask->modName);
            strcat(gl_Task.listName,pLaunchTask->listName);
            gl_Task.subprocNum=pLaunchTask->subprocNum;
            gl_Task.threadNum=pLaunchTask->threadNum;
            gl_Task.timeo=pLaunchTask->timeo;
            gl_Task.reportLevel=pLaunchTask->reportLevel;
            while(gl_Task.status==0) gl_Task.status=1;

            memset(&messagePacket,NULL,sizeof(messagePacket));
            strcat(messagePacket.message,"Task running.");
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
            printf("Task running.\n");
        }
        else
        {
            //当前有未完成的任务
            memset(&messagePacket,NULL,sizeof(messagePacket));
            strcat(messagePacket.message,"Already has a task running.");
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
            printf("Already has a task running.\n");
        }
        break;
    case E_PROTO_TASK_REPORT:
        //收到扫描报告
        pTaskReport=(PROTO_TASK_REPORT *)pIOData->recvBuffer;
        pthread_rwlock_wrlock(&gl_TaskRwlock);
        if(gl_Task.status)
        {
            //有任务存在
            if(pTaskReport->taskFin)
            {
                //当前节点报告任务完成
                if(gl_Task.taskNode>0)
                    gl_Task.taskNode--;
            }
            else
            {
                //接收到报告结果
                if(gl_Task.reportLevel==1)
                {
                    pStr=strtok(pTaskReport->target,"\n");
                    while(pStr!=NULL)
                    {
                        //printf("recv report of %s\n",pStr);
                        if(gl_Task.resFile==NULL)
                        {
                            pthread_rwlock_unlock(&gl_TaskRwlock);
                            break;
                        }
                        fseek(gl_Task.resFile,0,SEEK_END);
                        fputs(pStr,gl_Task.resFile);
                        fputc('\n',gl_Task.resFile);
                        fflush(gl_Task.resFile);

                        pStr=strtok(NULL,"\n");
                    }
                }
                else
                {
                    //接收到的报告等级为2。
                    if(pIOData->reportBuf==NULL)
                    {
                        pIOData->reportBuf=(char *)malloc(strlen(pTaskReport->target)+1);
                        memset(pIOData->reportBuf,NULL,strlen(pTaskReport->target)+1);
                    }
                    else
                    {
                        len=strlen(pIOData->reportBuf);
                        pIOData->reportBuf=(char *)realloc(pIOData->reportBuf,len+strlen(pTaskReport->target)+1);
                        memset(pIOData->reportBuf+len,NULL,strlen(pTaskReport->target)+1);
                    }
                    strcat(pIOData->reportBuf,pTaskReport->target);

                    if(pTaskReport->sendFin)
                    {
                        fseek(gl_Task.resFile,0,SEEK_END);
                        fputs(pIOData->reportBuf,gl_Task.resFile);
                        free(pIOData->reportBuf);
                        pIOData->reportBuf=NULL;
                    }
                }
            }
        }
        else
        {
            //server无任务作业，请求节点重启进程。
            memset(&tmpNodeInstr,NULL,sizeof(tmpNodeInstr));
            tmpNodeInstr.instruct=E_TASK_ABORT;
            ex_send(pIOData,&tmpNodeInstr,E_PROTO_TASK_NODE_INSTR);
            pthread_rwlock_unlock(&gl_TaskRwlock);
            break;
        }
        pthread_rwlock_unlock(&gl_TaskRwlock);
        return 0;
        break;

    case E_PROTO_KEEP_ALIVE:
        //心跳包
        pIOData->timestamp=time(NULL);
        return 0;

    case E_PROTO_GET_RESULTS:
        //请求获取结果报告
        memset(&threadID,NULL,sizeof(threadID));
        pthread_create(&threadID,NULL,get_report_thread,(void *)pIOData);
        return 0;
    case E_PROTO_TASK_NODE_INSTR:
        //
        pNodeInstr=(PROTO_TASK_NODE_INSTR *)pIOData->recvBuffer;
        send_data_to_client(pIOData->recvBuffer,E_PROTO_TASK_NODE_INSTR,1,1);
        if(pNodeInstr->instruct==E_TASK_ABORT)
        {
            gl_Task.exit=1;
        }
        break;
    }

    return -1;
}

int recv_data_from_client(IO_OPERATION_DATA *pIOData)
{
    /*
    接收客户端传来的数据，保存在IO_OPERATION_DATA结构体的recvBuffer中
    传入当前客户端相关信息的结构体指针
    */
    int recvSize;
    char *recvPointer=NULL;

    if(pIOData->recvBuffer==NULL)
    {
        pIOData->recvSize=0;
        pIOData->recvBuffer=(char *)malloc(pIOData->nextRecvSize);
        if(pIOData->recvBuffer==NULL) return 0;
        memset(pIOData->recvBuffer,NULL,pIOData->nextRecvSize);
        recvPointer=pIOData->recvBuffer;
    }
    else
        recvPointer=pIOData->recvBuffer+pIOData->recvSize;

    recvSize=recv(pIOData->Socket,recvPointer,pIOData->nextRecvSize-pIOData->recvSize,0);
    if(recvSize<=0)
    {
        //socket disable
        return -2;
    }
    pIOData->recvSize+=recvSize;

    if(pIOData->recvSize<pIOData->nextRecvSize)
        return recvSize;

    //recv finish
    if(pIOData->recvHeader)
    {
        //recv protocol header
        pIOData->nextRecvSize=get_proto_len(((PROTO_HEADER *)pIOData->recvBuffer)->protoType);
        if(pIOData->nextRecvSize<=0)
        {
            printf("Protocol mismatch.\n");
            return -3;
        }
        pIOData->recvSize=0;
        pIOData->recvHeader=0;
        pIOData->protocol=((PROTO_HEADER *)pIOData->recvBuffer)->protoType;
        //printf("%d\n",pIOData->protocol);

        free(pIOData->recvBuffer);
        pIOData->recvBuffer=NULL;
    }
    else
    {
        //recv protocol data
        if(deal_protocol_data(pIOData)!=0)
            return -4;
        pIOData->recvHeader=1;
        pIOData->nextRecvSize=sizeof(PROTO_HEADER);
        pIOData->recvSize=0;

        free(pIOData->recvBuffer);
        pIOData->recvBuffer=NULL;
    }

    return recvSize;
}

int listen_socket_from_client(void *Parameter)
{
    int activeCount;
    int i,j;
    IO_OPERATION_DATA_NODE *IO_Operation_Node_Pointer=NULL;
    struct epoll_event eventList[ASYNC_MAX_WAIT_OBJECTS];

    printf("Create a new thread\n");

    pthread_detach(pthread_self());     //线程状态分离
    IO_Operation_Node_Pointer=(IO_OPERATION_DATA_NODE *)Parameter;  //当前线程操作的链表节点的指针

    while(1)
    {
        activeCount=epoll_wait(IO_Operation_Node_Pointer->epollfd,eventList,ASYNC_MAX_WAIT_OBJECTS,SOCKET_TIMEOUT);
        if(activeCount<=0) continue;

        for(j=0; j<ASYNC_MAX_WAIT_OBJECTS; j++)
        {
            if(IO_Operation_Node_Pointer->IOArray[j]==NULL) continue;
            for(i=0; i<activeCount; i++)
            {
                if(IO_Operation_Node_Pointer->IOArray[j]->Socket==eventList[i].data.fd)
                {
                    if(eventList[i].events & EPOLLIN)
                    {
                        if(recv_data_from_client(IO_Operation_Node_Pointer->IOArray[j])<=0)
                        {
                            //clean sockset;
                            pthread_rwlock_wrlock(&LockIOData);
                            clean_client_connection(IO_Operation_Node_Pointer->IOArray[j]);
                            pthread_rwlock_unlock(&LockIOData);
                        }
                    }
                    else
                    {
                        //client disconnect
                        pthread_rwlock_wrlock(&LockIOData);
                        clean_client_connection(IO_Operation_Node_Pointer->IOArray[j]);
                        pthread_rwlock_unlock(&LockIOData);
                    }
                    break;
                }
            }
        }
    }

    return 0;
}










