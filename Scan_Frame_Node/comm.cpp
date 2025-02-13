#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <list>

#include "comm.h"

using namespace std;

ServerComm *pgl_Server_Comm=NULL;
RecvThread *pgl_Recv_Thread=NULL;

list<RECV_EVENT> gl_Event_List;
pthread_mutex_t gl_EventListMutex;

extern Task *pgl_Task;
extern char **gl_argv;

int restart_proc()
{
    int pid;

    pid=fork();
    if(pid<0)
    {
        cout <<"fork() error."<<endl;
        exit(-1);
    }
    if(pid>0)
    {
        cout <<"Parent process exited."<<endl;
        sleep(1);
        exit(0);
    }

    //subproc
    setsid();
    execl(gl_argv[0],gl_argv[0],"-r",gl_argv[1],gl_argv[2],gl_argv[3],gl_argv[4],gl_argv[5],gl_argv[6],gl_argv[7],gl_argv[8],gl_argv[9],NULL);

    return 0;
}

ServerComm::ServerComm(CMD_OPTIONS &options)
{
    this->options=options;
    pAliveObj=NULL;
    reconnFlag=0;
    pthread_mutex_init(&sendMutex,NULL);

    return;
}

int ServerComm::run_recv_thread()
{
    if(pgl_Recv_Thread!=NULL)
    {
        pgl_Recv_Thread->update_socket(this->soc);
        return -1;
    }

    pgl_Recv_Thread=new RecvThread(this->soc);
    pgl_Recv_Thread->run(NULL);

    return 0;
}

int ServerComm::connect_to_server()
{
    addr.sin_addr.s_addr=inet_addr(this->options.serverIP);
    addr.sin_family=AF_INET;
    addr.sin_port=htons(this->options.serverPort);

    soc=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(soc==INVALID_SOCKET)
    {
        cout <<"create socket failed"<<endl;
        return -1;
    }

    if(connect(soc,(struct sockaddr *)&addr,sizeof(addr))!=0)
    {
        cout <<"connect failed"<<endl;
        close(this->soc);
        return -3;
    }

    //启动recv线程
    this->run_recv_thread();

    //认证客户端
    if(client_auth()!=0)
    {
        close(this->soc);
        return -4;
    }

    //创建心跳线程
    if(this->pAliveObj==NULL)
    {
        this->keep_alive();
    }

    return 0;
}

int ServerComm::reconnect()
{
    int n;

    close(this->soc);
    this->soc=INVALID_SOCKET;
    this->reconnFlag=1;
    for(n=0; 1; n++)
    {
        cout <<"Try to reconnect."<<endl;
        if(this->connect_to_server()==0)
        {
            cout <<"Reconnect success."<<endl;
            break;
        }
        cout <<"Reconnect failed."<<endl;
        sleep(10);
    }
    this->reconnFlag=0;

    return 0;
}

int ServerComm::client_auth()
{
    PROTO_CLIENT_AUTH clientAuthPacket,*pReponse=NULL;

    memset(&clientAuthPacket,NULL,sizeof(clientAuthPacket));

    strcat(clientAuthPacket.authKey,this->options.key);
    if(ex_send(&clientAuthPacket,E_PROTO_CLIENT_AUTH)<=0)
        return -1;
    if(this->reconnFlag)
        return 0;
    pReponse=(PROTO_CLIENT_AUTH *)wait_event(E_PROTO_CLIENT_AUTH,15000);
    if(pReponse==NULL)
    {
        cout <<"Auth timeout"<<endl;
        return -2;
    }
    if(pReponse->authStatus==0)
    {
        cout <<"Key invalid"<<endl;
        return -3;
    }
    pgl_Server_Comm->clientType=pReponse->authStatus;
    cout <<"client type:"<<pReponse->authStatus<<endl;
    free(pReponse);

    return 0;
}

int ServerComm::upload_file(FILE *srcFile,char *fileName,int fileType)
{
    PROTO_FILE_TRAN fileTranPacket;

    while(!feof(srcFile))
    {
        memset(&fileTranPacket,NULL,sizeof(fileTranPacket));

        fileTranPacket.fileType=fileType;
        strcat(fileTranPacket.fileName,fileName);
        fileTranPacket.dataSize=fread(fileTranPacket.fileData,sizeof(char),sizeof(fileTranPacket.fileData)-1,srcFile);
        fileTranPacket.feof=feof(srcFile);
        if(fileTranPacket.dataSize<0)
        {
            fclose(srcFile);
            cout <<"fread() error."<<endl;
            return -2;
        }
        if(ex_send(&fileTranPacket,E_PROTO_FILE_TRAN)<=0)
        {
            fclose(srcFile);
            cout <<"ex_send() error."<<endl;
            return -3;
        }
    }
    fclose(srcFile);

    return 0;
}

