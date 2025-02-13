#include <pthread.h>

#include "task.h"
#include "concurrent/async.h"
#include "concurrent/cleanup.h"

LANUCH_TASK_INFO gl_Task;
pthread_rwlock_t gl_TaskRwlock;

extern IO_OPERATION_DATA_NODE *IO_Operation_Data_Header;
extern pthread_rwlock_t LockIOData;

int send_data_to_client(void *buff,int proto,int clientType,int closeFlag)
{
    //closeFlag：发送完数据后关闭连接
    int ret=0;
    int i,sendSize;
    IO_OPERATION_DATA_NODE *pIONode=NULL;
    IO_OPERATION_DATA *pIOData=NULL;

    pthread_rwlock_rdlock(&LockIOData);
    for(pIONode=IO_Operation_Data_Header; pIONode!=NULL; pIONode=pIONode->next)
    {
        for(i=0; i<ASYNC_MAX_WAIT_OBJECTS; i++)
        {
            if(pIONode->IOArray[i]!=NULL)
            {
                pIOData=pIONode->IOArray[i];
                if(pIOData->clientType==clientType)
                {
                    //printf("send to client:%s\n",inet_ntoa(pIOData->Address.sin_addr));
                    if((sendSize=ex_send(pIOData,buff,proto))<=0)
                    {
                        printf("send failed\n");
                        printf("node expection:%s\n",inet_ntoa(pIOData->Address.sin_addr));
                        clean_client_connection(pIOData);
                        ret++;
                    }
                    else
                    {
                        //printf("send size:%d\n",sendSize);
                        if(closeFlag)
                        {
                            clean_client_connection(pIOData);
                        }
                    }
                }
            }
        }
    }
    pthread_rwlock_unlock(&LockIOData);

    return ret;
}

int send_mod_to_node(LANUCH_TASK_INFO *pTaskInfo)
{
    FILE *srcFile=NULL;
    PROTO_FILE_TRAN fileTranPacket;

    srcFile=fopen(pTaskInfo->modName,"rb");
    if(srcFile==NULL)
    {
        printf("can't open %s\n",pTaskInfo->modName);
        return -1;
    }

    while(!feof(srcFile))
    {
        memset(&fileTranPacket,NULL,sizeof(fileTranPacket));
        fileTranPacket.fileType=E_FILE_SCAN_MOD;
        strcat(fileTranPacket.fileName,pTaskInfo->modName);
        fileTranPacket.dataSize=fread(fileTranPacket.fileData,sizeof(char),sizeof(fileTranPacket.fileData)-1,srcFile);
        if(fileTranPacket.dataSize<=0)
            return -1;
        fileTranPacket.feof=feof(srcFile);
        //广播数据包
        send_data_to_client(&fileTranPacket,E_PROTO_FILE_TRAN,1,0);
        //printf("send mod:%s\n",fileTranPacket.fileName);
    }

    fclose(srcFile);
    printf("send_mod_to_node finish\n");

    return 0;
}

int launch_instr_to_node()
{
    PROTO_TASK_NODE_INSTR nodeInstrPacket;

    memset(&nodeInstrPacket,NULL,sizeof(nodeInstrPacket));

    //assign
    nodeInstrPacket.instruct=E_TASK_LANUCH;
    nodeInstrPacket.taskInfo.subprocNum=gl_Task.subprocNum;
    nodeInstrPacket.taskInfo.threadNum=gl_Task.threadNum;
    nodeInstrPacket.taskInfo.timeo=gl_Task.timeo;
    nodeInstrPacket.taskInfo.reportLevel=gl_Task.reportLevel;
    strcat(nodeInstrPacket.taskInfo.listName,gl_Task.listName);
    strcat(nodeInstrPacket.taskInfo.modName,gl_Task.modName);

    //broadcast
    send_data_to_client(&nodeInstrPacket,E_PROTO_TASK_NODE_INSTR,1,0);

    return 0;
}

