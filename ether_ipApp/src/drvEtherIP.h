/* $Id$
 *
 * drvEtherIP
 *
 * IOC driver that uses ether_ip routines,
 * keeping lists of PLCs and tags and scanlists etc.
 *
 * kasemir@lanl.gov
 */

#ifndef ETHERIP_MAYOR

#include "ether_ip.h"
#include "dl_list.h"
#include "epicsVersion.h"

#define ETHERIP_MAYOR 2
#define ETHERIP_MINOR 1

/* TCP port */
#define ETHERIP_PORT 0xAF12

/* TCP timeout in millisec for connection and readback */
#define ETHERIP_TIMEOUT 5000

#if EPICS_VERSION >= 3 && EPICS_REVISION >= 14

#include "epicsMutex.h"
#include "epicsEvent.h"
#include "epicsThread.h"
#include "epicsTime.h"

/* For timing */
#define EIP_TIME_CONVERSION     1.0  /* sec/sec */
#define EIP_MIN_DELAY           0.02 /* second */
#define EIP_MIN_TIMEOUT         0.1  /* second */
#define EIP_MIN_CONN_TIMEOUT    1.0  /* second */
#define EIP_SHORT_TIMEOUT       1.0  /* second */
#define EIP_MEDIUM_TIMEOUT      2.0  /* second */
#define EIP_LONG_TIMEOUT        5.0  /* second */
#define EIP_TIME_GET(A)         epicsTimeGetCurrent(&(A))
#define EIP_TIME_DIFF(A,B)      epicsTimeDiffInSeconds(&(A),&(B))
#define EIP_TIME_ADD(A,B)       epicsTimeAddSeconds(&(A),(B))

/* For event and mutex */
#define EIP_SEMOPTS                    epicsEventFull
#define epicsMutexLockWithTimeout(A,B) epicsMutexLock(A)
 
#else /* begin 3.13 settings */

#include <taskLib.h>
#include "devLib.h"
#define  S_db_noMemory          S_dev_noMemory
 
/* For timing */
#define EIP_TIME_CONVERSION     sysClkRateGet()         /* tick/sec */
#define EIP_MIN_DELAY           0                       /* ticks */
#define EIP_MIN_TIMEOUT         10                      /* ticks */
#define EIP_MIN_CONN_TIMEOUT    100                     /* ticks */
#define EIP_SHORT_TIMEOUT       EIP_TIME_CONVERSION     /* ticks in 1 sec */
#define EIP_MEDIUM_TIMEOUT      (2*EIP_TIME_CONVERSION) /* ticks in 2 sec */
#define EIP_LONG_TIMEOUT        (5*EIP_TIME_CONVERSION) /* ticks in 5 sec */
#define epicsTimeStamp          ULONG               /* all times in ticks */
#define EIP_TIME_GET(A)         (A=tickGet())    /* current time in ticks */
#define EIP_TIME_DIFF(A,B)      ((double)(A)-(double)(B))  /* time diff   */
#define EIP_TIME_ADD(A,B)       (A+=(ULONG)(B))            /* time add    */

/* For event and mutex */ 
#define epicsMutexId            SEM_ID
#define epicsMutexLockStatus    int
#define epicsMutexLockOK        OK
#define epicsEventId            SEM_ID
#define epicsEventWaitStatus    int
#define epicsEventWaitOK        OK
/* semaphore options (mutex) */
#define EIP_SEMOPTS (SEM_Q_PRIORITY | SEM_DELETE_SAFE | SEM_INVERSION_SAFE)
#define epicsMutexCreate()             semMCreate(EIP_SEMOPTS)
#define epicsMutexDestroy              semDelete
#define epicsMutexUnlock               semGive
#define epicsMutexLock(A)              semTake(A,WAIT_FOREVER)
#define epicsMutexLockWithTimeout(A,B) semTake(A,B)
#define epicsMutexTryLock(A)           semTake(A,NO_WAIT)
#define epicsEventCreate               semMCreate
#define epicsEventDestroy              semDelete
#define epicsEventWaitWithTimeout      semTake
#define epicsEventSignal               semGive
 
/* For threads */
#define epicsThreadSleep(A)     taskDelay((int)A)
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
 
#endif /* end 3.13 settings */
 
typedef struct __TagInfo  TagInfo;  /* forwards */
typedef struct __ScanList ScanList;
typedef struct __PLC      PLC;

