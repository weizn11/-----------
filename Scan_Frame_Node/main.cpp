#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "cmdopt.h"
#include "comm.h"

using namespace std;

extern ServerComm *pgl_Server_Comm;

int main(int argc,char *argv[])
{
    CMD_OPTIONS options;

    config_init(options,argc,argv);

    pgl_Server_Comm=new ServerComm(options);
    if(pgl_Server_Comm->connect_to_server()!=0)
        return -1;
    if(pgl_Server_Comm->clientType==1)
    {
        //node
    }
    else
    {
        //manager
        if(options.upScanList || options.upScanMod)
        {
            pgl_Server_Comm->upload_scan_module();
        }
        if(options.launch && strlen(options.modName) && strlen(options.listName))
        {
            pgl_Server_Comm->launch_scan_task();
        }
    }

    while(1) sleep(1);
    Py_Finalize();

    return 0;
}
