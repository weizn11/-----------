#include "exectask.h"

Task *pgl_Task=NULL;
extern ServerComm *pgl_Server_Comm;

Task::Task(SUB_TASK_INFO taskInfo)
{
    this->taskInfo=taskInfo;
    this->taskDistriFinFlag=0;
    pthread_mutex_init(&queueMutex,NULL);
    this->run(NULL);
    this->thread_detch();
    memset(&nodeReport,NULL,sizeof(nodeReport));
    writeLen=0;
    threadTimoCount=0;

    return;
}

Task::~Task()
{
    return;
}

int Task::add_scan_target(PROTO_TASK_TARGET taskTarget)
{
    char *pTarget=NULL;

    pthread_mutex_lock(&queueMutex);
    pTarget=strtok(taskTarget.target,"\n");
    while(pTarget)
    {
        this->scanQueue.push(pTarget);
        //cout <<"add a task to scan queue:"<<pTarget<<endl;
        pTarget=strtok(NULL,"\n");
    }
    pthread_mutex_unlock(&queueMutex);

    return 0;
}

int timestamp;

void *check_subproc_status(void *para)
{
    timestamp=time(NULL);

    while(1)
    {
        if(time(NULL)-timestamp>20)
        {
            exit(-1);
        }
    }
}

int Task::task_proc(int fpr,int fpw)
{
    string target;
    ScanThread *pScan=NULL;
    int ret;
    char pwd[1000],pyenv[1000];
    fd_set fdRead;
    struct timeval timeo;
    PIPE_DATA rdPipeData,wrPipeData;
    int taskRequestFlag=0,taskFinFlag=0;
    int activeCount;
    pthread_t threadID;

    memset(pwd,NULL,sizeof(pwd));
    memset(pyenv,NULL,sizeof(pyenv));

    //setenv("PYTHONPATH", "$PYTHONPATH:./", 1);
    getcwd(pwd,sizeof(pwd));
    Py_Initialize();
    PyEval_InitThreads();
    PyEval_ReleaseLock();
    PyRun_SimpleString("import sys");
    sprintf(pyenv,"sys.path.append('%s')",pwd);
    PyRun_SimpleString(pyenv);
    PyRun_SimpleString("sys.path.append(\".\")");

    pthread_create(&threadID,NULL,check_subproc_status,NULL);
    pthread_create(&threadID,NULL,output_log,NULL);

    while(1)
    {
        if(this->scan_thread_list_join(fpw)!=0)   //clear complete thread
        {
            ;
        }

        //request task
        if(this->threadList.size()<SUBPROC_MAX_THREAD_OBJ-1 && taskRequestFlag==0 && \
                this->threadTimoCount<SUBPROC_MAX_THREAD_OBJ/2)
        {
            memset(&wrPipeData,NULL,sizeof(wrPipeData));
            wrPipeData.instr=E_PIPE_TASK_REQUEST;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc write pipe failed."<<endl;
                exit(-1);
            }
            //cout <<"request send success."<<endl;
            taskRequestFlag=1;
        }

        //task complete
        if(threadList.size()<1 && taskFinFlag==0)
        {
            //task complete
            memset(&wrPipeData,NULL,sizeof(wrPipeData));
            wrPipeData.instr=E_PIPE_TASK_FIN;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc write pipe failed."<<endl;
                exit(-1);
            }
            taskFinFlag=1;
        }

        FD_ZERO(&fdRead);
        memset(&timeo,NULL,sizeof(timeo));
        timeo.tv_usec=1000;
        FD_SET(fpr,&fdRead);

        if((activeCount=select(fpr+1,&fdRead,NULL,NULL,&timeo))<=0)
        {
            continue;
        }

        memset(&rdPipeData,NULL,sizeof(rdPipeData));

        if(read(fpr,&rdPipeData,sizeof(rdPipeData))<=0)
        {
            cout <<"subproc read pipe failed."<<endl;
            exit(-1);
        }
        //cout <<getpid()<<" recv task target:"<<rdPipeData.data<<endl;

        switch(rdPipeData.instr)
        {
        case E_PIPE_TASK_TARGET:
            taskRequestFlag=0;
            taskFinFlag=0;
            if(threadList.size()>=SUBPROC_MAX_THREAD_OBJ)
            {
                cout <<"Beyond the maximum of thread."<<endl;
                break;
            }

            pScan=new ScanThread(rdPipeData.data,taskInfo.modName);
            if(pScan==NULL)
            {
                cout <<"new obj error."<<endl;
                break;
            }
            if((ret=pScan->run(NULL))!=0)
            {
                cout <<"pthread_create() error.code:"<<ret<<endl;
                delete pScan;
                break;
            }
            else
            {
                threadList.push_back(pScan);
            }
            break;
        case E_PIPE_TASK_RECV_FIN:
            this->taskDistriFinFlag=1;
            break;
        case E_PIPE_PROC_KEEP:
            timestamp=time(NULL);
            break;
        }
    }

    Py_Finalize();

    return 0;
}

