TOP = ../..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
ARCH = RTEMS-svgm
include $(TOP)/configure/RULES.ioc

else

include $(TOP)/config/CONFIG_APP
#ARCH = mv2300
ARCH = mv2100
include $(TOP)/config/RULES.ioc

endif
