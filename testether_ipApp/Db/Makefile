# Makefile.Host
TOP = ../..
ifdef EPICS_HOST_ARCH

include $(TOP)/configure/CONFIG

#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
#  Optimization of DB files using dbst (DEFAULT: NO)
#DB_OPT = YES

#----------------------------------------------------
#  Creation of <name>.dbd file from <name>Include.dbd
#DBDNAME += testether_ip.dbd

#----------------------------------------------------
# Add databases, templates, substitutions like this

DB += ai.db
DB += ana.db
DB += ao.db
DB += bi.db
DB += bin.db
DB += bo.db
DB += mbbi.db
DB += mbbo.db
DB += ramp.db
DB += eip_stat.db 
DB += test.db 
DB += mod.db 

#----------------------------------------------------
# Declare template files which do not show up in DB
#USES_TEMPLATE += testether_ip.template

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

else

include $(TOP)/config/CONFIG_APP
include $(TOP)/config/RULES_ARCHS

endif