void sig_wait(int sigNum)
{
    wait((int *)0);
}

void *Task::func(void *para)
{

    TASK_PROC taskProc[20];
    int i,j,wrSize;
    fd_set fdRead;
    struct timeval timeo;
    int activeCount;
    PIPE_DATA rdPipeData,wrPipeData;
    string target;
    int nfds;
    int subprocRet;
    int informDistriFin=0;
    int timestamp;
    int exitpid,exitret;
    int readAgain=0;

    memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
    this->logTime=0;
    timestamp=time(NULL);

    //signal(SIGCHLD,sig_wait);

    taskInfo.threadNum/=SUBPROC_MAX_THREAD_OBJ;
    if(taskInfo.threadNum>20) taskInfo.threadNum=20;
    if(taskInfo.threadNum<=0) taskInfo.threadNum=1;
    for(i=0; i<taskInfo.threadNum; i++)
    {
        create_subproc(&taskProc[i]);
    }

    while(1)
    {
skip:
        exitpid=waitpid(-1,&exitret,WNOHANG);
        if(exitpid>0)
        {
            cout <<"pid:"<<exitpid<<" subproc exit.code:"<<exitret<<endl;
        }

        if(time(NULL)-logTime>5)
        {
            printf("queue size:%d\tfinFlag:%d\n",scanQueue.size(),taskDistriFinFlag);
            logTime=time(NULL);
        }

        if(time(NULL)-timestamp>3)
        {
            for(j=0; j<taskInfo.threadNum; j++)
            {
                if(taskProc[j].pid>0)
                {
                    memset(&wrPipeData,NULL,sizeof(wrPipeData));
                    wrPipeData.instr=E_PIPE_PROC_KEEP;
                    if((wrSize=write(taskProc[j].pipe[1],&wrPipeData,sizeof(wrPipeData)))<=0)
                    {
                        cout <<"update timestamp failed."<<endl;
                        if(wrSize==-1 && errno==EAGAIN && taskProc[j].blockCount<10000)
                        {
                            cout <<"write block."<<endl;
                            taskProc[j].blockCount++;
                        }
                        else
                        {
                            kill(taskProc[j].pid,SIGKILL);
                            close(taskProc[j].pipe[0]);
                            close(taskProc[j].pipe[1]);
                            create_subproc(&taskProc[j]);
                            goto skip;
                        }
                    }
                    else
                    {
                        taskProc[j].blockCount=0;
                    }
                }
            }
            timestamp=time(NULL);
        }

        //distr task
        for(j=0; j<taskInfo.threadNum; j++)
        {
            if(taskProc[j].pid>0 && taskProc[j].reqTask)
            {
                pthread_mutex_lock(&queueMutex);
                if(!scanQueue.empty())
                {
                    memset(&wrPipeData,NULL,sizeof(wrPipeData));

                    target=scanQueue.front();
                    strcat(wrPipeData.data,target.c_str());
                    wrPipeData.instr=E_PIPE_TASK_TARGET;
                    if((wrSize=write(taskProc[j].pipe[1],&wrPipeData,sizeof(wrPipeData)))<=0)
                    {
                        cout <<"distri task failed."<<endl;
                        if(wrSize==-1 && errno==EAGAIN && taskProc[j].blockCount<10000)
                        {
                            cout <<"write block."<<endl;
                            taskProc[j].blockCount++;
                        }
                        else
                        {
                            kill(taskProc[j].pid,SIGKILL);
                            close(taskProc[j].pipe[0]);
                            close(taskProc[j].pipe[1]);
                            create_subproc(&taskProc[j]);
                            pthread_mutex_unlock(&queueMutex);
                            goto skip;
                        }
                    }
                    else
                    {
                        scanQueue.pop();
                        taskProc[j].reqTask=0;
                        taskProc[j].fin=0;
                        taskProc[j].blockCount=0;
                    }
                }
                pthread_mutex_unlock(&queueMutex);
            }
        }

        if(informDistriFin==0 && this->taskDistriFinFlag)
        {
            memset(&wrPipeData,NULL,sizeof(wrPipeData));

            wrPipeData.instr=E_PIPE_TASK_RECV_FIN;
            for(j=0; j<taskInfo.threadNum; j++)
            {
                if(taskProc[j].pid>0)
                {
                    if((wrSize=write(taskProc[j].pipe[1],&wrPipeData,sizeof(wrPipeData)))<=0)
                    {
                        cout <<"inform fin failed."<<endl;
                        if(wrSize==-1 && errno==EAGAIN && taskProc[j].blockCount<10000)
                        {
                            cout <<"write block."<<endl;
                            taskProc[j].blockCount++;
                        }
                        else
                        {
                            kill(taskProc[j].pid,SIGKILL);
                            close(taskProc[j].pipe[0]);
                            close(taskProc[j].pipe[1]);
                            create_subproc(&taskProc[j]);
                            goto skip;
                        }
                    }
                    else
                    {
                        taskProc[j].blockCount=0;
                    }
                }
            }
            informDistriFin=1;
        }

        if(this->taskDistriFinFlag && this->scanQueue.empty())
        {
            for(j=0; j<taskInfo.threadNum; j++)
            {
                if(taskProc[j].fin)
                {
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    memset(&taskProc[j],NULL,sizeof(TASK_PROC));
                    cout <<"subproc exit."<<endl;
                }
            }

            for(i=0; i<taskInfo.threadNum; i++)
            {
                if(taskProc[i].pid!=0) break;
            }
            if(i==taskInfo.threadNum)
            {
                //all task complete
                if(strlen(this->nodeReport.target)>0)
                {
                    while(pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT)<=0)
                    {
                        //resend
                        cout <<"send task report failed."<<endl;
                        sleep(10);
                    }
                }

                memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                this->nodeReport.taskFin=1;

                while(pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT)<=0)
                {
                    //resend
                    cout <<"send task report failed."<<endl;
                    sleep(10);
                }

                cout <<"destory task obj."<<endl;
                pgl_Task=NULL;
                delete this;
                return NULL;
            }
        }

