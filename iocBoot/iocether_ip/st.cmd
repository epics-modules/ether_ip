# -*- shell-script -*-
# Example vxWorks startup file before R3.14

< cdCommands

cd appbin
ld < iocCore
ld < testether_ip
# Initialize EtherIP driver, define PLCs
# -------------------------------------
drvEtherIP_init

# Tell vxWorks how to get to "snsioc1"
# that is the ENET module of a ControlLogix PLC.
# vxWorks doesn't generally use DNS, so if you
# want to use names, you have to define them:
hostAdd("myplc", "192.168.0.50")
# You might need this, too, if the IOC is on
# a different subnet: routeAdd <target>, <gateway>
# routeAdd("128.165.160.146", "128.165.160.241")

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
drvEtherIP_define_PLC("plc1", "myplc", 0)

# 10 - Truckload of detailed messages down to the raw send/receive buffers
#  8 - Good value for operation
#  0 - Nothing except very severe problems
EIP_verbosity=8

cd top
dbLoadDatabase("./dbd/ether_ip_test.dbd")
#dbLoadRecords("./db/ramp.db",    "IOC=snsioc4")
#dbLoadRecords("./db/eip_stat.db","PLC=plc1,IOC=snsioc4,TAG=REAL")
#dbLoadRecords("./db/ai.db",      "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/ana.db",     "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/bi.db",      "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/bin.db",     "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/mbbi.db",    "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/ao.db",      "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/bo.db",      "PLC=plc1,IOC=snsioc4")
#dbLoadRecords("./db/mbbo.db",    "PLC=plc1,IOC=snsioc4")
dbLoadRecords("./db/test.db",    "PLC=plc1,IOC=plc1")

# Default master port: 18233
TSconfigure (1, 10, 0, 18299, 0, 0, 0)

iocInit

