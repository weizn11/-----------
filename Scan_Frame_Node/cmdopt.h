#ifndef CMDOPT_H_INCLUDED
#define CMDOPT_H_INCLUDED

#include <getopt.h>

#define GETOPTS_OPTION_STRING "frn:p:P:L"

enum _Cmd_Options_
{
    E_OPT_UPLOAD_MOD=0x01,
    E_OPT_UPLOAD_LIST,
    E_OPT_LOAD_MOD,
    E_OPT_LOAD_LIST,
    E_OPT_GET_REPORT,
    E_OPT_TIMEO,
    E_OPT_TASK_ABORT,
    E_OPT_THREADS,
    E_OPT_SUBPROC,
    E_OPT_REPORT_LEVEL,  //1:brief 2:detailed
};

static struct option cmd_opts[] =
{
    //{"参数名","是否带有值","返回值"}
    {"upload-mod",1, NULL,E_OPT_UPLOAD_MOD},
    {"upload-list",1,NULL,E_OPT_UPLOAD_LIST},
    {"load-mod",1,NULL,E_OPT_LOAD_MOD},
    {"load-list",1,NULL,E_OPT_LOAD_LIST},
    {"get-report",0,NULL,E_OPT_GET_REPORT},
    {"timeo",1,NULL,E_OPT_TIMEO},
    {"task-abort",0,NULL,E_OPT_TASK_ABORT},
    {"threads",1,NULL,E_OPT_THREADS},
    {"subproc",1,NULL,E_OPT_SUBPROC},
    {"report-level",1,NULL,E_OPT_REPORT_LEVEL},
    {0,0,0,0}
};

typedef struct _CMD_OPTIONS_
{
    char serverIP[16];
    short serverPort;
    char key[50];
    char scanModulePath[255];
    char scanListPath[255];
    char modName[255];
    char listName[255];

    int upScanMod;
    int upScanList;
    int subprocNum;
    int threadNum;
    int timeo;
    int launch;
    int reportLevel;

    int getRes;
    int forground;
    int taskAbort;
    int reconn;
}CMD_OPTIONS;

int config_init(CMD_OPTIONS &options,int argc,char *argv[]);

#endif // CMDOPT_H_INCLUDED