read_again:
        FD_ZERO(&fdRead);
        readAgain=0;
        memset(&timeo,NULL,sizeof(timeo));
        timeo.tv_usec=1000;
        for(i=0,nfds=0; i<taskInfo.threadNum; i++)
        {
            if(taskProc[i].pid==0) continue;

            FD_SET(taskProc[i].pipe[0],&fdRead);
            if(taskProc[i].pipe[0]>nfds)
            {
                nfds=taskProc[i].pipe[0];
            }
        }

        if((activeCount=select(nfds+1,&fdRead,NULL,NULL,&timeo))<=0)
        {
            continue;
        }

        for(j=0; j<taskInfo.threadNum; j++)
        {
            if(FD_ISSET(taskProc[j].pipe[0],&fdRead))
            {
                memset(&rdPipeData,NULL,sizeof(rdPipeData));
                if(read(taskProc[j].pipe[0],&rdPipeData,sizeof(rdPipeData))<=0)
                {
                    cout <<"read pipe failed."<<j<<endl;
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    create_subproc(&taskProc[j]);
                    continue;
                }

                readAgain++;
                switch(rdPipeData.instr)
                {
                case E_PIPE_TASK_REQUEST:
                    taskProc[j].reqTask=1;
                    break;
                case E_PIPE_TASK_REPORT:
                    //cout <<rdPipeData.data<<endl;

                    sprintf(this->nodeReport.target+writeLen,"%s\n",rdPipeData.data);
                    wrSize=strlen(this->nodeReport.target+writeLen);
                    writeLen+=wrSize;
                    if(sizeof(this->nodeReport.target)-writeLen<255)
                    {
                        writeLen=0;
                        while(pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT)<=0)
                        {
                            //resend
                            cout <<"send task report failed."<<endl;
                            sleep(10);
                        }
                        memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                    }
                    break;
                case E_PIPE_PROC_RESTART:
                    kill(taskProc[j].pid,SIGKILL);
                    memset(&taskProc[j],NULL,sizeof(TASK_PROC));
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);

                    this->create_subproc(&taskProc[j]);
                    cout <<"subproc restart."<<endl;
                    break;
                case E_PIPE_TASK_FIN:
                    taskProc[j].fin=1;
                    break;
                case E_PIPE_PROC_ABORT:
                    kill(taskProc[j].pid,SIGKILL);
                    memset(&taskProc[j],NULL,sizeof(TASK_PROC));
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    break;
                }//end switch
            }
        }
        goto read_again;
    }

    return NULL;
}

int Task::create_subproc(struct _Task_Proc_ *taskProc)
{
    int fpw[2],fpr[2];
    int flags;

    memset(taskProc,NULL,sizeof(struct _Task_Proc_));

    if(pipe(fpw)!=0 || pipe(fpr)!=0)
    {
        cout <<"create pipe failed."<<endl;
        return -1;
    }

    taskProc->pid=fork();
    if(taskProc->pid<0)
    {
        cout <<"create child process failed."<<endl;
        return -1;
    }
    taskProc->pipe[0]=fpr[0];
    taskProc->pipe[1]=fpw[1];

    flags=fcntl(taskProc->pipe[1],F_GETFL);
    fcntl(taskProc->pipe[1],F_SETFL, flags | O_NONBLOCK); // 设置为非阻塞

    if(taskProc->pid==0)
    {
        //child proc
        close(fpw[1]);
        close(fpr[0]);

        setsid();

        this->task_proc(fpw[0],fpr[1]);

        exit(0);
    }
    close(fpw[0]);
    close(fpr[1]);
    cout <<"new subproc pid :"<<taskProc->pid<<endl;

    return 0;
}

