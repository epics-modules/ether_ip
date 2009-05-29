#Makefile at top of application tree
TOP = .
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), configure)
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard ether_ipApp))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard testether_ipApp))
#DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *App))
#DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *app))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *iocBoot))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *iocboot))
include $(TOP)/configure/RULES_TOP

else

include $(TOP)/config/CONFIG_APP
DIRS += ether_ipApp
DIRS += config
#DIRS += $(wildcard *App)
DIRS += testether_ipApp
DIRS += $(wildcard *app)
DIRS += $(wildcard iocBoot)
DIRS += $(wildcard iocboot)
include $(TOP)/config/RULES_TOP

endif


test:
	bin/linux-x86/ether_ip_test -i 160.91.232.217 -v 10 K_RealArray_10[2] 

val:
	valgrind --trace-children=yes --leak-check=full --show-reachable=yes bin/linux-x86/ether_ip_test -i 160.91.232.217 -v 1 K_RealArray_10[2] 
