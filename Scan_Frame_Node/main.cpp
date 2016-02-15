#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "cmdopt.h"
#include "comm.h"

using namespace std;

extern ServerComm *pgl_Server_Comm;
char **gl_argv=NULL;

void *trunc_log_thread(void *para)
{
    struct stat st;

    while(1)
    {
        sleep(5);
        memset(&st,NULL,sizeof(st));
        if(stat("node_log.out",&st)!=0)
        {
            continue;
        }
        if(st.st_size>10*1024*1024)
        {
            truncate("node_log.out",0);
        }
    }

    return NULL;
}

int main(int argc,char *argv[])
{
    int pid;

    CMD_OPTIONS options;
    pthread_t threadID;
    PROTO_GET_RESULTS getResPacket;

    gl_argv=argv;

    printf("v1.0.1\n");
    if(argc<7)
    {
        return -1;
    }

    config_init(options,argc,argv);

    if(strcmp(options.key,CLIENT_NODE_AUTH_KEY)==0)
    {
        //node
        if(options.forground==0)
        {
            pid=fork();
            if(pid<0)
            {
                printf("fork() error\n");
                return -1;
            }
            else if(pid>0)
            {
                sleep(1);
                printf("Parent process exit\n");
                exit(0);
            }
            setsid();
            freopen("node_log.out","w",stdout);    //redirect stdout
            pthread_create(&threadID,NULL,trunc_log_thread,NULL);
        }

        //cout <<"create comm thread."<<endl;
        pgl_Server_Comm=new ServerComm(options);
reconn:
        if(pgl_Server_Comm->connect_to_server()!=0)
        {
            if(options.reconn)
            {
                sleep(5);
                goto reconn;
            }
            return -1;
        }
    }
    else
    {
        //manager
        //cout <<"create comm thread."<<endl;
        pgl_Server_Comm=new ServerComm(options);
        if(pgl_Server_Comm->connect_to_server()!=0)
            return -1;

        if(options.getRes)
        {
            memset(&getResPacket,NULL,sizeof(getResPacket));
            pgl_Server_Comm->ex_send(&getResPacket,E_PROTO_GET_RESULTS);
        }
        if(options.upScanList || options.upScanMod)
        {
            pgl_Server_Comm->upload_scan_module();
            exit(0);
        }
        if(options.launch && strlen(options.modName) && strlen(options.listName))
        {
            pgl_Server_Comm->launch_scan_task();
        }
        if(options.taskAbort)
        {
            pgl_Server_Comm->abort_scan_task();
        }
    }

    while(1) sleep(1);

    return 0;
}
