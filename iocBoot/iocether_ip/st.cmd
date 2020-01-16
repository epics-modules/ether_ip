#! ../../bin/linux-x86_64/eipIoc
# 3.14 example startup file for a Host  -*- shell-script -*-

# Load dbd, register the drvEtherIP.. commands
dbLoadDatabase("../../dbd/eipIoc.dbd")
eipIoc_registerRecordDeviceDriver(pdbbase)


# Initialize EtherIP driver, define PLCs
drvEtherIP_init()

# EIP_verbosity(10)

drvEtherIP_define_PLC("plc1", "160.91.233.45", 0)

#dbLoadRecords("db/mbbo.db",    "PLC=plc1,IOC=test")
#dbLoadRecords("../../db/test.db", "PLC=plc1,IOC=test")
#dbLoadRecords("../../db/mod.db", "PLC=plc1,IOC=test")
dbLoadRecords("string.db", "PLC=plc1,IOC=test")

iocInit()

