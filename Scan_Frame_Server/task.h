#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

#include <stdio.h>

typedef struct
{
    int status;     //1:busy 0:free
    int except;
    int taskNode;
    int threadNum;
    char modName[255];
    char listName[255];
    FILE *resFile;
    pthread_mutex_t fileMutex;
}LANUCH_TASK_INFO;

void *distribute_task(void *para);

#endif // TASK_H_INCLUDED
