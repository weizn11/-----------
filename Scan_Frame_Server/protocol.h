#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

#define CLIENT_NODE_AUTH_KEY "123"
#define CLIENT_MANAGER_AUTH_KEY "321"

enum _Protocol_Type_
{
    E_PROTO_CLIENT_AUTH=0x01,
    E_PROTO_MESSAGE,
    E_PROTO_FILE_TRAN,
    E_PROTO_LAUNCH_TASK,
    E_PROTO_TASK_NODE_INSTR,
    E_PROTO_TASK_TARGET,
    E_PROTO_TASK_REPORT,
    E_PROTO_KEEP_ALIVE,
    E_PROTO_GET_RESULTS,
};

enum _Tran_File_Type_
{
    E_FILE_SCAN_LIST=0x01,
    E_FILE_SCAN_MOD,
    E_FILE_RES,
};

enum _Node_Instruct_
{
    E_TASK_LANUCH=0x01,
    E_TASK_ABORT,
    E_TASK_DISTRI_FIN,
};

//协议头
typedef struct
{
    int protoType;
}PROTO_HEADER;

//发送一段消息
typedef struct
{
    char message[5000];
}PROTO_MESSAGE;

//客户端类型认证协议
typedef struct
{
    int authStatus;
    char authKey[50];
}PROTO_CLIENT_AUTH;

//文件传输协议
typedef struct
{
    int fileType;
    int feof;
    int dataSize;
    char fileName[255];
    char fileData[5000];
}PROTO_FILE_TRAN;

//启动任务（含启动参数）
typedef struct
{
    int subprocNum;
    int threadNum;
    int timeo;
    int reportLevel;
    char modName[255];
    char listName[255];
}PROTO_LAUNCH_TASK,SUB_TASK_INFO;

//分配目标
typedef struct
{
    char target[5000];
}PROTO_TASK_TARGET;

//发送给节点命令
typedef struct
{
    int instruct;
    SUB_TASK_INFO taskInfo;
}PROTO_TASK_NODE_INSTR;

//节点返回给controller的报告
typedef struct
{
    char target[5000];
    int result;
    int taskFin;    //0:task running 1:complete
    int sendFin;    //set:报告发送完成。
    char *reserved;
}PROTO_TASK_REPORT,SCAN_REPORT;

//心跳包
typedef struct
{
    char info[10];
}PROTO_KEEP_ALIVE;

//获取结果报告
typedef struct
{
    char taskName[100];
}PROTO_GET_RESULTS;

#endif // PROTOCOL_H_INCLUDED












