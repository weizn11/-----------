#ifndef COMM_H_INCLUDED
#define COMM_H_INCLUDED

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "cmdopt.h"
#include "protocol.h"
#include "thread.h"
#include "exectask.h"

#define SOCKET int
#define INVALID_SOCKET -1

class KeepAlive;
class ServerComm
{
public:
    ServerComm(CMD_OPTIONS &options);
    int connect_to_server();
    int reconnect();
    int keep_alive();
    int upload_scan_module();
    int launch_scan_task();
    int run_recv_thread();
    int abort_scan_task();
    int ex_send(void *buff,int proto);
public:
    int clientType; //1:node 2:manager
private:
    int client_auth();
    int upload_file(FILE *srcFile,char *fileName,int fileType);
private:
    SOCKET soc;
    CMD_OPTIONS options;
    struct sockaddr_in addr;
    pthread_mutex_t sendMutex;
    KeepAlive *pAliveObj;
    int reconnFlag;
};

class RecvThread : public Thread
{
public:
    RecvThread(SOCKET soc);
    int update_socket(SOCKET soc);
protected:
    void *func(void *Parameter);
private:
    int recv_message_from_server();
    int deal_protocol_data(int proto);
private:
    SOCKET soc;
    char *recvBuffer;
    int recvSize; //already recv size
    int nextRecvSize; //need recv size
    int recvHeader;
    FILE *dstFile;
};

class KeepAlive : public Thread
{
public:
    KeepAlive();
    void *func(void *para);
};

typedef struct
{
    int protocol;
    void *pBuffer;
}RECV_EVENT;

void *wait_event(int protocol,int timeo);
int get_proto_len(int proto);
int restart_proc();
void *output_log(void *para);
void deal_sigterm(int sigNum);

#endif // COMM_H_INCLUDED
