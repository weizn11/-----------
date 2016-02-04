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

void *Task::func(void *para)
{
    string target;
    ScanThread *pScan=NULL;
    int ret;
    char pwd[1000],pyenv[1000];

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

    while(1)
    {
        if(this->scan_thread_list_join()!=0)   //clear complete thread
        {
            ::pgl_Task=NULL;
            if(::pgl_Task==NULL)
                cout <<"set pgl_Task=NULL"<<endl;
            cout <<"Destory task obj success."<<endl;

            if(this->taskInfo.timeo==0)
            {
                PyGILState_Ensure();
                Py_Finalize();
                cout <<"clear Python env"<<endl;
            }
            delete this;
            return NULL;
        }
        //create scan thread
        pthread_mutex_lock(&queueMutex);
        while(!scanQueue.empty())
        {
            if(threadList.size()>=taskInfo.threadNum)
            {
                break;
            }
            target=scanQueue.front();

            pScan=new ScanThread(target,taskInfo.modName);
            if(pScan==NULL)
            {
                break;
            }
            if((ret=pScan->run(NULL))!=0)
            {
                //cout <<"pthread_create() error.code:"<<ret<<endl;
                delete pScan;
                break;
            }
            else
            {
                scanQueue.pop();
                threadList.push_back(pScan);
            }
        }
        pthread_mutex_unlock(&queueMutex);
        usleep(1000);
    }
    Py_Finalize();
    return NULL;
}

int Task::scan_thread_list_join()
{
    list<ScanThread *>::iterator iter;
    ScanThread *pThread=NULL;
    SCAN_REPORT *pReport=NULL;
    int wrSize;
    int currTime;

    currTime=time(NULL);
    for(iter=threadList.begin(); iter!=threadList.end();)
    {
        pThread=*iter;
        if(pThread->finish())
        {
            //thread finish
            //printf("list.size:%d    queue.size:%d    finFlag:%d      \r",threadList.size(),scanQueue.size(),this->taskDistriFinFlag);
            threadList.erase(iter);
            iter=threadList.begin();

            pThread->thread_join((void **)&pReport);
            //cout <<"scan thread "<<pReport->target<<" exit"<<endl;

            if(pReport->result)
            {
                //available
                sprintf(this->nodeReport.target+writeLen,"%s\n",pReport->target);
                wrSize=strlen(this->nodeReport.target+writeLen);
                writeLen+=wrSize;
                if(sizeof(this->nodeReport.target)-writeLen<255)
                {
                    writeLen=0;
                    pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT);
                    memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
                }
            }
            delete pThread;
            continue;
        }
        else
        {
            //cout <<"thread "<<pThread->target<<" not exit"<<endl;
            if(this->taskInfo.timeo!=0 && (currTime-pThread->timestamp>this->taskInfo.timeo))
            {
                //thread timeout
                printf("list.size:%d    queue.size:%d    finFlag:%d      \r",threadList.size(),scanQueue.size(),this->taskDistriFinFlag);
                pThread->thread_detch();

                threadList.erase(iter);
                iter=threadList.begin();

                continue;
            }
        }
        iter++;
    }
    if(this->taskDistriFinFlag && threadList.empty() && scanQueue.empty())
    {
        //task complete
        cout <<"task completed."<<endl;
        if(strlen(this->nodeReport.target)>1)
        {
            writeLen=0;
            pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT);
        }

        memset(&this->nodeReport,NULL,sizeof(this->nodeReport));
        this->nodeReport.taskFin=1;
        pgl_Server_Comm->ex_send(&this->nodeReport,E_PROTO_TASK_REPORT);

        return -1;
    }
    //printf("list.size:%d    queue.size:%d    finFlag:%d      \r",threadList.size(),scanQueue.size(),this->taskDistriFinFlag);

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









