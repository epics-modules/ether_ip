# Example vxWorks startup file

# Following must be added for many board support packages
#cd <full path to target bin directory>

< cdCommands

#< ../nfsCommands

cd appbin
ld < iocCore
ld < testether_ip
# Initialize EtherIP driver, define PLCs
# -------------------------------------
drvEtherIP_init
hostAdd "snsioc1", "128.165.160.146"
drvEtherIP_define_PLC "plc1", "snsioc1"

EIP_verbosity=10

cd top
dbLoadDatabase("./dbd/ether_ip_test.dbd")
dbLoadRecords("./db/ramp.db","user=kay")
dbLoadRecords("./db/stat.db","user=kay,testtag=BOOLs")
dbLoadRecords("./db/ai.db","user=kay")
dbLoadRecords("./db/ana.db","user=kay")
dbLoadRecords("./db/bi.db","user=kay")
dbLoadRecords("./db/bin.db","user=kay")
dbLoadRecords("./db/mbbi.db","user=kay")
dbLoadRecords("./db/ao.db","user=kay")
dbLoadRecords("./db/bo.db","user=kay")
dbLoadRecords("./db/mbbo.db","user=kay")
dbLoadRecords("./db/test.db","user=kay")

# Time syncronization:
# beowolf: 128.165.160.128
putenv("EPICS_TS_NTP_INET=128.165.160.128")
# Default master port: 18233
TSconfigure (1, 10, 0, 18299, 0, 0, 0)

iocInit

#dbpf "kay:test1.TPRO", "1"
#dbpf "kay:test2.TPRO", "1"