int ServerComm::upload_scan_module()
{
    FILE *srcFile=NULL;
    char fileName[255],*p;

    if(strlen(this->options.scanModulePath)>0)
    {
        memset(fileName,NULL,sizeof(fileName));
        srcFile=fopen(this->options.scanModulePath,"rb");
        if(srcFile==NULL)
        {
            cout <<"can't open "<<this->options.scanModulePath<<endl;
            return -1;
        }
        p=strrchr(options.scanModulePath,'/');
        if(p!=NULL)
        {
            strcat(fileName,++p);
        }
        else
        {
            strcat(fileName,options.scanModulePath);
        }
        if(upload_file(srcFile,fileName,E_FILE_SCAN_MOD))
        {
            cout <<fileName<<" upload failed"<<endl;
            return -2;
        }
        srcFile=NULL;
        cout <<fileName<<" upload success."<<endl;
    }

    if(strlen(this->options.scanListPath)>0)
    {
        memset(fileName,NULL,sizeof(fileName));
        srcFile=fopen(this->options.scanListPath,"rb");
        if(srcFile==NULL)
        {
            cout <<"can't open "<<this->options.scanListPath<<endl;
            return -1;
        }
        p=strrchr(options.scanListPath,'/');
        if(p!=NULL)
        {
            strcat(fileName,++p);
        }
        else
        {
            strcat(fileName,options.scanListPath);
        }
        if(upload_file(srcFile,fileName,E_FILE_SCAN_LIST))
        {
            cout <<fileName<<" upload failed"<<endl;
            return -2;
        }
        cout <<fileName<<" upload success."<<endl;
    }

    return 0;
}

int ServerComm::launch_scan_task()
{
    PROTO_LAUNCH_TASK launchPacket;

    memset(&launchPacket,NULL,sizeof(launchPacket));

    //default线程数为1
    if(options.threadNum==0)
        options.threadNum=1;
    //default进程数为1
    if(options.subprocNum==0)
        options.subprocNum=1;

    //assign
    launchPacket.subprocNum=options.subprocNum;
    launchPacket.threadNum=options.threadNum;
    launchPacket.timeo=options.timeo;
    launchPacket.reportLevel=options.reportLevel;

    strcat(launchPacket.modName,options.modName);
    strcat(launchPacket.listName,options.listName);

    //发送任务启动命令
    if(ex_send(&launchPacket,E_PROTO_LAUNCH_TASK)<=0)
        return -1;

    return 0;
}

int ServerComm::abort_scan_task()
{
    PROTO_TASK_NODE_INSTR nodeInstr;

    memset(&nodeInstr,NULL,sizeof(nodeInstr));

    nodeInstr.instruct=E_TASK_ABORT;

    if(ex_send(&nodeInstr,E_PROTO_TASK_NODE_INSTR)<=0)
        return -1;

    return 0;
}

int ServerComm::keep_alive()
{
    pAliveObj=new KeepAlive();

    return 0;
}

void deal_sigpipe(int sigNum)
{
    printf("proc recv SIGPIPE.\n");

    return;
}

int ServerComm::ex_send(void *buff,int proto)
{
    int sendSize,protoLen;
    PROTO_HEADER *pHeader=NULL;
    char sendBuffer[65536];

    memset(sendBuffer,NULL,sizeof(sendBuffer));
    pHeader=(PROTO_HEADER *)sendBuffer;

    pHeader->protoType=proto;
    protoLen=get_proto_len(proto);
    if(protoLen<=0)
        return -1;
    memcpy(sendBuffer+sizeof(PROTO_HEADER),buff,protoLen);

    signal(SIGPIPE,deal_sigpipe);

    pthread_mutex_lock(&sendMutex);
    sendSize=send(soc,sendBuffer,protoLen+sizeof(PROTO_HEADER),0);
    pthread_mutex_unlock(&sendMutex);

    return sendSize<=0?sendSize:sendSize-sizeof(PROTO_HEADER);
}

RecvThread::RecvThread(SOCKET soc)
{
    this->soc=soc;
    this->recvHeader=1;
    this->nextRecvSize=sizeof(PROTO_HEADER);
    this->recvBuffer=NULL;
    this->recvSize=0;
    pthread_mutex_init(&gl_EventListMutex,NULL);

    return;
}

int RecvThread::update_socket(SOCKET soc)
{
    //断线重连后更新socket
    while(this->soc!=soc)
    {
        this->soc=soc;
    }

    return 0;
}

void *RecvThread::func(void *Paramter)
{
    recv_message_from_server();
    return NULL;
}

