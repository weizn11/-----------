#ifndef EXECTASK_H_INCLUDED
#define EXECTASK_H_INCLUDED

#include <queue>
#include <list>
#include <string.h>
#include <string>
#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <python2.7/Python.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>

#include "protocol.h"
#include "thread.h"
#include "comm.h"

using namespace std;

class ScanThread : public Thread
{
public:
    ScanThread(string target,string modName,int reportLevel);
    ~ScanThread();
    void *func(void *para);
    int finish();
public:
    int timestamp;
private:
    string target;
    string modName;
    int reportLevel;
    SCAN_REPORT report;
};

class Task : public Thread
{
public:
    Task(SUB_TASK_INFO taskInfo);
    ~Task();
    int add_scan_target(PROTO_TASK_TARGET taskTarget);
    void *func(void *para);
public:
    int taskDistriFinFlag;
private:
    int scan_thread_list_join(int fpw);
    int task_proc(int fpr,int fpw);
    int create_subproc(struct _Task_Proc_ *taskProc);
private:
    SUB_TASK_INFO taskInfo;
    queue<string> scanQueue;
    list<ScanThread *> threadList;
    pthread_mutex_t queueMutex;
    pthread_mutex_t listMutex;
    SCAN_REPORT nodeReport;
    int writeLen;
    int threadTimoCount;
    int logTime;
    int informTaskFin;
};

class SendReport : public Thread
{
public:
    SendReport();
    ~SendReport();
    int push_report(SCAN_REPORT report);
    int lock();
    int unlock();
    void *func(void *para);
    int empty();
private:
    queue<SCAN_REPORT> reportQueue;
    pthread_rwlock_t rwlock;
};

typedef struct
{
    int instr;           //数据类型
    int reportFin;       //结束接收报告标识
    char data[PIPE_BUF-40];
}PIPE_DATA;

typedef struct _Task_Proc_
{
    int pid;
    int pipe[2];
    int reqTask;
    int fin;
    int blockCount;
    int timestamp;
    int rdSize;
    vector<SCAN_REPORT> vecReport;
}TASK_PROC;

enum _Pipe_Data_
{
    E_PIPE_TASK_TARGET=0x01,
    E_PIPE_TASK_REPORT,
    E_PIPE_PROC_RESTART,
    E_PIPE_PROC_ABORT,
    E_PIPE_PROC_KEEP,
    E_PIPE_TASK_REQUEST,
    E_PIPE_TASK_FIN,
    E_PIPE_TASK_RECV_FIN,
};

#endif // EXECTASK_H_INCLUDED











