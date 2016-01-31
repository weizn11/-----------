#include "thread.h"

pthread_mutex_t Thread::createThreadMutex=PTHREAD_MUTEX_INITIALIZER;

Thread::Thread()
{
    threadPara=NULL;
    runFlag=0;
    pthread_attr_init(&this->threadAttr);
    this->stackSize=10240;  //1Mb
    pthread_attr_setstacksize(&threadAttr,stackSize);

    return;
}

Thread::~Thread()
{
    pthread_attr_destroy(&threadAttr);

    return;
}

int Thread::run(void *Parameter)
{
    int ret;

    if(runFlag) return -1;

    memset(&this->threadID,NULL,sizeof(threadID));

    this->threadPara=Parameter;
    this->runFlag=1;

    pthread_mutex_lock(&createThreadMutex);
    ret=pthread_create(&this->threadID,&threadAttr,start_thread,this);
    pthread_mutex_unlock(&createThreadMutex);

    return ret;
}

void Thread::signal_quit(int signo)
{
    pthread_exit(NULL);
    return;
}

void *Thread::_func(void *threadPara)
{
    void *pRetVal=NULL;

    pRetVal=this->func(threadPara);
    this->runFlag=0;

    return pRetVal;
}

void *Thread::start_thread(void *Parameter)
{
    Thread *_this=(Thread *)Parameter;

    signal(SIGQUIT,_this->signal_quit);
    return _this->_func(_this->threadPara);
}

int Thread::thread_detch()
{
    if(this->runFlag)
        return pthread_detach(this->threadID);
    else
        return -1;
}

int Thread::thread_join(void **ppRetVal)
{
    int ret;
    void *pRetVal=NULL;

    ret=pthread_join(this->threadID,&pRetVal);
    if(ppRetVal!=NULL)
    {
        *ppRetVal=pRetVal;
    }

    return ret;
}

int Thread::thread_quit()
{
    int ret;

    if(this->runFlag)
    {
        //ret=pthread_kill(this->threadID,SIGQUIT);
    }
    else
    {
        ret=0;
    }

    return ret;
}
