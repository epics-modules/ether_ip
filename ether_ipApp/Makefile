TOP = ..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *src*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Src*))
include $(TOP)/configure/RULES_DIRS

else

include $(TOP)/config/CONFIG_APP
DIRS += $(wildcard *src*)
DIRS += $(wildcard *Src*)
include $(TOP)/config/RULES_DIRS

endif