int distribute_task_target()
{
    FILE *srcFile=NULL;
    PROTO_TASK_TARGET taskTargetPacket;
    PROTO_TASK_NODE_INSTR taskInstr;
    int len=0,readSize=0,count=0;
    int i=0,sendSize=0;
    IO_OPERATION_DATA_NODE *pIONode=NULL;
    IO_OPERATION_DATA *pIOData=NULL;
    char *pBuff=NULL;
    int sendFlag,allowSend=0;
    int ret=0;
    char readBuff[5000];

    pBuff=taskTargetPacket.target;
    memset(&taskTargetPacket,NULL,sizeof(taskTargetPacket));

    srcFile=fopen(gl_Task.listName,"rb");
    if(srcFile==NULL)
    {
        printf("can't open %s\n",gl_Task.listName);
        return -1;
    }
    else
    {
        printf("open %s\n",gl_Task.listName);
    }

    pthread_rwlock_rdlock(&LockIOData);
    while(!feof(srcFile))
    {
        allowSend=0;
        memset(readBuff,NULL,sizeof(readBuff));
        if(fgets(readBuff,sizeof(readBuff)-1,srcFile)!=NULL)
        {
            //printf("%s",taskTargetPacket.target);
            readSize=strlen(readBuff);
            if(sizeof(taskTargetPacket.target)-strlen(taskTargetPacket.target)-2>readSize)
            {
                //还有空间存储任务目标
                strcat(pBuff,readBuff);
                pBuff+=readSize;
            }
            else
            {
                //没有空间存储任务目标
                if(strlen(taskTargetPacket.target))
                {
                    //有任务目标已写入待发送的数据包中
                    if(readSize<sizeof(taskTargetPacket.target)-2)
                    {
                        //文件指针回滚
                        readSize=0-readSize;
                        fseek(srcFile,readSize,SEEK_CUR);
                    }
                    allowSend=1;
                }
                else
                {
                    continue;
                }
            }
        }

        if(allowSend || feof(srcFile))
        {
            //send task target to node
            //printf("%s\n",taskTargetPacket.target);
            for(sendFlag=1; 1; pIONode=pIONode->next,i=0)
            {
                if(pIONode==NULL)
                {
                    pIONode=IO_Operation_Data_Header;
                    i=0;
                    if(sendFlag==0)
                    {
                        printf("node not found.\n");
                        sleep(1);
                    }
                    sendFlag=0;
                }

                for(; i<ASYNC_MAX_WAIT_OBJECTS; i++)
                {
                    if(pIONode->IOArray[i]!=NULL)
                    {
                        pIOData=pIONode->IOArray[i];
                        if(pIOData->clientType==1)
                        {
                            //printf("send to client:%s\n",inet_ntoa(pIOData->Address.sin_addr));
                            sendFlag=1;
                            if((sendSize=ex_send(pIOData,&taskTargetPacket,E_PROTO_TASK_TARGET))<=0)
                            {
                                //printf("distribute task to %s error.\n",inet_ntoa(pIOData->Address.sin_addr));
                                clean_client_connection(pIOData);
                                //pthread_rwlock_unlock(&LockIOData);
                                //return -1;
                                ret=-2;
                            }
                            else
                            {
                                //printf("send num:%d\n",count);
                                if(++i>=ASYNC_MAX_WAIT_OBJECTS)
                                {
                                    i=0;
                                    pIONode=pIONode->next;
                                }
                                goto skip;
                            }
                        }
                    }
                }
            }
skip:
            count=0;
            len=0;
            readSize=0;
            pBuff=taskTargetPacket.target;
            memset(&taskTargetPacket,NULL,sizeof(taskTargetPacket));
        }
    }
    pthread_rwlock_unlock(&LockIOData);

    memset(&taskInstr,NULL,sizeof(taskInstr));
    taskInstr.instruct=E_TASK_DISTRI_FIN;
    if(send_data_to_client(&taskInstr,E_PROTO_TASK_NODE_INSTR,1,0)!=0)
    {
        fclose(srcFile);
        return -1;
    }

    fclose(srcFile);

    return ret;
}

int confirm_task_node()
{
    int ret=0;
    int i;
    IO_OPERATION_DATA_NODE *pIONode=NULL;
    IO_OPERATION_DATA *pIOData=NULL;

    pthread_rwlock_rdlock(&LockIOData);
    for(pIONode=IO_Operation_Data_Header; pIONode!=NULL; pIONode=pIONode->next)
    {
        for(i=0; i<ASYNC_MAX_WAIT_OBJECTS; i++)
        {
            if(pIONode->IOArray[i]!=NULL)
            {
                pIOData=pIONode->IOArray[i];
                if(pIOData->clientType==1)
                {
                    gl_Task.taskNode++;
                }
            }
        }
    }
    pthread_rwlock_unlock(&LockIOData);

    return 0;
}

