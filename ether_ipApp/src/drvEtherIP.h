/* $Id$
 *
 * drvEtherIP
 *
 * vxWorks driver that uses ether_ip routines,
 * keeping lists of PLCs and tags and scanlists etc.
 *
 * kasemir@lanl.gov
 */

#ifndef ETHERIP_MAYOR

#include <taskLib.h>
#include "ether_ip.h"
#include "dl_list.h"

#define ETHERIP_MAYOR 1
#define ETHERIP_MINOR 10

/* TCP port */
#define ETHERIP_PORT 0xAF12

/* TCP timeout in millisec for connection and readback */
#define ETHERIP_TIMEOUT 5000

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

/* semaphore options (mutex) */
#define EIP_SEMOPTS (SEM_Q_PRIORITY | SEM_DELETE_SAFE |SEM_INVERSION_SAFE)
#define EIP_SEM_TIMEOUT sysClkRateGet()

typedef struct __TagInfo  TagInfo;  /* forwards */
typedef struct __ScanList ScanList;
typedef struct __PLC      PLC;

/* PLCInfo:
 * Per-PLC information
 * Generated with call to drvEtherIP_define_PLC
 * in vxWorks startup file.
 *
 * Holds
 * - EIPConnection for ether_ip protocol routines
 * - ScanList for this PLC, filled by device support
 */
struct __PLC
{
    DLL_Node      node;
    SEM_ID        lock;
    char          *name;        /* symbolic name, used to identify PLC    */
    char          *ip_addr;     /* IP or DNS name that IOC/vxWorks knows  */
    int           slot;         /* slot in ControlLogix Backplane: 0, ... */
    size_t        plc_errors;   /* # of communication errors              */
    size_t        slow_scans;   /* Count: scan task is getting late       */
    EIPConnection connection;
    DL_List /*ScanList*/ scanlists;
    int           scan_task_id;
};

/* ScanList:
 * A list of TagInfos,
 * to be scanned at the same rate
 */
struct __ScanList
{
    DLL_Node node;
    PLC      *plc;               /* PLC this Scanlist belongs to */
    bool     enabled;
    double   period;             /* scan period [secs] */
    size_t   period_ticks;       /* corresponding delay in ticks */
    size_t   list_errors;        /* # of communication errors */
    ULONG    scan_ticktime;      /* last run in ticktime */
    ULONG    scheduled_ticktime; /* next run in ticktime */
    size_t   min_scan_ticks;     /* statistics: scan times in ticks */
    size_t   max_scan_ticks;     /* minimum, maximum, */
    size_t   last_scan_ticks;    /* and most recent scan */
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
    SEM_ID     data_lock;          /* see "locking" in drvEtherIP.c */
    size_t     data_size;          /* total size of data buffer */
    size_t     valid_data_size;    /* used portion of data, 0 for "invalid" */
    bool       do_write;           /* set by device, reset by driver */
    bool       is_writing;         /* driver copy of do_write for cycle */
    CN_USINT   *data;              /* CIP data (type, raw data) */
    size_t     transfer_ticktime;  /* ticks needed for last transfer */
    DL_List    callbacks;          /* TagCallbacks for new values&write done */
};

extern double drvEtherIP_default_rate;

void drvEtherIP_help();

void drvEtherIP_init();

bool drvEtherIP_define_PLC(const char *PLC_name,
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

#endif

