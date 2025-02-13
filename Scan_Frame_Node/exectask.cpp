#include "exectask.h"

Task *pgl_Task=NULL;
extern ServerComm *pgl_Server_Comm;
SendReport *pgl_Send_Report=NULL;

Task::Task(SUB_TASK_INFO taskInfo)
{
    this->taskInfo=taskInfo;
    this->taskDistriFinFlag=0;
    this->informTaskFin=0;
    pthread_mutex_init(&queueMutex,NULL);
    this->run(NULL);
    this->thread_detch();
    memset(&nodeReport,NULL,sizeof(nodeReport));
    writeLen=0;
    threadTimoCount=0;
    if(pgl_Send_Report==NULL)
    {
        pgl_Send_Report=new SendReport();
    }

    return;
}

Task::~Task()
{
    return;
}

int Task::add_scan_target(PROTO_TASK_TARGET taskTarget)
{
    //添加到扫描队列
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

int rdTimestamp=0;

void *check_subproc_status(void *para)
{
    //子进程检查自己的时间戳是否更新，超时则退出子进程
    ::rdTimestamp=time(NULL);

    while(1)
    {
        if(time(NULL)-::rdTimestamp>60)
        {
            exit(-1);
        }
        sleep(3);
    }
}

int Task::task_proc(int fpr,int fpw)
{
    //子进程主线程
    string target;
    ScanThread *pScan=NULL;
    int ret;
    char pwd[1000],pyenv[1000];
    fd_set fdRead;
    struct timeval timeo;
    PIPE_DATA rdPipeData,wrPipeData;
    int taskRequestFlag=0;
    int activeCount;
    pthread_t threadID;
    int wrTimestamp=0;
    int rdSize=0,totalSize=0;
    //char *pRdBuff=NULL;

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

    while(1)
    {
        //清除已完成的扫描线程
        if(this->scan_thread_list_join(fpw)!=0)
        {
            ;
        }

        //向父进程发送心跳包
        if(time(NULL)-wrTimestamp>3)
        {
            memset(&wrPipeData,NULL,sizeof(wrPipeData));
            wrPipeData.instr=E_PIPE_PROC_KEEP;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc update wrTimestamp failed."<<endl;
                exit(-1);
            }
            wrTimestamp=time(NULL);
        }

        //请求一个扫描任务
        if(this->threadList.size()<taskInfo.threadNum && taskRequestFlag==0 && \
                (this->taskInfo.timeo==0?1:this->threadTimoCount<(taskInfo.threadNum%2==0?taskInfo.threadNum/2:(taskInfo.threadNum+1)/2)))
        {
            //cout <<"this->threadList.size():"<<this->threadList.size()<<" and taskInfo.threadNum:"<<taskInfo.threadNum<<endl;
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

recv_again:
        //接收来自父进程的消息
        FD_ZERO(&fdRead);
        memset(&timeo,NULL,sizeof(timeo));
        timeo.tv_usec=100;
        FD_SET(fpr,&fdRead);

        if((activeCount=select(fpr+1,&fdRead,NULL,NULL,&timeo))<=0)
        {
            continue;
        }

        memset(&rdPipeData,NULL,sizeof(rdPipeData));

        if((rdSize=read(fpr,(char *)&rdPipeData+totalSize,sizeof(rdPipeData)-totalSize))<=0)
        {
            cout <<"subproc read pipe failed."<<endl;
            exit(-1);
        }
        //cout <<getpid()<<" recv task target:"<<rdPipeData.data<<endl;

        totalSize+=rdSize;
        if(totalSize==sizeof(rdPipeData))
        {
            totalSize=0;
        }
        else
        {
            continue;
        }
        switch(rdPipeData.instr)
        {
        case E_PIPE_TASK_TARGET:
            //新的扫描目标
            taskRequestFlag=0;
            this->informTaskFin=0;
            if(threadList.size()>=taskInfo.threadNum)
            {
                cout <<"Beyond the maximum of threads."<<"threadList.size():"<<threadList.size()<<endl;
                break;
            }

            pScan=new ScanThread(rdPipeData.data,taskInfo.modName,taskInfo.reportLevel);
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
                pScan=NULL;
            }
            break;
        case E_PIPE_TASK_RECV_FIN:
            //从服务端已接收完扫描任务通知
            this->taskDistriFinFlag=1;
            break;
        case E_PIPE_PROC_KEEP:
            //父进程发送来的心跳包
            ::rdTimestamp=time(NULL);
            break;
        default:
            exit(-1);
        }
        goto recv_again;
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
    //父进程任务主线程
    TASK_PROC taskProc[20];
    char tempStr[5000];
    int i,j,wrSize,rdSize;
    fd_set fdRead;
    struct timeval timeo;
    int activeCount;
    PIPE_DATA rdPipeData,wrPipeData;
    string target;
    int nfds;
    int subprocRet;
    int timestamp;
    int exitpid,exitret;
    vector<SCAN_REPORT>::iterator iter;

    memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
    memset(taskProc,NULL,sizeof(taskProc));
    this->logTime=0;
    writeLen=0;
    timestamp=time(NULL);

    //signal(SIGCHLD,sig_wait);

    if(taskInfo.subprocNum>20) taskInfo.subprocNum=20;
    if(taskInfo.subprocNum<1) taskInfo.subprocNum=1;
    if(taskInfo.threadNum>200) taskInfo.threadNum=200;
    if(taskInfo.threadNum<1) taskInfo.threadNum=1;

    //创建子进程
    for(i=0; i<taskInfo.subprocNum; i++)
    {
        create_subproc(&taskProc[i]);
    }

    while(1)
    {
        do
        {
            //回收僵尸进程
            exitpid=waitpid(-1,&exitret,WNOHANG);
            if(exitpid>0)
            {
                cout <<"pid:"<<exitpid<<" subproc exit.code:"<<exitret<<endl;
            }
        }
        while(exitpid>0);

        //每五秒输出一次日志
        if(time(NULL)-logTime>5)
        {
            printf("queue size:%d\tfinFlag:%d\n",scanQueue.size(),taskDistriFinFlag);
            logTime=time(NULL);
        }

        //每三秒向所有子进程发送一次心跳包
        if(time(NULL)-timestamp>3)
        {
            for(j=0; j<taskInfo.subprocNum; j++)
            {
                if(taskProc[j].pid>0)
                {
                    memset(&wrPipeData,NULL,sizeof(wrPipeData));
                    wrPipeData.instr=E_PIPE_PROC_KEEP;
                    if((wrSize=write(taskProc[j].pipe[1],&wrPipeData,sizeof(wrPipeData)))<=0)
                    {
                        cout <<"update timestamp failed."<<endl;
                        if(wrSize==-1 && errno==EAGAIN)
                        {
                            cout <<"write block."<<endl;
                            taskProc[j].blockCount++;
                        }
                        else
                        {
                            cout <<"return code:"<<wrSize<<" errno:"<<errno<<endl;
                            kill(taskProc[j].pid,SIGKILL);
                            close(taskProc[j].pipe[0]);
                            close(taskProc[j].pipe[1]);
                            create_subproc(&taskProc[j]);
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

        //检查子进程的心跳是否存活
        for(j=0; j<taskInfo.subprocNum; j++)
        {
            if(taskProc[j].pid>0 && time(NULL)-taskProc[j].timestamp>60)
            {
                cout <<"subproc num:"<<j<<" timeout.Difftime :"<<time(NULL)-taskProc[j].timestamp<<endl;
                kill(taskProc[j].pid,SIGKILL);
                close(taskProc[j].pipe[0]);
                close(taskProc[j].pipe[1]);
                create_subproc(&taskProc[j]);
            }
        }

        //给子进程分配任务
        for(j=0; j<taskInfo.subprocNum; j++)
        {
            if(taskProc[j].pid>0 && taskProc[j].reqTask!=0)
            {
                pthread_mutex_lock(&queueMutex);
                if(!scanQueue.empty())
                {
                    memset(&wrPipeData,NULL,sizeof(wrPipeData));
                    memset(tempStr,NULL,sizeof(tempStr));
                    target=scanQueue.front();
                    strcat(tempStr,target.c_str());
                    if(strlen(tempStr)<sizeof(wrPipeData.data)-1)
                    {
                        memcpy(wrPipeData.data,tempStr,sizeof(wrPipeData.data)-2);
                        wrPipeData.instr=E_PIPE_TASK_TARGET;
                        if((wrSize=write(taskProc[j].pipe[1],&wrPipeData,sizeof(wrPipeData)))<=0)
                        {
                            cout <<"distri task to subproc failed."<<endl;
                            if(wrSize==-1 && errno==EAGAIN)
                            {
                                cout <<"write block."<<endl;
                                taskProc[j].blockCount++;
                                pthread_mutex_unlock(&queueMutex);
                                continue;
                            }
                            else
                            {
                                cout <<"closed subproc."<<" return code:"<<wrSize<<" errno:"<<errno<<endl;
                                kill(taskProc[j].pid,SIGKILL);
                                close(taskProc[j].pipe[0]);
                                close(taskProc[j].pipe[1]);
                                create_subproc(&taskProc[j]);
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
                }
                pthread_mutex_unlock(&queueMutex);
            }
        }

        //所有任务接收完并分配完，当子进程完成自己的任务后便结束它
        if(this->taskDistriFinFlag && this->scanQueue.empty())
        {
            for(j=0; j<taskInfo.subprocNum; j++)
            {
                //关闭已完成所有任务线程的子进程
                if(taskProc[j].fin)
                {
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    memset(&taskProc[j],NULL,sizeof(TASK_PROC));
                    cout <<"subproc exit."<<endl;
                }
            }

            for(i=0; i<taskInfo.subprocNum; i++)
            {
                if(taskProc[i].pid>0) break;
            }
            if(i==taskInfo.subprocNum)
            {
                //all task complete
                //printf("last report:%s\n",this->nodeReport.target);
                if(strlen(this->nodeReport.target)>0)
                {
                    //发送最后的扫描报告
                    pgl_Send_Report->lock();
                    pgl_Send_Report->push_report(this->nodeReport);
                    pgl_Send_Report->unlock();
                }

                //等待所有报告发送完毕
                while(!pgl_Send_Report->empty()) sleep(1);

                //通知服务器节点任务已完成
                memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                this->nodeReport.taskFin=1;
                while(pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT)<=0)
                {
                    //resend
                    cout <<"send task report failed."<<endl;
                    sleep(10);
                }

                do
                {
                    //回收僵尸进程
                    exitpid=waitpid(-1,&exitret,WNOHANG);
                    if(exitpid>0)
                    {
                        cout <<"pid:"<<exitpid<<" subproc exit.code:"<<exitret<<endl;
                    }
                }
                while(exitpid>0);

                //销毁自己的任务对象
                cout <<"destory task obj."<<endl;
                pgl_Task=NULL;
                delete this;
                return NULL;
            }
        }

        do
        {
            //回收僵尸进程
            exitpid=waitpid(-1,&exitret,WNOHANG);
            if(exitpid>0)
            {
                cout <<"pid:"<<exitpid<<" subproc exit.code:"<<exitret<<endl;
            }
        }
        while(exitpid>0);

        //接收来自子进程的消息
        FD_ZERO(&fdRead);
        memset(&timeo,NULL,sizeof(timeo));
        timeo.tv_usec=100;
        for(i=0,nfds=0; i<taskInfo.subprocNum; i++)
        {
            if(taskProc[i].pid>0)
            {
                FD_SET(taskProc[i].pipe[0],&fdRead);
                if(taskProc[i].pipe[0]>nfds)
                {
                    nfds=taskProc[i].pipe[0];
                }
            }
        }

        if((activeCount=select(nfds+1,&fdRead,NULL,NULL,&timeo))<=0)
        {
            continue;
        }

        for(j=0; j<taskInfo.subprocNum && activeCount>0; j++)
        {
            if(FD_ISSET(taskProc[j].pipe[0],&fdRead))
            {
                activeCount--;
                memset(&rdPipeData,NULL,sizeof(rdPipeData));
                if((rdSize=read(taskProc[j].pipe[0],(char *)&rdPipeData+taskProc[j].rdSize,\
                                sizeof(rdPipeData)-taskProc[j].rdSize))<=0)
                {
                    cout <<"read pipe failed.index:"<<j<<" pid:"<<taskProc[j].pid<<" return:"<<rdSize<<" errno:"<<errno<<endl;
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    create_subproc(&taskProc[j]);
                    continue;
                }

                taskProc[j].rdSize+=rdSize;
                if(taskProc[j].rdSize==sizeof(rdPipeData))
                {
                    taskProc[j].rdSize=0;
                }
                else
                {
                    cout <<"read not complete."<<endl;
                    continue;
                }
                switch(rdPipeData.instr)
                {
                case E_PIPE_TASK_REQUEST:
                    //子进程请求一个扫描任务
                    taskProc[j].reqTask=1;
                    break;
                case E_PIPE_TASK_REPORT:
                    //返回一个扫描报告
                    if(taskInfo.reportLevel==1)
                    {
                        if(strlen(rdPipeData.data)>sizeof(this->nodeReport.target)-strlen(this->nodeReport.target)-3)
                        {
                            //剩余空间不够，先将报告加入发送队列
                            //printf("%s\n",nodeReport.target);
                            pgl_Send_Report->lock();
                            pgl_Send_Report->push_report(this->nodeReport);
                            pgl_Send_Report->unlock();
                            memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                        }
                        strcat(this->nodeReport.target,rdPipeData.data);
                    }
                    else
                    {
                        //reportLevel = 2
                        if(strlen(rdPipeData.data)>sizeof(this->nodeReport.target)-writeLen-3)
                        {
                            //剩余空间不够，先将报告加入容器
                            taskProc[j].vecReport.push_back(nodeReport);
                            writeLen=0;
                            memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                        }
                        sprintf(this->nodeReport.target+writeLen,"%s",rdPipeData.data);
                        writeLen=strlen(this->nodeReport.target);

                        if(rdPipeData.reportFin)
                        {
                            //所有报告接收完成后加入到发送队列中
                            taskProc[j].vecReport.push_back(nodeReport);

                            pgl_Send_Report->lock();
                            for(iter=taskProc[j].vecReport.begin(); iter!=taskProc[j].vecReport.end(); iter++)
                            {
                                pgl_Send_Report->push_report(*iter);
                            }
                            pgl_Send_Report->unlock();

                            taskProc[j].vecReport.clear();
                            writeLen=0;
                            memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                        }
                    }
                    break;
                case E_PIPE_PROC_RESTART:
                    //请求重新启动子进程
                    cout <<"subproc require restart."<<endl;
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    this->create_subproc(&taskProc[j]);
                    break;
                case E_PIPE_TASK_FIN:
                    //子进程通知父进程所有任务已完成
                    taskProc[j].fin=1;
                    break;
                case E_PIPE_PROC_ABORT:
                    //请求终止子进程
                    cout <<"subproc require abort."<<endl;
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    memset(&taskProc[j],NULL,sizeof(TASK_PROC));
                    break;
                case E_PIPE_PROC_KEEP:
                    //心跳包
                    taskProc[j].timestamp=time(NULL);
                    break;
                default:
                    cout <<"read pipe data type error."<<endl;
                    kill(taskProc[j].pid,SIGKILL);
                    close(taskProc[j].pipe[0]);
                    close(taskProc[j].pipe[1]);
                    create_subproc(&taskProc[j]);
                    break;
                }//end switch
            }
        }
    }

    return NULL;
}

int Task::create_subproc(struct _Task_Proc_ *taskProc)
{
    //创建一个子进程
    int fpw[2],fpr[2];
    int flags;

    memset(taskProc,NULL,sizeof(struct _Task_Proc_));

    if(pipe(fpw)!=0 || pipe(fpr)!=0)
    {
        cout <<"create pipe failed."<<endl;
        return -1;
    }

    taskProc->pid=fork();
    if(taskProc->pid==0)
    {
        //child proc
        close(fpw[1]);
        close(fpr[0]);

        setsid();
        signal(SIGTERM,deal_sigterm);
        this->task_proc(fpw[0],fpr[1]);

        exit(0);
    }
    else if(taskProc->pid<0)
    {
        cout <<"fork() error."<<endl;
        close(fpw[0]);
        close(fpr[1]);
        memset(taskProc,NULL,sizeof(struct _Task_Proc_));
        return -1;
    }

    taskProc->pipe[0]=fpr[0];
    taskProc->pipe[1]=fpw[1];
    close(fpw[0]);
    close(fpr[1]);

    flags=fcntl(taskProc->pipe[1],F_GETFL);
    fcntl(taskProc->pipe[1],F_SETFL, flags | O_NONBLOCK); // 设置为非阻塞
    taskProc->timestamp=time(NULL);
    cout <<"new subproc pid :"<<taskProc->pid<<endl;

    return 0;
}

int Task::scan_thread_list_join(int fpw)
{
    list<ScanThread *>::iterator iter;
    ScanThread *pThread=NULL;
    SCAN_REPORT *pReport=NULL;
    int wrSize,i;
    char *pStr=NULL,*p=NULL;
    PIPE_DATA wrPipeData;

    for(iter=threadList.begin(); iter!=threadList.end();)
    {
        pThread=*iter;
        if(pThread->finish())
        {
            //thread finish
            threadList.erase(iter);
            iter=threadList.begin();

            pThread->thread_join((void **)&pReport);    //获取线程返回结果

            if(taskInfo.reportLevel==1)
            {
                if(pReport->result)
                {
                    //report level为1时返回为ture
                    memset(&wrPipeData,NULL,sizeof(wrPipeData));
                    wrPipeData.instr=E_PIPE_TASK_REPORT;
                    memcpy(wrPipeData.data,pReport->target,sizeof(wrPipeData.data));
                    wrPipeData.data[sizeof(wrPipeData.data)-1]=NULL;
                    wrPipeData.data[sizeof(wrPipeData.data)-2]=NULL;
                    wrPipeData.data[strlen(wrPipeData.data)]='\n';
                    if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
                    {
                        cout <<"subproc write pipe failed."<<endl;
                        exit(-1);
                    }
                }
            }
            else
            {
                if(pReport->result!=0)
                {
                    //report level为2时返回报告指针
                    pStr=(char *)pReport->reserved;
                    p=pStr;
                    while(*p!=NULL)
                    {
                        memset(&wrPipeData,NULL,sizeof(wrPipeData));
                        wrPipeData.instr=E_PIPE_TASK_REPORT;

                        for(i=0; i<sizeof(wrPipeData.data)-2 && *p!=NULL; i++,p++)
                        {
                            wrPipeData.data[i]=*p;
                        }
                        if(*p==NULL)
                        {
                            wrPipeData.reportFin=1;
                        }
                        if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
                        {
                            cout <<"subproc write pipe failed."<<endl;
                            exit(-1);
                        }
                    }
                    free(pStr);
                    pStr=NULL;
                    p=NULL;
                }
            }

            delete pThread;
            continue;
        }
        iter++;
    }

    //检查线程是否超时
    if(this->taskInfo.timeo!=0)
    {
        //统计超时线程个数
        for(iter=threadList.begin(),this->threadTimoCount=0; iter!=threadList.end(); iter++)
        {
            pThread=*iter;
            if(time(NULL)-pThread->timestamp>this->taskInfo.timeo)
            {
                //thread timeout
                this->threadTimoCount++;    //线程超时个数加一
            }
        }

        //当超时的线程超过容量一半并且无未完成的线程时，请求重启子进程。
        if((this->threadTimoCount>=(taskInfo.threadNum%2==0?taskInfo.threadNum/2:(taskInfo.threadNum+1)/2)) && \
                this->threadTimoCount>=this->threadList.size())
        {
            //All thread has timed out
            memset(&wrPipeData,NULL,sizeof(wrPipeData));
            //请求重启子进程
            wrPipeData.instr=E_PIPE_PROC_RESTART;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc write pipe failed."<<endl;
                exit(-1);
            }
        }

        //子进程所有任务线程已超时或所有线程已结束时，通知父进程。
        if(this->threadTimoCount>=threadList.size() || threadList.empty())
        {
            //通知所有任务已完成
            wrPipeData.instr=E_PIPE_TASK_FIN;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc write pipe failed."<<endl;
                exit(-1);
            }
        }
    }
    else
    {
        //没有设置超时的情况。
        if(threadList.empty() && this->informTaskFin==0)
        {
            //通知所有任务已完成
            wrPipeData.instr=E_PIPE_TASK_FIN;
            if(write(fpw,&wrPipeData,sizeof(wrPipeData))<=0)
            {
                cout <<"subproc write pipe failed."<<endl;
                exit(-1);
            }
            this->informTaskFin=1;
        }
    }

    return 0;
}

ScanThread::ScanThread(string target,string modName,int reportLevel)
{
    memset(&report,NULL,sizeof(report));
    this->target=target;
    this->modName=modName;
    this->timestamp=time(NULL);
    this->reportLevel=reportLevel;

    return;
}

ScanThread::~ScanThread()
{
    return;
}

void *ScanThread::func(void *para)
{
    //scan function
    char modPath[1000];
    char *pBuffer=NULL,*pTemp=NULL;
    char prefix[5100];
    PyObject *pModule=NULL,*pFunc=NULL;
    PyObject *pArgs=NULL,*pValue=NULL;

    memset(modPath,NULL,sizeof(modPath));
    memset(&report,NULL,sizeof(report));
    memset(prefix,NULL,sizeof(prefix));
    strcat(report.target,target.c_str());

    PyGILState_STATE state = PyGILState_Ensure();
    //导入Python模块
    modName[modName.find('.')]=NULL;
    pModule=PyImport_Import(PyString_FromString(modName.c_str()));
    if(pModule==NULL)
    {
        printf("import '%s' error\n",modName.c_str());
        goto skip;
    }

    //获取函数
    pFunc=PyObject_GetAttrString(pModule,"exec_task");
    if(pFunc==NULL)
    {
        printf("get function error\n");
        Py_DECREF(pModule);
        goto skip;
    }

    //设置参数
    pArgs=PyTuple_New(1);   //PyTuple==Python中的元组
    PyTuple_SetItem(pArgs,0,PyString_FromString(report.target));

    //调用函数并获取返回值
    pValue=PyEval_CallObject(pFunc,pArgs);
    if(pValue==NULL)
    {
        //printf("call function error\n");
        Py_DECREF(pModule);
        Py_DECREF(pFunc);
        Py_DECREF(pArgs);
        goto skip;
    }

    if(PyArg_Parse(pValue,"s",&pBuffer))
    {
        if(this->reportLevel==1)
        {
            report.result=atoi(pBuffer);
        }
        else
        {
            if(strlen(pBuffer)>0)
            {
                sprintf(prefix,"Frame report for %s:\n",report.target);
                pTemp=(char *)malloc(strlen(pBuffer)+strlen(prefix)+5);
                memset(pTemp,NULL,strlen(pBuffer)+strlen(prefix)+5);
                strcat(pTemp,prefix);
                strcat(pTemp,pBuffer);
                strcat(pTemp,"\n\n");
                report.reserved=pTemp;
                report.result=1;
            }
            else
            {
                printf("report NULL.\n");
            }
        }

    }

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

SendReport::SendReport()
{
    pthread_rwlock_init(&rwlock,NULL);
    this->run(NULL);

    return;
}

SendReport::~SendReport()
{
    pthread_rwlock_destroy(&rwlock);

    return;
}

int SendReport::push_report(SCAN_REPORT report)
{
    //加入待发送报告队列
    this->reportQueue.push(report);

    return 0;
}

void *SendReport::func(void *para)
{
    SCAN_REPORT report;

    while(1)
    {
        //每隔3秒发送一次队列中的报告
        sleep(3);
        this->lock();
        while(!this->reportQueue.empty())
        {
            report=this->reportQueue.front();
            this->reportQueue.pop();
            if(this->reportQueue.empty())
            {
                report.sendFin=1;
            }
            this->unlock();

            while(pgl_Server_Comm->ex_send(&report,E_PROTO_TASK_REPORT)<=0)
            {
                //resend
                cout <<"send task report failed."<<endl;
                sleep(10);
            }
            this->lock();
        }
        this->unlock();
    }

    return NULL;
}

int SendReport::empty()
{
    this->lock();
    if(this->reportQueue.empty())
    {
        this->unlock();
        return 1;
    }
    this->unlock();
    return 0;
}

int SendReport::lock()
{
    pthread_rwlock_wrlock(&this->rwlock);
    return 0;
}

int SendReport::unlock()
{
    pthread_rwlock_unlock(&this->rwlock);
    return 0;
}




