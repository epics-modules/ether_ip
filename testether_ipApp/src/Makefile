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

# Use win32 object libs for registered support
eipIoc_LIBS_win32 += recIocObj
eipIoc_LIBS_win32 += softDevIocObj
eipIoc_LIBS_win32 += testDevIocObj
eipIoc_LIBS_DEFAULT += recIoc
eipIoc_LIBS_DEFAULT += softDevIoc
eipIoc_LIBS_DEFAULT += testDevIoc

eipIoc_LIBS += ether_ip
ether_ip_DIR = $(INSTALL_LIB)

eipIoc_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
