# Makefile
TOP = ../..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
include $(TOP)/configure/RULES

else

include $(TOP)/config/CONFIG_APP
include $(TOP)/config/RULES_ARCHS

endif