int RecvThread::recv_message_from_server()
{
    int rSize;
    char *recvPointer=NULL;
    int proto;

    //printf("lanuch recv loop.\n");
    while(1)
    {
        if(this->recvBuffer==NULL)
        {
            this->recvSize=0;
            this->recvBuffer=(char *)malloc(this->nextRecvSize);
            if(this->recvBuffer==NULL) continue;
            memset(this->recvBuffer,NULL,this->nextRecvSize);
        }
        recvPointer=this->recvBuffer+this->recvSize;

        rSize=recv(this->soc,recvPointer,this->nextRecvSize-this->recvSize,0);
        if(rSize<=0)
        {
            //连接断开
            close(this->soc);
            cout <<"Connection closed by remote host."<<endl;
            if(pgl_Server_Comm->clientType==1)
            {
                //节点进程进行重连
                if(pgl_Server_Comm->reconnect()!=0)
                {
                    exit(0);
                }
                else
                {
                    //reconnect success
                    //参数重新初始化
                    if(this->recvBuffer!=NULL)
                    {
                        free(this->recvBuffer);
                        this->recvBuffer=NULL;
                    }
                    this->recvHeader=1;
                    this->nextRecvSize=sizeof(PROTO_HEADER);
                    this->recvSize=0;
                    continue;
                }
            }
            else
            {
                exit(0);
            }
        }

        this->recvSize+=rSize;
        //printf("recvSize:%d \n",this->recvSize);

        if(this->recvSize<this->nextRecvSize)
            continue;

        //recv finish
        if(this->recvHeader)
        {
            //接收完协议头
            proto=((PROTO_HEADER *)this->recvBuffer)->protoType;
            this->nextRecvSize=get_proto_len(proto);
            if(this->nextRecvSize<=0)
            {
                //协议不匹配
                close(this->soc);
                if(pgl_Server_Comm->clientType==1)
                {
                    //节点进程进行重连
                    if(pgl_Server_Comm->reconnect()!=0)
                    {
                        exit(0);
                    }
                    else
                    {
                        //reconnect success
                        //参数重新初始化
                        if(this->recvBuffer!=NULL)
                        {
                            free(this->recvBuffer);
                            this->recvBuffer=NULL;
                        }
                        this->recvHeader=1;
                        this->nextRecvSize=sizeof(PROTO_HEADER);
                        this->recvSize=0;
                        continue;
                    }
                }
                else
                {
                    exit(0);
                }
            }
            //printf("\nproto:0x%.2x\tsize:%d\n",proto,this->nextRecvSize);
            this->recvSize=0;
            this->recvHeader=0;
            free(this->recvBuffer);
            this->recvBuffer=NULL;
        }
        else
        {
            //接收完协议数据段，进行处理
            if(deal_protocol_data(proto)!=0)
                exit(0);

            this->recvHeader=1;
            this->nextRecvSize=sizeof(PROTO_HEADER);
            this->recvSize=0;

            free(this->recvBuffer);
            this->recvBuffer=NULL;
        }
    }
    return 0;
}

