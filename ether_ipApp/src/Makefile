# Makefile
TOP = ../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

# On Darwin, got errors about unused assignment in recGblSetSevr
USR_CFLAGS += -Wno-unused-value

# On WIN32, have to add socket library
# to build the test program
SYS_PROD_LIBS_WIN32 = wsock32

ifdef BASE_3_15
devEtherIP_CFLAGS += -DBUILD_LONG_STRING_SUPPORT
ETHER_IP_DBD = ether_ip-lsio.dbd
else
ETHER_IP_DBD = ether_ip-3-14.dbd
endif

PROD_HOST += ether_ip_test
ether_ip_test_SRCS += ether_ip_test.c
ether_ip_test_LIBS += Com
ether_ip_test_SYS_LIBS_solaris += socket
ether_ip_test_SYS_LIBS_solaris += nsl

DBD = ether_ip.dbd

LIBRARY_IOC = ether_ip

INC += eip_bool.h
INC += dl_list.h
INC += ether_ip.h
INC += drvEtherIP.h

# create munch file for dynamic loading will install in <bin>
PROD_IOC_vxWorks += ether_ipLib

LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
DLL_LIBS = ca Com
SYS_DLL_LIBS = wsock32
ether_ip_RCS_WIN32 = ether_ip.rc

ether_ip_SRCS += dl_list.c
ether_ip_SRCS += ether_ip.c
ether_ip_SRCS += drvEtherIP.c
ether_ip_SRCS += devEtherIP.c
ether_ip_SRCS += drvEtherIPRegister.cpp

# The PROD_IOC_vxWorks is built from the same srcs and must include
# the registry routines.
ether_ipLib_SRCS+=$(ether_ip_SRCS)
ether_ipLib_SRCS+= ether_ip_registerRecordDeviceDriver.cpp

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

ether_ip.dbd$(DEP): ../Makefile
	@$(RM) $@
	@echo $(COMMON_DIR)/ether_ip.dbd:> $@

$(COMMON_DIR)/ether_ip.dbd: ../$(ETHER_IP_DBD)
	@$(RM) $@
	$(CP) $< $@
