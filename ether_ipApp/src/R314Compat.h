/* $Id$
 *
 * R314Compat.h
 *
 * Pulled Stephanie Allison's wrappers that make R3.13 look
 * like R3.14 - at least to the extend that we need for this.
 *
 */
#include "epicsVersion.h"

/* TODO:
 * This works for R3.14.x, but will fail for R4.1 ...
 */
#if EPICS_VERSION >= 3 && EPICS_REVISION >= 14
#  include "epicsMutex.h"
#  include "epicsEvent.h"
#  include "epicsThread.h"
#  include "epicsTime.h"
#  define HAVE_314_API
#else /* begin 3.13 settings */

#ifdef vxWorks
#include <taskLib.h>
 
/* For timing: all times in seconds */
#define epicsTimeStamp              double
#define epicsTimeGetCurrent(A)      (*(A)=(double)tickGet()/sysClkRateGet())
#define epicsTimeDiffInSeconds(B,A) (*(B) - *(A))
#define epicsTimeLessThan(A,B)      (*(A) < *(B))
#define epicsTimeAddSeconds(T,S)    (*(T) += (S))

/* For event and mutex */ 
#define epicsMutexId         SEM_ID
#define epicsMutexLockOK     OK
/* semaphore options (mutex) */
#define epicsMutexCreate()   semMCreate(SEM_Q_PRIORITY|SEM_DELETE_SAFE|SEM_INVERSION_SAFE)
#define epicsMutexDestroy    semDelete
#define epicsMutexUnlock     semGive
#define epicsMutexLockOK     OK
#define epicsMutexLock(A)    semTake(A,WAIT_FOREVER)
#define epicsMutexTryLock(A) semTake(A,NO_WAIT)
/*
#define epicsEventId            SEM_ID
#define epicsEventWaitStatus    int
#define epicsEventWaitOK        OK
#define epicsEventCreate               semMCreate
#define epicsEventDestroy              semDelete
#define epicsEventWaitWithTimeout      semTake
#define epicsEventSignal               semGive
*/

/* For threads */
#define epicsThreadSleep(A)     taskDelay((int)((A) * sysClkRateGet()))
#define epicsThreadId           int
 
/* Parameters for scan task, one per PLC.
 * These values are similar to the Allen Bradley ones
 * taken from EPICS/base/include/task_param.h.
 */
#ifndef ARCH_STACK_FACTOR
#  if CPU_FAMILY == MC680X0 
#    define ARCH_STACK_FACTOR 1
#  else
#    define ARCH_STACK_FACTOR 2
#  endif
#endif
#define EIPSCAN_PRI   65
#define EIPSCAN_OPT   VX_FP_TASK | VX_STDIO
#define EIPSCAN_STACK (5096*ARCH_STACK_FACTOR)

#else /* We're before R3.14, not under vxWorks */
#define epicsMutexId  int
#define epicsMutexCreate()   0
#define epicsMutexLock(A)    A=1
#define epicsMutexUnlock(A)  A=0
#endif
 
#endif /* end 3.13 settings */
