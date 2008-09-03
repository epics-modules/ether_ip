# Makefile
TOP = ../..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# <name>.dbd will be created from <name>Include.dbd
DBD += example.dbd

#=============================

PROD_IOC = example

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
example_SRCS += example_registerRecordDeviceDriver.cpp
example_SRCS_DEFAULT += exampleMain.cpp
example_SRCS_vxWorks += -nil-

#The following adds support from base/src/vxWorks
example_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Use win32 object libs for registered support
example_LIBS_win32 += recIocObj
example_LIBS_win32 += softDevIocObj
example_LIBS_win32 += testDevIocObj
example_LIBS_DEFAULT += recIoc
example_LIBS_DEFAULT += softDevIoc
example_LIBS_DEFAULT += testDevIoc

example_LIBS += ether_ip
ether_ip_DIR = $(INSTALL_LIB)

example_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

else

include $(TOP)/config/CONFIG_APP
include $(TOP)/config/RULES_ARCHS

endif
