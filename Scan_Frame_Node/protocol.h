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

typedef struct
{
    int protoType;
}PROTO_HEADER;

typedef struct
{
    char message[5000];
}PROTO_MESSAGE;

typedef struct
{
    int authStatus;
    char authKey[50];
}PROTO_CLIENT_AUTH;

typedef struct
{
    int fileType;
    int feof;
    int dataSize;
    char fileName[255];
    char fileData[5000];
}PROTO_FILE_TRAN;

typedef struct
{
    int threadNum;
    char modName[255];
    char listName[255];
}PROTO_LAUNCH_TASK,SUB_TASK_INFO;

typedef struct
{
    char target[5000];
}PROTO_TASK_TARGET;

typedef struct
{
    int instruct;
    SUB_TASK_INFO taskInfo;
}PROTO_TASK_NODE_INSTR;

typedef struct
{
    char target[5000];
    int result;
    int taskFin;    //0:task running 1:complete
}PROTO_TASK_REPORT,SCAN_REPORT;

#endif // PROTOCOL_H_INCLUDED












