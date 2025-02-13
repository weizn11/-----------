#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <pthread.h>
#include <signal.h>
#include <string.h>

class Thread
{
public:
    Thread();
    ~Thread();
    int run(void *Parameter);
    int thread_join(void **ppRetVal);
    int thread_detch();
protected:
    virtual void *func(void *threadPara)=0;
    void *_func(void *threadPara);
    static void *start_thread(void *Parameter);
protected:
    static pthread_mutex_t createThreadMutex;
    pthread_t threadID;
    pthread_attr_t threadAttr;
    void *threadPara;
    int runFlag;
    //int detchFlag;
    int stackSize;
};


#endif // THREAD_H_INCLUDED
