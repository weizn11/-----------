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
            options.forground=1;
            break;
        case 'r':
            options.reconn=1;
            break;
        case 'n':
            if(strlen(optarg)>15)
            {
                cout <<"-n option input error"<<endl;
                exit(-1);
            }
            strcat(options.serverIP,optarg);
            break;
        case 'p':
            options.serverPort=atoi(optarg);
            if(options.serverPort<1 || options.serverPort>65535)
            {
                cout <<"-p option input error"<<endl;
                exit(-1);
            }
            break;
        case 'P':
            if(strlen(optarg)>20)
            {
                cout <<"-P option input error"<<endl;
                exit(-1);
            }
            strcat(options.key,optarg);
            break;
        case E_OPT_UPLOAD_MOD:
            strcat(options.scanModulePath,optarg);
            options.upScanMod=1;
            break;
        case E_OPT_UPLOAD_LIST:
            strcat(options.scanListPath,optarg);
            options.upScanList=1;
            break;
        case E_OPT_SUBPROC:
            options.subprocNum=atoi(optarg);
            if(options.subprocNum>20 || options<1)
            {
                cout <<"--subproc option input error."<<endl
                exit(-1);
            }
            break;
        case E_OPT_THREADS:
            options.threadNum=atoi(optarg);
            if(options.threadNum>200 || options.threadNum<1)
            {
                cout <<"--threads option input error."<<endl;
                exit(-1);
            }
            break;
        case E_OPT_REPORT_LEVEL:
            options.reportLevel=atoi(optarg);
            if(options.reportLevel!=1 && options.reportLevel!=2)
            {
                cout <<"--report-level option input error."<<endl;
                exit(-1);
            }
            break;
        case 'L':
            options.launch=1;
            break;
        case E_OPT_LOAD_MOD:
            strcat(options.modName,optarg);
            break;
        case E_OPT_LOAD_LIST:
            strcat(options.listName,optarg);
            break;
        case E_OPT_GET_REPORT:
            options.getRes=1;
            break;
        case E_OPT_TIMEO:
            options.timeo=atoi(optarg);
            if(options.timeo<0)
            {
                cout <<"--timeo option input error"<<endl;
                exit(-1);
            }
            break;
        case E_OPT_TASK_ABORT:
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