int RecvThread::deal_protocol_data(int proto)
{
    RECV_EVENT recvEvent;
    PROTO_CLIENT_AUTH *pClientAuth=NULL;
    PROTO_FILE_TRAN *pFileTran=NULL;
    PROTO_TASK_NODE_INSTR *pTaskInstr=NULL;
    PROTO_TASK_TARGET *pTaskTarget=NULL;

    memset(&recvEvent,NULL,sizeof(recvEvent));
    //printf("proto code:0x%.2x\n",proto);
    switch(proto)
    {
    case E_PROTO_CLIENT_AUTH:
        //客户端认证结果
        pClientAuth=(PROTO_CLIENT_AUTH *)recvBuffer;
        recvEvent.protocol=proto;
        recvEvent.pBuffer=(void *)recvBuffer;
        this->recvBuffer=NULL;

        pthread_mutex_lock(&gl_EventListMutex);
        gl_Event_List.push_back(recvEvent);
        pthread_mutex_unlock(&gl_EventListMutex);
        break;

    case E_PROTO_MESSAGE:
        //展示消息
        cout <<(*(PROTO_MESSAGE *)recvBuffer).message<<endl;
        break;

    case E_PROTO_FILE_TRAN:
        //接收文件
        pFileTran=(PROTO_FILE_TRAN *)recvBuffer;
        if(dstFile==NULL)
        {
            dstFile=fopen(pFileTran->fileName,"wb");
            if(dstFile==NULL)
            {
                cout <<"can't create "<<pFileTran->fileName<<endl;
                exit(-1);
            }
            else
            {
                cout <<"create \'"<<pFileTran->fileName<<"\' success."<<endl;
                //getchar();
            }
        }
        fwrite(pFileTran->fileData,sizeof(char),pFileTran->dataSize,dstFile);
        if(pFileTran->feof)
        {
            fclose(dstFile);
            //printf("file recv finish.\n");
            dstFile=NULL;
        }
        break;

    case E_PROTO_TASK_NODE_INSTR:
        //接收节点命令
        pTaskInstr=(PROTO_TASK_NODE_INSTR *)recvBuffer;
        printf("instruct code:0x%.2x\n",pTaskInstr->instruct);
        if(pTaskInstr->instruct==E_TASK_LANUCH && pgl_Task==NULL)
        {
            //启动任务
            pgl_Task=new Task(pTaskInstr->taskInfo);    //新建任务对象
            if(pgl_Task==NULL)
            {
                cout <<"new Task error."<<endl;
                exit(-1);
            }
            else
            {
                cout <<"create task success."<<endl;
            }
        }
        else if(pTaskInstr->instruct==E_TASK_DISTRI_FIN)
        {
            //SERVER distri task fin
            if(pgl_Task!=NULL)
            {
                while(!pgl_Task->taskDistriFinFlag)
                    pgl_Task->taskDistriFinFlag=1;
            }
            else
            {
                cout <<"task not found."<<endl;
            }
        }
        else if(pTaskInstr->instruct==E_TASK_ABORT)
        {
            //终止任务
            restart_proc(); //重启节点进程
        }
        else
        {
            if(pgl_Task!=NULL)
            {
                cout <<"The task is not complete."<<endl;
                exit(0);
            }
            else
            {
                cout <<"instruct invailed."<<endl;
            }
        }
        break;

    case E_PROTO_TASK_TARGET:
        //节点接收到任务目标
        pTaskTarget=(PROTO_TASK_TARGET *)recvBuffer;
        if(pgl_Task==NULL)
        {
            cout <<"Task object not found."<<endl;
            break;
        }
        pgl_Task->add_scan_target(*pTaskTarget);
        break;

    default:
        cout <<"Protocol mismatch."<<endl;
        return -1;
    }

    return 0;
}

KeepAlive::KeepAlive()
{
    this->run(NULL);
    return;
}

void *KeepAlive::func(void *para)
{
    PROTO_KEEP_ALIVE alivePacket;

    while(1)
    {
        sleep(3);
        if(pgl_Server_Comm->ex_send(&alivePacket,E_PROTO_KEEP_ALIVE)<=0)
        {
            /*
            if(pgl_Server_Comm->reconnect()!=0)
            {
                break;
            }*/
            cout <<"send heartbeat packet failed."<<endl;
        }
        else
        {
            //cout <<"send heartbeat packet."<<endl;
        }
    }
    return NULL;
}

void *wait_event(int protocol,int timeo)
{
    list<RECV_EVENT>::iterator iter;
    void *pRet=NULL;

    while(timeo)
    {
        pthread_mutex_lock(&gl_EventListMutex);
        for(iter=gl_Event_List.begin(); iter!=gl_Event_List.end(); iter++)
        {
            if((*iter).protocol==protocol)
            {
                pRet=(*iter).pBuffer;
                gl_Event_List.erase(iter);
                pthread_mutex_unlock(&gl_EventListMutex);
                return pRet;
            }
        }
        pthread_mutex_unlock(&gl_EventListMutex);
        usleep(1000);
        timeo-=1;
    }

    return NULL;
}

int get_proto_len(int proto)
{
    //获取协议数据段长度
    switch(proto)
    {
    case E_PROTO_CLIENT_AUTH:
        return sizeof(PROTO_CLIENT_AUTH);

    case E_PROTO_FILE_TRAN:
        return sizeof(PROTO_FILE_TRAN);

    case E_PROTO_LAUNCH_TASK:
        return sizeof(PROTO_LAUNCH_TASK);

    case E_PROTO_MESSAGE:
        return sizeof(PROTO_MESSAGE);

    case E_PROTO_TASK_NODE_INSTR:
        return sizeof(PROTO_TASK_NODE_INSTR);

    case E_PROTO_TASK_TARGET:
        return sizeof(PROTO_TASK_TARGET);

    case E_PROTO_TASK_REPORT:
        return sizeof(PROTO_TASK_REPORT);

    case E_PROTO_KEEP_ALIVE:
        return sizeof(PROTO_KEEP_ALIVE);

    case E_PROTO_GET_RESULTS:
        return sizeof(PROTO_GET_RESULTS);

    default:
        printf("can't get proto len,protocol mismatch.code:0x%.2x\n",proto);
        return -1;
    }
}







