TOP = ..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
include $(TOP)/configure/RULES.iocBoot

else

include $(TOP)/config/CONFIG_APP
DIRS += $(wildcard *ioc*)
include $(TOP)/config/RULES.iocBoot

endif
