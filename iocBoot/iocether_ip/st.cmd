# -*- shell-script -*-
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

# Tell vxWorks how to get to "snsioc1":
hostAdd "snsioc1", "128.165.160.146"
# You might need this, too: routeAdd <target>, <gateway>
# routeAdd "128.165.160.146", "128.165.160.241"

# drvEtherIP_define_PLC <name>, <ip_addr>, <slot>
# The driver/device uses the <name> to indentify the PLC.
# 
# <ip_addr> can be an IP address in dot-notation
# or a name that the IOC knows about (defined via hostAdd,
# see step 4).
# The IP address gets us to the ENET interface.
# To get to the PLC itself, we need the slot that
# it resides in. The first, left-most slot in the
# ControlLogix crate is slot 0.
drvEtherIP_define_PLC "plc1", "snsioc1", 6

EIP_verbosity=10

cd top
dbLoadDatabase("./dbd/ether_ip_test.dbd")
dbLoadRecords("./db/ramp.db",    "IOC=snsioc4")
dbLoadRecords("./db/eip_stat.db","PLC=plc1,IOC=snsioc4,TAG=REAL")
dbLoadRecords("./db/ai.db",      "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/ana.db",     "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/bi.db",      "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/bin.db",     "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/mbbi.db",    "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/ao.db",      "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/bo.db",      "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/mbbo.db",    "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/test.db",    "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/null.db","user=kay")

# Time syncronization:
# beowolf: 128.165.160.128
putenv("EPICS_TS_NTP_INET=128.165.160.128")
# Default master port: 18233
TSconfigure (1, 10, 0, 18299, 0, 0, 0)

iocInit

#dbpf "kay:test1.TPRO", "1"
#dbpf "kay:test2.TPRO", "1"