int Task::scan_thread_list_join(int fpw)
{
    list<ScanThread *>::iterator iter;
    ScanThread *pThread=NULL;
    SCAN_REPORT *pReport=NULL;
    int wrSize;
    PIPE_DATA wrPipeData;

    for(iter=threadList.begin(); iter!=threadList.end();)
    {
        pThread=*iter;
        if(pThread->finish())
        {
            //thread finish
            threadList.erase(iter);
            iter=threadList.begin();

            pThread->thread_join((void **)&pReport);
            //cout <<"scan thread "<<pReport->target<<" exit"<<endl;

            if(pReport->result)
            {
                //available
                memset(&wrPipeData,NULL,sizeof(wrPipeData));
                wrPipeData.instr=E_PIPE_TASK_REPORT;
                strcat(wrPipeData.data,pReport->target);
                if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
                {
                    cout <<"subproc write pipe failed."<<endl;
                    exit(-1);
                }
            }
            delete pThread;
            continue;
        }
        iter++;
    }

    for(iter=threadList.begin(),this->threadTimoCount=0; iter!=threadList.end(); iter++)
    {
        pThread=*iter;
        if(this->taskInfo.timeo!=0 && (time(NULL)-pThread->timestamp>this->taskInfo.timeo))
        {
            //thread timeout
            this->threadTimoCount++;
        }
    }

    if(this->threadTimoCount>=SUBPROC_MAX_THREAD_OBJ/2 || (this->taskDistriFinFlag && this->threadTimoCount>0))
    {
        for(iter=threadList.begin(); iter!=threadList.end(); iter++)
        {
            pThread=*iter;
            if(this->taskInfo.timeo!=0 && (time(NULL)-pThread->timestamp>this->taskInfo.timeo))
            {
                ;//timeout
            }
            else
            {
                break;
            }
        }
        if(iter==threadList.end())
        {
            memset(&wrPipeData,NULL,sizeof(wrPipeData));
            if(this->threadTimoCount>=SUBPROC_MAX_THREAD_OBJ/2)
            {
                wrPipeData.instr=E_PIPE_PROC_RESTART;
                if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
                {
                    cout <<"subproc write pipe failed."<<endl;
                    exit(-1);
                }
            }
            else if(this->taskDistriFinFlag)
            {
                wrPipeData.instr=E_PIPE_TASK_FIN;
                if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
                {
                    cout <<"subproc write pipe failed."<<endl;
                    exit(-1);
                }
            }
        }
    }

    return 0;
}

ScanThread::ScanThread(string target,string modName)
{
    memset(&report,NULL,sizeof(report));
    this->target=target;
    this->modName=modName;
    this->timestamp=time(NULL);

    return;
}

ScanThread::~ScanThread()
{
    return;
}

void *ScanThread::func(void *para)
{
    //scan function
    char modPath[1000],appStr[1000];
    PyObject *pModule=NULL,*pFunc=NULL;
    PyObject *pArgs=NULL,*pValue=NULL;

    memset(modPath,NULL,sizeof(modPath));
    memset(appStr,NULL,sizeof(appStr));
    memset(&report,NULL,sizeof(report));
    strcat(report.target,target.c_str());

    PyGILState_STATE state = PyGILState_Ensure();
    //  导入Python模块..
    modName[modName.find('.')]=NULL;
    pModule=PyImport_Import(PyString_FromString(modName.c_str()));
    if (!pModule)
    {
        printf("import '%s' error\n",modName.c_str());
        goto skip;
    }

    // 获取py_test中的py_func函数..
    pFunc=PyObject_GetAttrString(pModule,"scan_target");
    if (!pFunc)
    {
        printf("get function error\n");
        Py_DECREF(pModule);
        goto skip;
    }
    // 设置参数...
    pArgs=PyTuple_New(1); // PyTuple == Python中的元组..
    PyTuple_SetItem(pArgs,0,PyString_FromString(report.target));

    pValue=PyObject_CallObject(pFunc,pArgs);
    if (!pValue)
    {
        //printf("call function error\n");
        Py_DECREF(pModule);
        Py_DECREF(pFunc);
        Py_DECREF(pArgs);
        goto skip;
    }

    report.result=PyInt_AsLong(pValue);

    Py_DECREF(pModule);
    Py_DECREF(pFunc);
    Py_DECREF(pArgs);
    Py_DECREF(pValue);
skip:
    PyGILState_Release(state);

    return &report;
}

int ScanThread::finish()
{
    return this->runFlag?0:1;
}









