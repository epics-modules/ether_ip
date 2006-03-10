/*drvEtherIPRegister.cpp */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "iocsh.h"
#include "epicsExport.h"
#include "drvEtherIP.h"

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */
  
static const iocshArg drvEtherIP_default_rateArg0 = {"value", iocshArgDouble};
static const iocshArg *const drvEtherIP_default_rateArgs[1] = {&drvEtherIP_default_rateArg0};
static const iocshFuncDef drvEtherIP_default_rateDef = {"drvEtherIP_default_rate", 1, drvEtherIP_default_rateArgs};
static void drvEtherIP_default_rateCall(const iocshArgBuf * args) {
	drvEtherIP_default_rate = args[0].dval;
}
  
static const iocshArg EIP_verbosityArg0 = {"value", iocshArgInt};
static const iocshArg *const EIP_verbosityArgs[1] = {&EIP_verbosityArg0};
static const iocshFuncDef EIP_verbosityDef = {"EIP_verbosity", 1, EIP_verbosityArgs};
static void EIP_verbosityCall(const iocshArgBuf * args) {
	EIP_verbosity = args[0].ival;
}

static const iocshArg EIP_buffer_limitArg0 = {"bytes", iocshArgInt};
static const iocshArg *const EIP_buffer_limitArgs[1] = {&EIP_buffer_limitArg0};
static const iocshFuncDef EIP_buffer_limitDef = {"EIP_buffer_limit", 1, EIP_buffer_limitArgs};
static void EIP_buffer_limitCall(const iocshArgBuf * args) {
        printf("Changing buffer limit from %lu ",
               (unsigned long) EIP_buffer_limit);
        EIP_buffer_limit = args[0].ival;
        printf("to %lu bytes\n",
               (unsigned long) EIP_buffer_limit);
}
  
static const iocshArg EIP_use_mem_string_fileArg0 = {"value", iocshArgInt};
static const iocshArg *const EIP_use_mem_string_fileArgs[1] = {&EIP_use_mem_string_fileArg0};
static const iocshFuncDef EIP_use_mem_string_fileDef = {"EIP_use_mem_string_file", 1, EIP_use_mem_string_fileArgs};
static void EIP_use_mem_string_fileCall(const iocshArgBuf * args) {
	EIP_use_mem_string_file = args[0].ival;
}
  
static const iocshFuncDef drvEtherIP_helpDef =
    {"drvEtherIP_help", 0, 0};
static void drvEtherIP_helpCall(const iocshArgBuf * args) {
	drvEtherIP_help();
}
  
static const iocshFuncDef drvEtherIP_initDef =
    {"drvEtherIP_init", 0, 0};
static void drvEtherIP_initCall(const iocshArgBuf * args) {
	drvEtherIP_init();
}
  
static const iocshFuncDef drvEtherIP_restartDef =
    {"drvEtherIP_restart", 0, 0};
static void drvEtherIP_restartCall(const iocshArgBuf * args) {
	drvEtherIP_restart();
}
  
static const iocshFuncDef drvEtherIP_dumpDef =
    {"drvEtherIP_dump", 0, 0};
static void drvEtherIP_dumpCall(const iocshArgBuf * args) {
	drvEtherIP_dump();
}
  
static const iocshFuncDef drvEtherIP_reset_statisticsDef =
    {"drvEtherIP_reset_statistics", 0, 0};
static void drvEtherIP_reset_statisticsCall(const iocshArgBuf * args) {
	drvEtherIP_reset_statistics();
}

static const iocshArg drvEtherIP_reportArg0 = {"value", iocshArgInt};
static const iocshArg *const drvEtherIP_reportArgs[1] = {&drvEtherIP_reportArg0};
static const iocshFuncDef drvEtherIP_reportDef = {"drvEtherIP_report", 1, drvEtherIP_reportArgs};
static void drvEtherIP_reportCall(const iocshArgBuf * args) {
	drvEtherIP_report(args[0].ival);
}

static const iocshArg drvEtherIP_define_PLCArg0 = {"plc_name", iocshArgString};
static const iocshArg drvEtherIP_define_PLCArg1 = {"ip_addr" , iocshArgString};
static const iocshArg drvEtherIP_define_PLCArg2 = {"slot"    , iocshArgInt   };
static const iocshArg * const drvEtherIP_define_PLCArgs[3] =
{&drvEtherIP_define_PLCArg0, &drvEtherIP_define_PLCArg1, &drvEtherIP_define_PLCArg2};
static const iocshFuncDef drvEtherIP_define_PLCDef = {"drvEtherIP_define_PLC", 3, drvEtherIP_define_PLCArgs};
static void drvEtherIP_define_PLCCall(const iocshArgBuf * args) {
	drvEtherIP_define_PLC(args[0].sval, args[1].sval, args[2].ival);
}
  
static const iocshArg drvEtherIP_read_tagArg0 = {"ip_addr" , iocshArgString};
static const iocshArg drvEtherIP_read_tagArg1 = {"slot"    , iocshArgInt   };
static const iocshArg drvEtherIP_read_tagArg2 = {"tag_name", iocshArgString};
static const iocshArg drvEtherIP_read_tagArg3 = {"elements", iocshArgInt   };
static const iocshArg drvEtherIP_read_tagArg4 = {"timeout" , iocshArgInt   };
static const iocshArg * const drvEtherIP_read_tagArgs[5] =
{&drvEtherIP_read_tagArg0, &drvEtherIP_read_tagArg1, &drvEtherIP_read_tagArg2,
 &drvEtherIP_read_tagArg3, &drvEtherIP_read_tagArg4};
static const iocshFuncDef drvEtherIP_read_tagDef = {"drvEtherIP_read_tag", 5, drvEtherIP_read_tagArgs};
static void drvEtherIP_read_tagCall(const iocshArgBuf * args) {
	drvEtherIP_read_tag(args[0].sval, args[1].ival, args[2].sval,
                            args[3].ival, args[4].ival);
}

void drvEtherIP_Register() {
	iocshRegister(&drvEtherIP_default_rateDef, drvEtherIP_default_rateCall);
	iocshRegister(&EIP_verbosityDef        , EIP_verbosityCall);
	iocshRegister(&EIP_buffer_limitDef     , EIP_buffer_limitCall);
	iocshRegister(&EIP_use_mem_string_fileDef, EIP_use_mem_string_fileCall);
	iocshRegister(&drvEtherIP_helpDef      , drvEtherIP_helpCall);
	iocshRegister(&drvEtherIP_initDef      , drvEtherIP_initCall);
	iocshRegister(&drvEtherIP_restartDef   , drvEtherIP_restartCall);
	iocshRegister(&drvEtherIP_dumpDef      , drvEtherIP_dumpCall);
	iocshRegister(&drvEtherIP_reset_statisticsDef, drvEtherIP_reset_statisticsCall);
	iocshRegister(&drvEtherIP_reportDef    , drvEtherIP_reportCall);
	iocshRegister(&drvEtherIP_define_PLCDef, drvEtherIP_define_PLCCall);
	iocshRegister(&drvEtherIP_read_tagDef  , drvEtherIP_read_tagCall);
}
#ifdef __cplusplus
}
#endif	/* __cplusplus */

epicsExportRegistrar(drvEtherIP_Register);
