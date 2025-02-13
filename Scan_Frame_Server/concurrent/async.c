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
#include <sys/epoll.h>
#include <fcntl.h>
#include <linux/tcp.h>
#include <signal.h>

#include "async.h"
#include "recv.h"
#include "mempool.h"
#include "stack.h"
#include "../protocol.h"

unsigned long int TotalConnections=0;    //当前连接总数
unsigned long int ListenThreads=0;       //当前启动监听线程总数
pthread_rwlock_t LockIOData;            //IO操作读写锁
MEMPOOL_LIST *Mempool_IOData;              //IO_OPERATION_DATA结构内存池描述字
STACK_INFO Stack_IOData;                    //栈信息结构体
IO_OPERATION_DATA_NODE *IO_Operation_Data_Header=NULL;
MEMPOOL_LIST *mempoolListHeader=NULL;

int listen_client()
{
    int addrLength=0,index=0,createThread=0;
    pthread_t threadID;
    struct epoll_event event;
    SOCKET ClientSocket=INVALID_SOCKET,ListenSocket=INVALID_SOCKET;
    struct sockaddr_in ClientAddress,LocalAddress;
    IO_OPERATION_DATA_NODE *IO_Operation_Node_Pointer=NULL;
    STACK_DATA_TYPE IODataInfo;         //栈元素变量
    int reuse_addr=1,sfd_flags,nagle_flag=1;
    struct timeval timeout= {10,0}; //10s

    memset(&LocalAddress,NULL,sizeof(struct sockaddr_in));
    memset(&Stack_IOData,NULL,sizeof(STACK_INFO));

    LocalAddress.sin_family=AF_INET;
    LocalAddress.sin_port=htons(LISTEN_PORT);
    LocalAddress.sin_addr.s_addr=INADDR_ANY;
    pthread_rwlock_init(&LockIOData,NULL);
    init_stack(&Stack_IOData);              //栈在使用前需要进行初始化操作

    if((ListenSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP))==INVALID_SOCKET)
    {
        //create socket failed
        return -1;
    }

    if(bind(ListenSocket,(struct sockaddr *)&LocalAddress,sizeof(struct sockaddr_in))!=0)
    {
        //bind address failed
        return -2;
    }

    if(listen(ListenSocket,SOMAXCONN)!=0)
    {
        //listen socket failed
        return -3;
    }

    //So that we can re-bind to it without TIME_WAIT problems
    if(setsockopt(ListenSocket,SOL_SOCKET,SO_REUSEADDR,&reuse_addr,sizeof(reuse_addr))==-1)
    {
        //setsockopt error
        return -4;
    }

    Mempool_IOData=create_mempool(sizeof(IO_OPERATION_DATA),IODATA_MEMPOOL_MAXIMUM_CELL);  //创建IO_OPERATION_DATA结构内存池
    if(Mempool_IOData==NULL)
    {
        printf("Create mempool failed.\n");
        return -4;
    }

    IO_Operation_Data_Header=(IO_OPERATION_DATA_NODE *)malloc(sizeof(IO_OPERATION_DATA_NODE));
    if(IO_Operation_Data_Header==NULL)
    {
        //malloc error
        return -5;
    }
    memset(IO_Operation_Data_Header,NULL,sizeof(IO_OPERATION_DATA_NODE));
    IO_Operation_Data_Header->epollfd=epoll_create(ASYNC_MAX_WAIT_OBJECTS);         //创建epoll描述字
    //新建链表结点后将所有可用位置压入栈
    for(index=0; index<ASYNC_MAX_WAIT_OBJECTS; index++)
    {
        memset(&IODataInfo,NULL,sizeof(STACK_DATA_TYPE));
        IODataInfo.pIONode=IO_Operation_Data_Header;
        IODataInfo.index=index;
        push_stack(&Stack_IOData,IODataInfo);
    }
    createThread=1;   //新建线程开关

    printf("Listen at port %d\n",LISTEN_PORT);
    while(1)
    {
        addrLength=sizeof(ClientAddress);
        memset(&ClientAddress,NULL,addrLength);
        ClientSocket=accept(ListenSocket,(struct sockaddr *)&ClientAddress,&addrLength);
        if(ClientSocket==INVALID_SOCKET) continue;

        setsockopt(ClientSocket,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
        /*
                if((sfd_flags = fcntl(ClientSocket, F_GETFL, 0)) < 0)
                {
                    close(ClientSocket);
                    continue;
                }
                sfd_flags |= O_NONBLOCK;
                if(fcntl(ClientSocket, F_SETFL, sfd_flags) < 0)
                {
                    close(ClientSocket);
                    continue;
                }*/

        if(setsockopt(ClientSocket,IPPROTO_TCP,TCP_NODELAY,(char *)&nagle_flag,sizeof(nagle_flag))==-1)
        {
            printf("Couldn't setsockopt(TCP_NODELAY)\n");
            close(ClientSocket);
            continue;
        }

        pthread_rwlock_wrlock(&LockIOData);     //write lock
skip:
        //在栈中弹出一个可用的位置
        if(pop_stack(&Stack_IOData,&IODataInfo)==STACK_EMPTY)
        {
            //栈中已无可用位置，需要新建一个链表节点和新建一个线程
            for(IO_Operation_Node_Pointer=IO_Operation_Data_Header; IO_Operation_Node_Pointer->next!=NULL; \
                    IO_Operation_Node_Pointer=IO_Operation_Node_Pointer->next);
            IO_Operation_Node_Pointer->next=(IO_OPERATION_DATA_NODE *)malloc(sizeof(IO_OPERATION_DATA_NODE));
            if(IO_Operation_Node_Pointer->next==NULL)
            {
                //malloc error
                pthread_rwlock_unlock(&LockIOData); //unlock
                continue;
            }
            memset(IO_Operation_Node_Pointer->next,NULL,sizeof(IO_OPERATION_DATA_NODE));
            IO_Operation_Node_Pointer->next->epollfd=epoll_create(ASYNC_MAX_WAIT_OBJECTS);
            //新建的所有IO节点压入栈
            for(index=0; index<ASYNC_MAX_WAIT_OBJECTS; index++)
            {
                memset(&IODataInfo,NULL,sizeof(STACK_DATA_TYPE));
                IODataInfo.pIONode=IO_Operation_Node_Pointer->next;
                IODataInfo.index=index;
                push_stack(&Stack_IOData,IODataInfo);
            }
            createThread=1;    //新建线程
            goto skip;
        }

        IO_Operation_Node_Pointer=IODataInfo.pIONode;
        index=IODataInfo.index;
        IO_Operation_Node_Pointer->IOArray[index]=(IO_OPERATION_DATA *)mempool_alloc(Mempool_IOData);   //从内存池中申请空间
        if(IO_Operation_Node_Pointer->IOArray[index]==NULL)
        {
            //malloc error
            pthread_rwlock_unlock(&LockIOData);     //unlock
            continue;
        }
        memset(IO_Operation_Node_Pointer->IOArray[index],NULL,sizeof(IO_OPERATION_DATA));
        //向存储有客户端相关信息的结构体中写入数据
        IO_Operation_Node_Pointer->IOArray[index]->Socket=ClientSocket;
        IO_Operation_Node_Pointer->IOArray[index]->Address=ClientAddress;
        IO_Operation_Node_Pointer->IOArray[index]->listIndex=index;
        IO_Operation_Node_Pointer->IOArray[index]->pIONode=IO_Operation_Node_Pointer;
        IO_Operation_Node_Pointer->IOArray[index]->nextRecvSize=sizeof(PROTO_HEADER);
        IO_Operation_Node_Pointer->IOArray[index]->recvHeader=1;
        IO_Operation_Node_Pointer->IOArray[index]->timestamp=time(NULL);
        pthread_mutex_init(&IO_Operation_Node_Pointer->IOArray[index]->sendMutex,NULL);

        //向epoll中加入一个需要被监听的socket
        memset(&event,NULL,sizeof(struct epoll_event));
        event.events=EPOLLIN | EPOLLERR | EPOLLHUP;
        event.data.fd=ClientSocket;
        if(epoll_ctl(IO_Operation_Node_Pointer->epollfd,EPOLL_CTL_ADD,ClientSocket,&event)!=0)
        {
            //add socket failed
            pthread_rwlock_unlock(&LockIOData);
            continue;
        }

        TotalConnections++;
        if(createThread)
        {
            //create a new listen thread
            ListenThreads++;
            createThread=0;
            if(pthread_create(&threadID,NULL,(void *)listen_socket_from_client,(void *)IO_Operation_Node_Pointer)!=0)
            {
                printf("create thread failed.\n");
            }
        }

        printf("Client \"%s\" online.TotalConnect:%d\n",inet_ntoa(IO_Operation_Node_Pointer->IOArray[index]->Address.sin_addr),TotalConnections);
        pthread_rwlock_unlock(&LockIOData);     //unlock
    }

    return 0;
}

