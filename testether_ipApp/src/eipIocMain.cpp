/* eipIocMain.cpp
 * kasemirk@ornl.gov
 */
#include <stddef.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <epicsThread.h>
#include <devSup.h>
#include <dbCommon.h>
#include <dbDefs.h>
#include <dbAccessDefs.h>
#include <iocInit.h>
#include <iocsh.h>

#include "drvEtherIP.h"

#ifndef EIP_DBD
#error This needs to be compiled with -DEIP_DBD=/path/to/installed/eip.dbd
#endif

extern "C"
{
extern void eipIoc_registerRecordDeviceDriver(struct dbBase *);
}

static void usage()
{
    printf("Usage:\n");
    printf("       eipIoc st.cmd\n");
    printf("or\n");
    printf("       eipIoc -pPlc1=IP,slot [-m macro=value] -d database.db [-d another.db]\n");
    printf("\n");
    printf("In the first case, this program acts as an ordinary IOC, executing a startup file\n");
    printf("\n");
    printf("The second invocation is for a command-line mode similar to the 'softIoc' from EPICS base,\n");
    printf("extended with options to communicate via etherIp.\n");
}

static bool definePlc(const char *def)
{
    // Create modifyable copy
    char *copy = strdup(def);

    // Optional slot in "Plc1=IP,slot"
    int slot = 0;
    char *c = strrchr(copy, ',');
    if (c)
    {
        slot = atoi(c+1);
        *c = '\0';
    }

    // Split "Plc1=IP" at '='
    c = strrchr(copy, '=');
    if (! c)
    {
        free(copy);
        fprintf(stderr, "Missing '=' in PLC definition '%s'\n", def);
        return false;
    }
    *c = '\0';
    const char *plc = copy;
    const char *ip = c+1;

    printf("drvEtherIP_define_PLC(\"%s\", \"%s\", %d);\n", plc, ip, slot);
    drvEtherIP_define_PLC(plc, ip, slot);
    free(copy);

    return true;
}

int main(int argc,char *argv[])
{
    const char *dbd_file = EIP_DBD;
    char *macros = strdup("");

    if (argc == 1)
    {
        usage();
        return 0;
    }
    else if (argc == 2  &&  argv[1][0] != '-')
    {    
        iocsh(argv[1]);
        epicsThreadSleep(.2);
    }
    else
    {
        printf("# Command-line mode\n");
        printf("#\n");
        printf("# The following commands are executed from cmdline options\n");
        printf("\n");
        printf("dbLoadDatabase(\"%s\")\n", dbd_file);
        dbLoadDatabase(dbd_file, NULL, NULL);

        printf("eipIoc_registerRecordDeviceDriver(pdbbase)\n");
        eipIoc_registerRecordDeviceDriver(pdbbase);
        printf("drvEtherIP_init()\n");
        drvEtherIP_init();

        int ch;
        while ( (ch = getopt(argc, argv, "p:m:d:h")) != -1)
        {
            switch (ch)
            {
            case 'h':
                usage();
                return 0;
            case 'p':
                if (! definePlc(optarg))
                    return -2;
                break;
            case 'm':
                free(macros);
                macros = strdup(optarg);
                break;
            case 'd':
                printf("dbLoadRecords(\"%s\", \"%s\");\n", optarg, macros);
                dbLoadRecords(optarg, macros);
                break;
            default:
                usage();
                return -1;
            }
        }
        printf("iocInit()\n");

        printf("\n");
        printf("#\n");
        printf("# End of command-line options, entering IOC shell\n");
        printf("#\n");
        printf("\n");

        iocInit();
        epicsThreadSleep(0.5);
    }

    iocsh(NULL);

    return 0;
}
