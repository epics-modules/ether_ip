TOP = ../..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
#ARCH = RTEMS-svgm
ARCH = vxWorks-ppc603
TARGETS = cdCommands
include $(TOP)/configure/RULES.ioc

else

include $(TOP)/config/CONFIG_APP
#ARCH = mv2300
ARCH = ppc603
include $(TOP)/config/RULES.ioc

endif
