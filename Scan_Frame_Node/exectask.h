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
#include<sys/wait.h>

#include "protocol.h"
#include "thread.h"
#include "comm.h"

using namespace std;

class ScanThread : public Thread
{
public:
    ScanThread(string target,string modName);
    ~ScanThread();
    void *func(void *para);
    int finish();

private:
    string target;
    string modName;
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
    int scan_thread_list_join();
private:
    SUB_TASK_INFO taskInfo;
    queue<string> scanQueue;
    list<ScanThread *> threadList;
    pthread_mutex_t queueMutex;
    pthread_mutex_t listMutex;
    SCAN_REPORT nodeReport;
    int writeLen;
};

#endif // EXECTASK_H_INCLUDED
