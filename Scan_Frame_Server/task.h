#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

#include <stdio.h>
#include "concurrent/async.h"

typedef struct
{
    int status;     //1:busy 0:free
    int except;
    int exit;
    int taskNode;
    int subprocNum;
    int threadNum;
    int reportLevel;
    int timeo;
    char modName[255];
    char listName[255];
    FILE *resFile;
}LANUCH_TASK_INFO;

void *distribute_task(void *para);
void *get_report_thread(void *para);
int send_data_to_client(void *buff,int proto,int clientType,int closeFlag);

#endif // TASK_H_INCLUDED
