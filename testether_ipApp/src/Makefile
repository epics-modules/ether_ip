# Makefile
TOP = ../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

# <name>.dbd will be created from <name>Include.dbd
DBD += eipIoc.dbd

#=============================

FINAL_LOCATION = $(shell $(PERL) $(TOOLS)/fullPathName.pl $(INSTALL_LOCATION))
EIP_DBD = $(FINAL_LOCATION)/dbd/eipIoc.dbd
USR_CPPFLAGS += -DEIP_DBD=\"$(EIP_DBD)\"

PROD_IOC = eipIoc

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
eipIoc_SRCS += eipIoc_registerRecordDeviceDriver.cpp
eipIoc_SRCS_DEFAULT += eipIocMain.cpp
eipIoc_SRCS_vxWorks += -nil-

#The following adds support from base/src/vxWorks
eipIoc_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

eipIoc_LIBS += ether_ip
ether_ip_DIR = $(INSTALL_LIB)

eipIoc_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