int send_results_to_manager(IO_OPERATION_DATA *pIOData)
{
    PROTO_FILE_TRAN fileTranPacket;
    FILE *fileRes=NULL;

    fileRes=fopen("scan_results.txt","rb");
    if(fileRes==NULL) return -1;

    while(!feof(fileRes))
    {
        memset(&fileTranPacket,NULL,sizeof(fileTranPacket));
        fileTranPacket.dataSize=fread(fileTranPacket.fileData,sizeof(char),sizeof(fileTranPacket.fileData)-1,fileRes);
        if(fileTranPacket.dataSize<=0)
        {
            printf("fread() error.\n");
            return -1;
        }
        fileTranPacket.feof=feof(fileRes);
        fileTranPacket.fileType=E_FILE_RES;
        strcat(fileTranPacket.fileName,"scan_results.txt");

        if(ex_send(pIOData,&fileTranPacket,E_PROTO_FILE_TRAN)<=0)
        {
            printf("ex_send() error.\n");
            return -1;
        }
    }

    return 0;
}

void *get_report_thread(void *para)
{
    //向管理员发送结果报告线程
    IO_OPERATION_DATA *pIOData=(IO_OPERATION_DATA *)para;
    PROTO_MESSAGE messagePacket;

    pthread_detach(pthread_self());

    if(gl_Task.status)
    {
        memset(&messagePacket,NULL,sizeof(messagePacket));
        strcat(messagePacket.message,"The task is not complete.");
        ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
    }
    else
    {
        if(send_results_to_manager(pIOData)!=0)
        {
            memset(&messagePacket,NULL,sizeof(messagePacket));
            strcat(messagePacket.message,"scan_results not found.");
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
        }
        else
        {
            memset(&messagePacket,NULL,sizeof(messagePacket));
            strcat(messagePacket.message,"Get report success.");
            ex_send(pIOData,&messagePacket,E_PROTO_MESSAGE);
        }
    }

    close(pIOData->Socket);

    return NULL;
}

void *distribute_task(void *para)
{
    PROTO_TASK_NODE_INSTR nodeInstr;
    int distriSucc;

    pthread_detach(pthread_self());
    pthread_rwlock_init(&gl_TaskRwlock,NULL);

    while(1)
    {
        if(gl_Task.status==0)
        {
            sleep(1);
            continue;
        }

        gl_Task.resFile=fopen("scan_results.txt","wt");
        if(gl_Task.resFile==NULL)
        {
            printf("create scan_results.txt failed.\n");
            memset(&gl_Task,NULL,sizeof(gl_Task));
            continue;
        }
        else
        {
            printf("create scan_results.txt\n");
        }

        //向所有节点发送模块文件
        printf("Assigned tasks.\n");
        if(send_mod_to_node(&gl_Task)!=0)
        {
            memset(&gl_Task,NULL,sizeof(gl_Task));
            printf("send mod error.\n");
            continue;
        }

        //确认参与任务的节点数
        confirm_task_node();

        //向节点发送启动任务命令和相关参数
        launch_instr_to_node();
        printf("send lanuch instruct success.\n");

        //分配任务列表
        if((distriSucc=distribute_task_target())!=0)
        {
            //node exception
            printf("distribute_task_target failed.\n");
        }
        else
        {
            printf("distribute_task_target success.\n");
        }

        while(gl_Task.taskNode>0)
        {
            if(gl_Task.exit)
            {
                break;
            }
            if(distriSucc!=0)
            {
                memset(&nodeInstr,NULL,sizeof(nodeInstr));
                nodeInstr.instruct=E_TASK_DISTRI_FIN;
                send_data_to_client(&nodeInstr,E_PROTO_TASK_NODE_INSTR,1,0);
            }
            sleep(5);
        }

        pthread_rwlock_wrlock(&gl_TaskRwlock);
        fclose(gl_Task.resFile);
        gl_Task.resFile=NULL;
        memset(&gl_Task,NULL,sizeof(gl_Task));
        pthread_rwlock_unlock(&gl_TaskRwlock);

        printf("task finish.\n");
    }

    return NULL;
}









