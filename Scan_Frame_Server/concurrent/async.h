#ifndef ASYNC_H_INCLUDED
#define ASYNC_H_INCLUDED

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../protocol.h"

#define SOCKET int
#define INVALID_SOCKET -1
#define LISTEN_PORT 23
#define SOCKET_TIMEOUT 100      //epoll_wait超时时间
#define MAX_RECV_SIZE 2048
#define ASYNC_MAX_WAIT_OBJECTS 2048     //一个监听线程最大服务的对象数量

/*
IO_OPERATION_DATA结构体用来存放每个客户端连接的相关信息及数据
Socket:与客户端连接的套接字
Address:客户端地址
recvBuffer:指向存放接收到客户端传来数据的内存
recvSize:当前recvBuffer已接收到的数据长度
*/
typedef struct
{
    SOCKET Socket;
    struct sockaddr_in Address;
    int listIndex;
    struct _IO_OPERATION_DATA_LIST_NODE_ *pIONode;
    char *recvBuffer;
    unsigned long recvSize;
    unsigned long nextRecvSize;
    int recvHeader;
    int protocol;
    int clientType; //1:node  2:manager
    pthread_mutex_t sendMutex;
    FILE *modFile;
    char scanList[255];
    char scanMod[255];
    int except;
    int timestamp;
    char *reportBuf;    //返回报告临时存放buf
} IO_OPERATION_DATA;

/*
IO_OPERATION_DATA_NODE结构体用来保存一组客户端信息结构体和epoll描述字，也是一个线程基本的操作单位
epollfd:epoll描述字
IOArray:存放指向IO_OPERATION_DATA的指针数组
next:指向下一个链表节点
*/
typedef struct _IO_OPERATION_DATA_LIST_NODE_
{
    int epollfd;
    IO_OPERATION_DATA *IOArray[ASYNC_MAX_WAIT_OBJECTS];
    struct _IO_OPERATION_DATA_LIST_NODE_ *next;
} IO_OPERATION_DATA_NODE;

int listen_client();
int ex_send(IO_OPERATION_DATA *pIOData,void *buff,int proto);
int get_proto_len(int proto);
void deal_sigpipe(int sigNum);
void *clear_zombie_conn_thread(void *para);

#endif // ASYNC_H_INCLUDED
