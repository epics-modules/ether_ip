/* drvEtherIP
 *
 * IOC driver that uses ether_ip routines,
 * keeping lists of PLCs and tags and scanlists etc.
 *
 * kasemir@lanl.gov
 */

#ifndef ETHERIP_MAYOR

#include "R314Compat.h"
#include "ether_ip.h"
#include "dl_list.h"

#define ETHERIP_MAYOR 2
#define ETHERIP_MINOR 26

/* For timing */
#define EIP_MIN_TIMEOUT         0.1  /* second */
#define EIP_MIN_CONN_TIMEOUT    1.0  /* second */

/* TCP port */
#define ETHERIP_PORT 0xAF12

/* TCP timeout in millisec for connection and readback */
#define ETHERIP_TIMEOUT 5000

typedef struct __TagInfo  TagInfo;  /* forwards */
typedef struct __ScanList ScanList;
typedef struct __PLC      PLC;

/* THE singleton main structure for this driver
 * Note that each PLC entry has it's own lock
 * for the scanlists & statistics.
 * Each PLC's scan task uses that per-PLC lock,
 * calls to loop/add/list PLCs also use this
 * more global lock.
 */
typedef struct
{
    DL_List      PLCs; /* List of PLC structs */
    epicsMutexId lock;
} DrvEtherIP_Private;

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
    EIPConnection *connection;
    DL_List       scanlists;    /* List of struct ScanList */
    epicsThreadId scan_task_id;
};

/* ScanList:
 * A list of TagInfos,
 * to be scanned at the same rate
 */
struct __ScanList
{
    DLL_Node       node;
    PLC            *plc;            /* PLC to which this Scanlist belongs */
    eip_bool       enabled;
    double         period;          /* scan period [secs]  */
    size_t         list_errors;     /* # of communication errors */
    size_t         sched_errors;    /* # of scheduling errors */
    epicsTimeStamp scan_time;       /* stamp of last run time */
    epicsTimeStamp scheduled_time;  /* stamp for next run time */
    double         min_scan_time;   /* statistics: scan time in seconds */
    double         max_scan_time;   /* minimum, maximum, */
    double         last_scan_time;  /* and most recent scan */
    DL_List        taginfos;        /* List of struct TagInfo */
};

typedef void (*EIPCallback) (void *arg);
typedef struct
{
    DLL_Node node;
    EIPCallback callback; /* called for each value */
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
    epicsMutexId data_lock;        /* see "locking" in drvEtherIP.c */
    size_t     data_size;          /* total size of data buffer */
    size_t     valid_data_size;    /* used portion of data, 0 for "invalid" */
    eip_bool   do_write;           /* set by device, reset by driver */
    eip_bool   is_writing;         /* driver copy of do_write for cycle */
    CN_USINT   *data;              /* CIP data (type, raw data), with buffer capacity of data_size */
    double     transfer_time;      /* time needed for last transfer */
    DL_List    callbacks;          /* TagCallbacks for new values&write done */
};

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef HAVE_314_API
void drvEtherIP_Register();
#endif

#ifdef __cplusplus
}
#endif

#endif

