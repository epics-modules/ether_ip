#Makefile at top of application tree
TOP = .
include $(TOP)/config/CONFIG_APP
DIRS += ether_ipApp
DIRS += config
#DIRS += $(wildcard *App)
DIRS += testether_ipApp
DIRS += $(wildcard *app)
DIRS += $(wildcard iocBoot)
DIRS += $(wildcard iocboot)
include $(TOP)/config/RULES_TOP