void *clear_zombie_conn_thread(void *para)
{
    int dev;
    IO_OPERATION_DATA_NODE *pIONode=NULL;
    IO_OPERATION_DATA *pIOData=NULL;
    int i;

    while(1)
    {
        sleep(15);
        pthread_rwlock_wrlock(&LockIOData);
        for(pIONode=IO_Operation_Data_Header; pIONode!=NULL; pIONode=pIONode->next)
        {
            for(i=0; i<ASYNC_MAX_WAIT_OBJECTS; i++)
            {
                if(pIONode->IOArray[i]!=NULL)
                {
                    pIOData=pIONode->IOArray[i];
                    dev=time(NULL)-pIOData->timestamp;
                    if(dev>15)
                    {
                        printf("clear a zombie conn.\n");
                        clean_client_connection(pIOData);
                    }
                }
            }
        }
        pthread_rwlock_unlock(&LockIOData);
    }
    return NULL;
}

void deal_sigpipe(int sigNum)
{
    printf("proc recv SIGPIPE.\n");

    return;
}

int ex_send(IO_OPERATION_DATA *pIOData,void *buff,int proto)
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

    pthread_mutex_lock(&pIOData->sendMutex);
    sendSize=send(pIOData->Socket,sendBuffer,protoLen+sizeof(PROTO_HEADER),0);
    pthread_mutex_unlock(&pIOData->sendMutex);

    return sendSize<=0?sendSize:sendSize-sizeof(PROTO_HEADER);
}

int get_proto_len(int proto)
{
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