/* PLCInfo:
 * Per-PLC information
 * Generated with call to drvEtherIP_define_PLC
 * in IOC startup file.
 *
 * Holds
 * - EIPConnection for ether_ip protocol routines
 * - ScanList for this PLC, filled by device support
 */
struct __PLC
{
    DLL_Node      node;
    epicsMutexId  lock;
    char          *name;        /* symbolic name, used to identify PLC    */
    char          *ip_addr;     /* IP or DNS name that IOC knows          */
    int           slot;         /* slot in ControlLogix Backplane: 0, ... */
    size_t        plc_errors;   /* # of communication errors              */
    size_t        slow_scans;   /* Count: scan task is getting late       */
    EIPConnection connection;
    DL_List /*ScanList*/ scanlists;
    epicsThreadId scan_task_id;
};

/* ScanList:
 * A list of TagInfos,
 * to be scanned at the same rate
 */
struct __ScanList
{
    DLL_Node node;
    PLC      *plc;                    /* PLC this Scanlist belongs to */
    eip_bool enabled;
    double   period;                  /* scan period [secs]  */
    double   period_time;             /* corresponding converted delay */
    size_t   list_errors;             /* # of communication errors */
    epicsTimeStamp    scan_time;      /* last run time */
    epicsTimeStamp    scheduled_time; /* next run time */
    double   min_scan_time;           /* statistics: scan time */
    double   max_scan_time;           /* minimum, maximum, */
    double   last_scan_time;          /* and most recent scan */
    DL_List /*TagInfo*/ taginfos;
};

typedef void (*EIPCallback) (void *arg);
typedef struct
{
    DLL_Node node;
    EIPCallback callback;       /* called for each value */
    void       *arg;
}   TagCallback;

/* TagInfo:
 * Information for a single tag:
 * Actual tag, how many elements are requested,
 * size for request/response, ...
 *
 * TagInfos are held in list.
 * Naming in here: "tag" is a single TagInfo,
 * "tags" is used when the whole list is meant.
 *
 * A cip_request_size of 0 will cause this tag
 * to be skipped in read/write operations.
 *
 * See Locking info in drvEtherIP.c for details
 * on locking as well as cip_request/response size
 * and the do_write flag.
 */
struct __TagInfo
{
    DLL_Node   node;
    ScanList   *scanlist;          /* list this tag in on */
    char       *string_tag;        /* tag as text */
    ParsedTag  *tag;               /* tag, compiled */
    size_t     elements;           /* array elements to read (or 1) */
    size_t     cip_r_request_size; /* byte-size of read request */
    size_t     cip_r_response_size;/* byte-size of read response */
    size_t     cip_w_request_size; /* byte-size of write request */
    size_t     cip_w_response_size;/* byte-size of write response */
    epicsEventId data_lock;        /* see "locking" in drvEtherIP.c */
    size_t     data_size;          /* total size of data buffer */
    size_t     valid_data_size;    /* used portion of data, 0 for "invalid" */
    eip_bool   do_write;           /* set by device, reset by driver */
    eip_bool   is_writing;         /* driver copy of do_write for cycle */
    CN_USINT   *data;              /* CIP data (type, raw data) */
    double     transfer_time;      /* time needed for last transfer */
    DL_List    callbacks;          /* TagCallbacks for new values&write done */
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern double drvEtherIP_default_rate;

void drvEtherIP_help();

void drvEtherIP_init();

long drvEtherIP_report(int level);

void drvEtherIP_dump();

void drvEtherIP_reset_statistics();

eip_bool drvEtherIP_define_PLC(const char *PLC_name,
                           const char *ip_addr, int slot);

PLC *drvEtherIP_find_PLC(const char *PLC_name);

TagInfo *drvEtherIP_add_tag(PLC *plc, double period,
                            const char *string_tag, size_t elements);
/* Register callbacks for "received new data" and "finished the write".
 * Note: The data is already locked (data_lock taken)
 * when the callback is called!
 */
void drvEtherIP_add_callback(PLC *plc, TagInfo *tag,
                             EIPCallback callback, void *arg);
void drvEtherIP_remove_callback(PLC *plc, TagInfo *tag,
                                EIPCallback callback, void *arg);

int drvEtherIP_restart();

/* Command-line communication test,
 * not used by the driver */
int drvEtherIP_read_tag(const char *ip_addr,
                        int slot,
                        const char *tag_name,
                        int elements,
                        int timeout);

#if EPICS_VERSION >= 3 && EPICS_REVISION >= 14
void drvEtherIP_Register();
#endif
  
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

