#include <getopt.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cmdopt.h"
#include "exectask.h"

using namespace std;

int config_init(CMD_OPTIONS &options,int argc,char *argv[])
{
    int cmd_arg,index;

    memset(&options,NULL,sizeof(options));

    while ((cmd_arg=getopt_long(argc,argv,GETOPTS_OPTION_STRING,cmd_opts, &index))!=-1)
    {
        //printf("%c\n",cmd_arg);
        switch(cmd_arg)
        {
        case 'f':
            //前台运行
            options.forground=1;
            break;
        case 'r':
            //初次连接不上就重连
            options.reconn=1;
            break;
        case 'n':
            //server 地址
            if(strlen(optarg)>15)
            {
                cout <<"-n option input error"<<endl;
                exit(-1);
            }
            strcat(options.serverIP,optarg);
            break;
        case 'p':
            //server 端口
            options.serverPort=atoi(optarg);
            if(options.serverPort<1 || options.serverPort>65535)
            {
                cout <<"-p option input error"<<endl;
                exit(-1);
            }
            break;
        case 'P':
            //密码
            if(strlen(optarg)>20)
            {
                cout <<"-P option input error"<<endl;
                exit(-1);
            }
            strcat(options.key,optarg);
            break;
        case E_OPT_UPLOAD_MOD:
            //上传模块
            strcat(options.scanModulePath,optarg);
            options.upScanMod=1;
            break;
        case E_OPT_UPLOAD_LIST:
            //上传列表
            strcat(options.scanListPath,optarg);
            options.upScanList=1;
            break;
        case E_OPT_SUBPROC:
            //节点新建子进程数目
            options.subprocNum=atoi(optarg);
            if(options.subprocNum>20 || options.subprocNum<1)
            {
                cout <<"--subproc option input error."<<endl;
                exit(-1);
            }
            break;
        case E_OPT_THREADS:
            //每个子进程中的线程数
            options.threadNum=atoi(optarg);
            if(options.threadNum>200 || options.threadNum<1)
            {
                cout <<"--threads option input error."<<endl;
                exit(-1);
            }
            break;
        case E_OPT_REPORT_LEVEL:
            //返回报告等级 1 or 2
            options.reportLevel=atoi(optarg);
            if(options.reportLevel!=1 && options.reportLevel!=2)
            {
                cout <<"--report-level option input error."<<endl;
                exit(-1);
            }
            break;
        case 'L':
            //launch
            options.launch=1;
            break;
        case E_OPT_LOAD_MOD:
            //加载模块文件
            strcat(options.modName,optarg);
            break;
        case E_OPT_LOAD_LIST:
            //加载列表文件
            strcat(options.listName,optarg);
            break;
        case E_OPT_GET_REPORT:
            //获取扫描报告
            options.getRes=1;
            break;
        case E_OPT_TIMEO:
            //设置每个线程执行的超时时间
            options.timeo=atoi(optarg);
            if(options.timeo<0)
            {
                cout <<"--timeo option input error"<<endl;
                exit(-1);
            }
            break;
        case E_OPT_TASK_ABORT:
            //终止任务
            options.taskAbort=1;
            break;

        default:
            cout <<"input error"<<endl;
            exit(-1);
            break;
        }
    }
    return 0;
}
