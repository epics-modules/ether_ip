TOP = ..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *src*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Src*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Db*))
include $(TOP)/configure/RULES_DIRS

else

include $(TOP)/config/CONFIG_APP
DIRS += $(wildcard *src*)
DIRS += $(wildcard *Src*)
DIRS += $(wildcard *db*)
DIRS += $(wildcard *Db*)
include $(TOP)/config/RULES_DIRS

endif
