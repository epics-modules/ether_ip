# This is just for getting an ioc log server to run,
# for testing message capture from the driver.
# Read the appdevguide.

export EPICS_IOC_LOG_FILE_NAME=`pwd`/ioclog.txt
export EPICS_IOC_LOG_PORT=6505
iocLogServer
