/* devEtherIP
 *
 * EPICS Device Support,
 * glue between EtherIP driver and records.
 *
 * kasemirk@ornl.gov
 */

/* System */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <string.h>
/* Base */
#include <alarm.h>
#include <cvtTable.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <devLib.h>
#include <link.h>
#include <dbScan.h>
#include <menuConvert.h>
#include <menuOmsl.h>
#include <aiRecord.h>
#include <biRecord.h>
#include <mbbiRecord.h>
#include <mbbiDirectRecord.h>
#include <stringinRecord.h>
#include <waveformRecord.h>
#include <menuFtype.h>
#include <aoRecord.h>
#include <boRecord.h>
#include <mbboRecord.h>
#include <mbboDirectRecord.h>
#include <stringoutRecord.h>
#include <errlog.h>

#ifdef BUILD_LONG_STRING_SUPPORT
#include <dbmf.h>
#include <lsiRecord.h>
#include <lsoRecord.h>
#endif

/* Local */
#include "drvEtherIP.h"

#ifdef SUPPORT_LINT
#include <int64inRecord.h>
#include <int64outRecord.h>
#endif

/* Base */
#  include "epicsExport.h"

/* Flags that pick special values instead of the tag's "value" */
typedef enum
{
    SPCO_READ_SINGLE_ELEMENT = (1<<0),
    SPCO_SCAN_PERIOD         = (1<<1),
    SPCO_BIT                 = (1<<2),
    SPCO_FORCE               = (1<<3),
    SPCO_INDEX_INCLUDED      = (1<<4),
    SPCO_PLC_ERRORS          = (1<<6),
    SPCO_PLC_TASK_SLOW       = (1<<7),
    SPCO_LIST_ERRORS         = (1<<8),
    SPCO_LIST_TICKS          = (1<<9),
    SPCO_LIST_SCAN_TIME      = (1<<10),
    SPCO_LIST_MIN_SCAN_TIME  = (1<<11),
    SPCO_LIST_MAX_SCAN_TIME  = (1<<12),
    SPCO_TAG_TRANSFER_TIME   = (1<<13),
    SPCO_LIST_TIME           = (1<<14),
    SPCO_INVALID             = (1<<15)
} SpecialOptions;

static struct
{
    const char *text;
    SpecialOptions mask;
} special_options[] =
{
  { "E",                  SPCO_READ_SINGLE_ELEMENT }, /* Force a SCAN for a single element */
  { "S ",                 SPCO_SCAN_PERIOD        }, /* note <space> Set SCAN period for I/O */
  { "B ",                 SPCO_BIT                }, /* note <space>  Select Bit out of element */
  { "FORCE",              SPCO_FORCE              }, /* Force output records to write when!=tag */
  { "PLC_ERRORS",         SPCO_PLC_ERRORS         }, /* Connection error count for tag's PLC */
  { "PLC_TASK_SLOW",      SPCO_PLC_TASK_SLOW      }, /* How often scan task had no time to wait */
  { "LIST_ERRORS",        SPCO_LIST_ERRORS        }, /* Error count for tag's list */
  { "LIST_TICKS",         SPCO_LIST_TICKS         }, /* 3.13-Ticktime when tag's list was checked */
  { "LIST_SCAN_TIME",     SPCO_LIST_SCAN_TIME     }, /* Time for handling scanlist */
  { "LIST_MIN_SCAN_TIME", SPCO_LIST_MIN_SCAN_TIME }, /* min. of '' */
  { "LIST_MAX_SCAN_TIME", SPCO_LIST_MAX_SCAN_TIME }, /* max. of '' */
  { "TAG_TRANSFER_TIME",  SPCO_TAG_TRANSFER_TIME  }, /* Time for last round-trip data request */
  { "LIST_TIME",          SPCO_LIST_TIME          }, /* 3.14-# of seconds since 0000 Jan 1, 1990 */
  { "",                   0                       }, /*      when tag's list was checked */
};

/* Device Private:
 * Link text is kept to check for changes when record is processed
 * (faster than re-parsing every time).
 *
 * Note on element for binary records:
 * We assume that tag refers to BOOL array,
 * so the original element that refers to a bit #
 * is transformed into
 * 1) element in the UDINT array
 * 2) mask then points to the bit inside that UDINT
 */
typedef struct
{
    char           *link_text;  /* Original text of INST_IO link  */
    char           *PLC_name;   /* PLC, */
    char           *string_tag; /* tag, */
    size_t         element;     /* array element parsed from that: 0, 1,... */
    CN_UDINT       mask;        /* For binaries: first bit of interest */
    SpecialOptions special;
    PLC            *plc;
    TagInfo        *tag;
    IOSCANPVT      ioscanpvt;
}   DevicePrivate;

/* Call to this routine can be triggered via TPRO field */
static void dump_DevicePrivate(const dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    if (!pvt)
    {
        printf("   Device Private = 0\n");
        return;
    }
    printf("   link_text  : '%s'\n",  pvt->link_text);
    printf("   PLC_name   : '%s'\n",  pvt->PLC_name);
    printf("   string_tag : '%s', element %d\n",
           pvt->string_tag, (int)pvt->element);
    printf("   mask       : 0x%08X    spec. opts.: %d\n",
           pvt->mask, pvt->special);
    printf("   plc        : 0x%lX    tag        : 0x%lX\n",
           (unsigned long)pvt->plc, (unsigned long)pvt->tag);
}

/* Helper: check for valid DevicePrivate, lock data
 * and see if it's valid */
static eip_bool lock_data(const dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    if (!(pvt && pvt->plc && pvt->tag && pvt->tag->scanlist))
    {
        if (rec->sevr != INVALID_ALARM) /* don't flood w/ messages */
            printf("devEtherIP lock_data (%s): no tag\n", rec->name);
        return false;
    }
    if (epicsMutexLock(pvt->tag->data_lock) != epicsMutexLockOK)
    {
        if (rec->sevr != INVALID_ALARM) /* don't flood w/ messages */
            printf("devEtherIP lock_data (%s): no lock\n", rec->name);
        return false;
    }
    if (pvt->tag->valid_data_size <= 0  ||
        pvt->tag->elements <= pvt->element)
    {
        epicsMutexUnlock(pvt->tag->data_lock);
        if (rec->tpro &&
            rec->sevr != INVALID_ALARM) /* don't flood w/ messages */
            printf("devEtherIP lock_data (%s): no data\n", rec->name);
        return false;
    }
    return true;
}

/* Like lock_data, but w/o the lock because
 * data_lock is already taken in callbacks
 */
static eip_bool check_data(const dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    return pvt && pvt->plc && pvt->tag && pvt->tag->scanlist &&
        pvt->tag->valid_data_size > 0  &&
        pvt->tag->elements > pvt->element;
}

/* Helper for (multi-bit) binary type records:
 * Get bits from driver, pack them into rval
 *
 * Does not take/give data lock because output records
 * might need it after looking at the value.
 *
 * The mbbx's MASK is not used because
 * 1) NOBT might change but MASK is only set once
 * 2) MASK doesn't help when reading bits accross UDINT boundaries
 */
static eip_bool get_bits(dbCommon *rec, size_t bits, epicsUInt32 *rval)
{
    DevicePrivate  *pvt = (DevicePrivate *)rec->dpvt;
    size_t         i, element = pvt->element;
    CN_UDINT       value, mask = pvt->mask;

    *rval   = 0;
    if (!get_CIP_UDINT(pvt->tag->data, element, &value))
    {
        errlogPrintf("EIP get_bits(%s), element %d failed\n",
                     rec->name, (int)element);
        return false;
    }
    /* Fetch bits from BOOL array.
     * #1 directly (faster for BI case), rest in loop */
    if ((value & 0x1) & mask)
        *rval = 1;
    for (i=1/*!*/; i<bits; ++i)
    {
        mask <<= 1;
        if (mask == 0) /* end of current UDINT ? */
        {
            mask = 1;
            ++element;
            if (!get_CIP_UDINT(pvt->tag->data, element, &value))
            {
                errlogPrintf("EIP get_bits(%s), element %d failed\n",
                       rec->name, (int)element);
                return false;
            }
        }
        if (value & mask)
            *rval |= 1 << (epicsUInt32)i;
    }
    return true;
}

/* Pendant to get_bits */
static eip_bool put_bits(dbCommon *rec, size_t bits, epicsUInt32 rval)
{
    DevicePrivate  *pvt = (DevicePrivate *)rec->dpvt;
    size_t         i, element = pvt->element;
    CN_UDINT       value, mask = pvt->mask;

    if (! get_CIP_UDINT(pvt->tag->data, element, &value))
    {
        errlogPrintf("EIP put_bits(%s), element %d failed\n",
                     rec->name, (int)element);
        return false;
    }
    /* Transfer bits into BOOL array.
     * First one directly (faster for BI case), rest in loop */
    if (rval & 1)
        value |= mask;
    else
        value = (value | mask) ^ mask;        /* Force the bit ON, then turn it off */
    for (i=1/*!*/; i<bits; ++i)
    {
        rval >>= 1;
        mask <<= 1;
        if (mask == 0) /* end of current UDINT ? */
        {
            if (! put_CIP_UDINT(pvt->tag->data, element, value))
            {
                errlogPrintf("EIP put_bits(%s), element %d failed\n",
                             rec->name, (int)element);
                return false;
            }
            mask = 1; /* reset mask, go to next element */
            ++element;
            if (! get_CIP_UDINT(pvt->tag->data, element, &value))
            {
                errlogPrintf("EIP put_bits(%s), element %d failed\n",
                             rec->name, (int)element);
                return false;
            }
        }
        if (rval & 1)
            value |= mask;
        else
            value &= ~mask;
    }
    if (!put_CIP_UDINT(pvt->tag->data, element, value))
    {
        errlogPrintf("EIP put_bits(%s), element %d failed\n",
                     rec->name, (int)element);
        return false;
    }
    return true;
}

/* Callback, registered with drvEtherIP, for input records.
 * Used _IF_ scan="I/O Event":
 * Driver has new value (or no value because of error), process record
 */
static void scan_callback(void *arg)
{
    dbCommon      *rec = (dbCommon *) arg;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    if (rec->tpro)
        printf("EIP scan_callback('%s')\n", rec->name);
    scanIoRequest(pvt->ioscanpvt);
}

/* Callback from driver for every received tag, for ao record:
 * Check if
 *
 * 1) pact set -> this is the "finshed the write" callback
 * 2) pact not set -> this is the "new value" callback
 *    Tag value is either different from current record value
 *    or there is no current record value:
 * 2a) disconnected; process record to set WRITE/INVALID
 * 2b) PLC's value != record's idea of the current value
 * 2c) record is UDF, so this is the first time we get a value
 *     from the PLC after a reboot
 * Causing process if necessary to update the record.
 * That process()/scanOnce() call should NOT cause the record to write
 * to the PLC because the xxx_write method will notice that
 * the PLC and record value agree.
 * It will, however, trigger CA monitors.
 *
 * Problem: Alarms are handled before "write_xx" is called.
 * So we have to set udf in here,
 * then process() can recognize udf and finally it will
 * call write_xx.
 */
static void check_ao_callback(void *arg)
{
    aoRecord      *rec = (aoRecord *) arg;
    rset          *rset = rec->rset;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    double        dbl;
    CN_DINT       dint;
    eip_bool      process = false;

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        if (rec->tpro)
            printf("EIP check_ao_callback('%s'), pact=%d\n",
                   rec->name, rec->pact);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data((dbCommon *)rec))
    {
        if (rec->tpro)
            printf("EIP check_ao_callback('%s'), no data\n", rec->name);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    if (get_CIP_typecode(pvt->tag->data) == T_CIP_REAL  ||
        get_CIP_typecode(pvt->tag->data) == T_CIP_LREAL)
    {
        if (rec->tpro)
            printf("EIP check_ao_callback('%s') w/ real data\n", rec->name);
        if (get_CIP_double(pvt->tag->data, pvt->element, &dbl) &&
            (rec->udf || rec->sevr == INVALID_ALARM || rec->val != dbl))
        {
            if (rec->tpro)
                printf("'%s': got %g from driver\n", rec->name, dbl);
            if (!rec->udf  &&  pvt->special & SPCO_FORCE)
            {
                if (rec->tpro)
                    printf("'%s': will re-write record's value %g\n",
                           rec->name, rec->val);
            }
            else
            {
                rec->val = rec->pval = dbl;
                rec->udf = false;
                if (rec->tpro)
                    printf("'%s': updated record's value %g\n",
                           rec->name, rec->val);
            }
            process = true;
        }
    }
    else
    {
        if (rec->tpro)
            printf("EIP check_ao_callback('%s') w/ int. data\n", rec->name);
        if (get_CIP_DINT(pvt->tag->data, pvt->element, &dint) &&
            (rec->udf || rec->sevr == INVALID_ALARM || rec->rval != dint))
        {
            if (rec->tpro)
                printf("AO '%s': got %d from driver\n",
                       rec->name, dint);
            if (!rec->udf  &&  pvt->special & SPCO_FORCE)
            {
                if (rec->tpro)
                    printf("AO '%s': will re-write record's rval 0x%X\n",
                           rec->name, (unsigned int)rec->rval);
            }
            else
            {
                /* back-convert raw value into val (copied from ao init) */
                dbl = (double)dint + (double)rec->roff;
                if (rec->aslo!=0.0)
                    dbl *= rec->aslo;
                dbl += rec->aoff;
                switch (rec->linr)
                {
                    case menuConvertNO_CONVERSION:
                        rec->val = rec->pval = dbl;
                        rec->udf = false;
                        break;
                    case menuConvertLINEAR:
                    case menuConvertSLOPE:
                        dbl = dbl*rec->eslo + rec->eoff;
                        rec->val = rec->pval = dbl;
                        rec->udf = false;
                        break;
                    default:
                        if (cvtRawToEngBpt(&dbl,rec->linr,rec->init,
                                           (void *)&rec->pbrk,
                                           &rec->lbrk)!=0)
                            break; /* cannot back-convert */
                        rec->val = rec->pval = dbl;
                        rec->udf = false;
                }
                if (rec->tpro)
                    printf("'%s': updated record's value to %g\n",
                           rec->name, rec->val);
            }
            process = true;
        }
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}

#ifdef SUPPORT_LINT
/* Callback for int64out */
static void check_int64out_callback(void *arg)
{
    int64outRecord   *rec = (int64outRecord *) arg;
    rset             *rset = rec->rset;
    DevicePrivate    *pvt = (DevicePrivate *)rec->dpvt;
    CN_LINT          val;
    eip_bool         process = false;

    /* We are about the check and even set val -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's VAL is current */
    if (!check_data((dbCommon *) rec))
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Compare received value with record */
    if (get_CIP_LINT(pvt->tag->data, pvt->element, &val) &&
        (rec->udf || rec->sevr == INVALID_ALARM || rec->val != val))
    {
        if (rec->tpro)
            printf("'%s': got %lld from driver\n", rec->name, val);
        if (!rec->udf && pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': will re-write record's value %lld\n", rec->name, rec->val);
        }
        else
        {
            rec->val = val;
            rec->udf = false;
            if (rec->tpro)
                printf("'%s': updated record's value to %lld\n", rec->name, rec->val);
        }
        process = true;
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}
#endif


/* Callback for bo, see ao_callback comments */
static void check_bo_callback(void *arg)
{
    boRecord      *rec = (boRecord *) arg;
    rset          *rset = rec->rset;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32   rval;
    eip_bool      process = false;

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data((dbCommon *) rec))
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    if (get_bits((dbCommon *)rec, 1, &rval) &&
        (rec->udf || rec->sevr == INVALID_ALARM || rec->rval != rval))
    {
        if (rec->tpro)
            printf("'%s': got %u from driver\n", rec->name, rval);
        if (!rec->udf  &&  pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': will re-write record's value %u\n",
                       rec->name, (unsigned int)rec->val);
        }
        else
        {   /* back-convert rval into val */
            rec->rval = rval;
            rec->val  = (rec->rval==0) ? 0 : 1;
            rec->udf = false;
            if (rec->tpro)
                printf("'%s': updated record to tag, val = %u\n",
                       rec->name, (unsigned int)rec->val);
        }
        process = true;
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}

#ifdef BUILD_LONG_STRING_SUPPORT
/* callback for lso record */
static void check_lso_callback(void *arg)
{
    lsoRecord *rec = (lsoRecord *) arg;
    rset          *rset = rec->rset;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool      process = false;
    char          *data = NULL;

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        if (rec->tpro)
            printf("EIP check_lso_callback('%s'), pact=%d\n",
                   rec->name, rec->pact);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data((dbCommon *)rec))
    {
        if (rec->tpro)
            printf("EIP check_lso_callback('%s'), no data\n", rec->name);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    data = dbmfMalloc(rec->sizv);
    if (get_CIP_STRING(pvt->tag->data, data, rec->sizv) &&
            (rec->udf || rec->sevr == INVALID_ALARM || strcmp(rec->val, data)))
    {
        if (rec->tpro)
            printf("'%s': got %s from driver\n", rec->name, data);
        if (!rec->udf && pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': will re-write record's value %s\n", rec->name,
                        rec->val);
        }
        else {
            strcpy(rec->val, data);
            rec->udf = false;
            if (rec->tpro)
                printf("'%s': updated record's value %s\n", rec->name, rec->val);
        }
        process = true;
    }
    dbmfFree(data);
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}
#endif

/* Callback for mbbo */
static void check_mbbo_callback(void *arg)
{
    mbboRecord    *rec = (mbboRecord *) arg;
    rset          *rset = rec->rset;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32   rval, *state_val;
    size_t        i;
    eip_bool      process = false;

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data ((dbCommon *) rec))
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    if (get_bits ((dbCommon *)rec, rec->nobt, &rval) &&
        (rec->udf || rec->sevr == INVALID_ALARM || rec->rval != rval))
    {
        if (rec->tpro)
            printf("'%s': got %u from driver\n", rec->name, rval);
        if (!rec->udf  &&  pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': will re-write record's rval 0x%X\n",
                       rec->name, (unsigned int)rec->rval);
        }
        else
        {   /* back-convert rval into val */
            if (rec->sdef)
            {
                rec->val  = 65535;  /* initalize to unknown state*/
                state_val = (epicsUInt32 *) &rec->zrvl;
                for (i=0; i<16; ++i)
                {
                    if (*state_val == rval)
                    {
                        rec->val = i;
                        break;
                    }
                    state_val++;
                }
                rec->udf = false;
            }
            else
            {
                /* the raw value is the desired val */
                rec->val = (unsigned short)rval;
                rec->udf = false;
            }
            if (rec->tpro)
                printf("--> val = %u\n", (unsigned int)rec->val);
        }
        process = true;
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce ((dbCommon *)rec);
}

/* Callback for mbboDirect */
static void check_mbbo_direct_callback(void *arg)
{
    mbboDirectRecord *rec = (mbboDirectRecord *) arg;
    rset             *rset = rec->rset;
    DevicePrivate    *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32      rval;
    eip_bool         process = false;

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data((dbCommon *) rec))
    {
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    if (get_bits((dbCommon *)rec, rec->nobt, &rval) &&
        (rec->udf || rec->sevr == INVALID_ALARM || rec->rval != rval))
    {
        if (rec->tpro)
            printf("'%s': got %u from driver\n",
                   rec->name, rval);
        if (!rec->udf  &&  pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': re-write record's rval 0x%X\n",
                       rec->name, (unsigned int)rec->rval);
        }
        else
        {
            rec->rval= rval;
            rec->val = (epicsUInt16) rval;
            rec->udf = false;

            if (rec->omsl == menuOmslsupervisory)
            {   /* Record's process routine will read B0, B1, .. into VAL.
                 * Update those to prevent clobbering VAL just fetched from the PLC.
                 */
                unsigned int i;
                epicsUInt8 *bits = &rec->b0;
                epicsUInt16 mask = 1;
                for (i=0;  i<16;  ++i, mask<<=1)
                    bits[i] = !!(rval & mask);
            }
        }
        process = true;
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}

/* callback for stringout record */
static void check_so_callback(void *arg)
{
    stringoutRecord *rec = (stringoutRecord *) arg;
    rset          *rset = rec->rset;
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool      process = false;
    char          data[MAX_STRING_SIZE];

    /* We are about the check and even set val, & rval -> lock */
    dbScanLock((dbCommon *)rec);
    if (rec->pact)
    {
        if (rec->tpro)
            printf("EIP check_so_callback('%s'), pact=%d\n",
                   rec->name, rec->pact);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Check if record's (R)VAL is current */
    if (!check_data((dbCommon *)rec))
    {
        if (rec->tpro)
            printf("EIP check_so_callback('%s'), no data\n", rec->name);
        rset->process((dbCommon *)rec);
        dbScanUnlock((dbCommon *)rec);
        return;
    }
    /* Get a null-terminated string from CIP data,
     * compare with record which we force to be terminated.
     */
    rec->val[MAX_STRING_SIZE-1] = '\0';
    if (get_CIP_STRING(pvt->tag->data, data, MAX_STRING_SIZE) &&
        (rec->udf || rec->sevr == INVALID_ALARM || strcmp(rec->val, data)))
    {
        if (rec->tpro)
            printf("'%s': got %s from driver\n", rec->name, data);
        if (!rec->udf && pvt->special & SPCO_FORCE)
        {
            if (rec->tpro)
                printf("'%s': will re-write record's value %s\n", rec->name, rec->val);
        }
        else
        {
            strcpy(rec->val, data);
            rec->udf = false;
            if (rec->tpro)
                printf("'%s': updated record's value %s\n", rec->name, rec->val);
        }
        process = true;
    }
    dbScanUnlock((dbCommon *)rec);
    /* Does record need processing and is not periodic? */
    if (process && rec->scan < SCAN_1ST_PERIODIC)
        scanOnce((dbCommon *)rec);
}

/* device support routine get_ioint_info */
static long get_ioint_info(int cmd, dbCommon *rec, IOSCANPVT *ppvt)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    if (pvt)
        *ppvt = pvt->ioscanpvt;
    return 0;
}

/* Try to parse scan period from record's SCAN field.
 * Sounds simple, but I ended up with this mess.
 * At DB runtime, scanPeriod(rec->scan) would return
 * the scan period in seconds, but at record initialization
 * time scanPeriod() doesn't work, so we have to get the
 * SCAN field text and parse the seconds ourselves.
 *
 * Returns <= 0 for error.
 */
static double get_period(dbCommon *rec)
{
    /** Buffer that can hold PV name plus ".SCAN" as well as SCAN field values */
    char          buf[PVNAME_STRINGSZ + 10];
    char          *p;
    size_t        len;
    struct dbAddr scan_field;
    long          options=0, count=1;
    double        period = -1.0;

    if (rec->scan < SCAN_1ST_PERIODIC)
        return period;

    len = strlen(rec->name);
    if (sizeof(buf) < len+6)
    {
        EIP_printf(1, "EIP record name '%s' too long to access SCAN field\n", rec->name);
        return period;
    }
    memcpy(buf, rec->name, len);
    memcpy(buf+len, ".SCAN", 6);
    if (dbNameToAddr(buf, &scan_field) != 0)
    {
        EIP_printf(1, "EIP cannot locate '%s'\n", buf);
        return period;
    }

    len = dbBufferSize(DBR_STRING, options, count);
    if (sizeof(buf) <= len)
    {
        EIP_printf(1, "EIP value of '%s' too long\n", buf);
        return period;
    }
    if (dbGet(&scan_field, DBR_STRING, buf, &options, &count, 0) != 0)
    {
        EIP_printf(1, "EIP cannot read '%s'\n", buf);
        return period;
    }
    if (strstr(buf, "second"))
    {
        period = strtod(buf, &p);
        if (p==buf || period==HUGE_VAL || period==-HUGE_VAL)
            period = -1.0;
    }
    EIP_printf(8, "EIP record '%s' scans at %.1lf secs\n", rec->name, period);
    return period;
}

/* Given string "s", return next token and end of token
 * Returns 0 on end-of-string
 */
static char *find_token(char *s, char **end)
{
    /* skip initial space  */
    while (*s == ' ')
        ++s;

    if (*s == '\0')
        return 0;

    *end = strchr(s, ' ');
    if (*end == NULL)
        *end = s + strlen(s);

    return s;
}

/* This driver uses INST_IO links,
 * the string has to be of the format
 *
 *      @PLC tag flags
 *
 * count: number of array elements to read (only != 1 for waveform record)
 * bits: if >0 indicates that we want binary data,
 *       then it's the number of bits
 */
static long analyze_link(dbCommon *rec,
                         EIPCallback cbtype,
                         const DBLINK *link,
                         size_t count,
                         size_t bits)
{
    DevicePrivate  *pvt = (DevicePrivate *)rec->dpvt;
    char           *p, *end;
    size_t         i, tag_len, last_element, bit=0;
    double         period = 0.0;
    eip_bool       single_element = false;

    if (pvt->link_text)
    {
        EIP_printf(3, "EIP link changed for record %s\n", rec->name);
        free(pvt->link_text);
        pvt->link_text = NULL;
    }
    pvt->link_text = EIP_strdup(link->value.instio.string);
    if (! pvt->link_text)
    {
        errlogPrintf("devEtherIP (%s): Cannot copy link\n", rec->name);
        return S_dev_noMemory;
    }
    /* Find PLC */
    p = find_token(pvt->link_text, &end);
    if (! p)
    {
        errlogPrintf("devEtherIP (%s): Missing PLC in link '%s'\n",
                     rec->name, pvt->link_text);
        return S_db_badField;
    }


    if (pvt->PLC_name && strncmp(pvt->PLC_name, p, end-p) )
    {
        EIP_printf(3, "EIP PLC changed for record %s\n", rec->name);
        free(pvt->PLC_name);
        pvt->PLC_name = NULL;
    }

    if(!pvt->PLC_name)
    {
        pvt->PLC_name = EIP_strdup_n(p, end-p);
        if (! pvt->PLC_name)
        {
            errlogPrintf("devEtherIP (%s): Cannot copy PLC\n", rec->name);
            return S_dev_noMemory;
        }
    }

    /* Find Tag */
    p = find_token(end, &end);
    if (! p)
    {
        errlogPrintf("devEtherIP (%s): Missing tag in link '%s'\n",
                     rec->name, pvt->link_text);
        return(S_db_badField);
    }
    tag_len = end-p;


    if (pvt->string_tag && strncmp(pvt->string_tag, p, tag_len) )
    {
        EIP_printf(3, "EIP tag changed for record %s\n", rec->name);
        free(pvt->string_tag);
        pvt->string_tag = NULL;
    }

    if(!pvt->string_tag)
    {
        pvt->string_tag = EIP_strdup_n(p, tag_len);
        if (! pvt->string_tag)
        {
            errlogPrintf("devEtherIP (%s): Cannot copy tag\n", rec->name);
            return S_dev_noMemory;
        }
    }

    /* Check for more flags */
    pvt->special = 0;  /* Init special options */
    while ((p = find_token(end, &end)))
    {
        for (i=0;
             special_options[i].mask;
             ++i)
        {
            if (strncmp(p,
                        special_options[i].text,
                        strlen(special_options[i].text) ) == 0)
            {
                pvt->special |= special_options[i].mask;
                if (special_options[i].mask==SPCO_READ_SINGLE_ELEMENT)
                {
                    if (count != 1)
                    {
                        errlogPrintf("devEtherIP (%s): "
                                     "Array record cannot use 'E' flag "
                                     "('%s')\n",
                                     rec->name, pvt->link_text);
                        return S_db_badField;
                    }
                    single_element = true;
                }
                else if (special_options[i].mask==SPCO_SCAN_PERIOD)
                {
                    period = strtod(p+2, &end);
                    if (end==p || period==HUGE_VAL || period==-HUGE_VAL)
                    {
                        errlogPrintf("devEtherIP (%s): "
                                     "Error in scan flag in link '%s'\n",
                                     rec->name, pvt->link_text);
                        return S_db_badField;
                    }
                }
                else if (special_options[i].mask==SPCO_BIT)
                {
                    bit = strtod(p+2, &end);
                    if (end==p || period==HUGE_VAL || period==-HUGE_VAL)
                    {
                        errlogPrintf("devEtherIP (%s): "
                                     "Error in bit flag in link '%s'\n",
                                     rec->name, pvt->link_text);
                        return S_db_badField;
                    }
                }
                break;
            }
        }
        if (!special_options[i].mask)
        {
            errlogPrintf("devEtherIP (%s): Invalid flag '%s' in link '%s'\n",
                         rec->name, p, pvt->link_text);
            return S_db_badField;
        }
    }

    if (period <= 0.0) /* no scan flag-> get SCAN field: */
    {
        period = get_period(rec);
        if (period <= 0)
            period = drvEtherIP_default_rate;
        if (period <= 0)
        {
            errlogPrintf("devEtherIP (%s): cannot decode SCAN field,"
                         " no scan flag given\n", rec->name);
            period = 1.0; /* default scan rate */
            errlogPrintf("Device support will use the default of %g secs, ",
                         period);
            errlogPrintf("please complete the record configuration\n");
        }
    }

    /* Parsed link_text into PLC_name, string_tag, special flags.
     * Analyse further */
    pvt->element = 0;
    p = &pvt->string_tag[tag_len-1];
    if (*p == ']') /* array tag? */
    {
        if (! single_element)
        {   /* Cut "array_tag[el]" into "array_tag" + el */
            while (p > pvt->string_tag)
                if (*(--p) == '[')
                    break;
            if (p <= pvt->string_tag)
            {
                errlogPrintf("devEtherIP (%s): malformed array tag in '%s'\n",
                             rec->name, pvt->link_text);
                return S_db_badField;
            }
            /* read element number */
            pvt->element = strtol(p+1, &end, 0);
            if (end==p+1 || pvt->element==LONG_MAX || pvt->element==LONG_MIN)
            {
                errlogPrintf("devEtherIP (%s): malformed array tag in '%s'\n",
                             rec->name, pvt->link_text);
                return S_db_badField;
            }
            /* Show that this definition included an index reference */
            pvt->special |= SPCO_INDEX_INCLUDED;

            /* remove element number text from tag */
            *p = '\0';
        }
    }

    pvt->plc = drvEtherIP_find_PLC(pvt->PLC_name);
    if (! pvt->plc)
    {
        errlogPrintf("devEtherIP (%s): unknown PLC '%s'\n",
                     rec->name, pvt->PLC_name);
        return S_db_badField;
    }

    if (count > 1 && (bits > 0  || (pvt->special & SPCO_BIT)))
    {
        errlogPrintf("devEtherIP (%s): cannot access bits for array records\n",
                     rec->name);
        return S_db_badField;
    }

    if (bits>0 && !(pvt->special & SPCO_BIT))
    {
        /* This is defining a boolean object that didn't have a bit number */
        if(pvt->special & SPCO_INDEX_INCLUDED)
        {
            /* If an index was supplied, it's a BOOL array,
             * so the data is packed into UDINTs (CIP "BITS").
             * The actual element requested is the UDINT index,
             * not the bit#.
             * Pick the bits within the UDINT via the mask.
             */
            pvt->mask = 1U << (pvt->element & 0x1F); /* 0x1F == 31 */
        }
        else
        {
            /* There was no index, so it's just a plain BOOLEAN reference */
            pvt->mask = 255;
        }
        last_element = pvt->element + bits - 1;
        pvt->element >>= 5;
        last_element >>= 5;
    }
    else
    {   /* Default (no binary rec) OR bit flag was given:
         * Use the element as is, use bit # (default:0)   */
        last_element = pvt->element;
        pvt->mask = 1U << bit;
    }

    /* tell driver to read up to this record's elements */
    pvt->tag = drvEtherIP_add_tag(pvt->plc, period,
                                  pvt->string_tag,
                                  last_element+count);
    if (! pvt->tag)
    {
        errlogPrintf("devEtherIP (%s): cannot register tag '%s' with driver\n",
                     rec->name, pvt->string_tag);
        return S_db_badField;
    }

    if (cbtype == scan_callback)
    {   /* scan_callback only allowed for SCAN=I/O Intr */
        if (rec->scan == SCAN_IO_EVENT)
            drvEtherIP_add_callback(pvt->plc, pvt->tag,
                                    scan_callback, rec);
        else
            drvEtherIP_remove_callback(pvt->plc, pvt->tag,
                                       scan_callback, rec);
    }
    else
        drvEtherIP_add_callback(pvt->plc, pvt->tag, cbtype, rec);

    return 0;
}

/* Check if link still matches what was recorded in DevicePrivate.
 * On change, reparse and restart driver */
static long check_link(dbCommon *rec, EIPCallback cbtype,
                       const DBLINK *link, size_t count, size_t bits)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    long status;

    if (link->type != INST_IO)
    {
        errlogPrintf("devEtherIP (%s): INP is not INST_IO\n", rec->name);
        return S_db_badField;
    }
    if (strcmp(link->value.instio.string, pvt->link_text))
    {   /* Link has changed, start over */
        if (rec->tpro)
            printf("Rec '%s': EtherIP link has changed, restarting\n",
                   rec->name);
        rec->udf = TRUE;
        if (pvt->plc && pvt->tag)
            drvEtherIP_remove_callback(pvt->plc, pvt->tag, cbtype, rec);
        status = analyze_link(rec, cbtype, link, count, bits);
        if (status)
            return status;
        drvEtherIP_restart();
    }
    return 0;
}

/* device init: Start driver after all records registered  */
static eip_bool driver_started = false;
static long init(int run)
{
    if (run==1 && driver_started==false)
    {
        drvEtherIP_restart();
        driver_started = true;
    }

    return 0;
}

/* Common initialization for all record types */
static long init_record(dbCommon *rec, EIPCallback cbtype,
                        const DBLINK *link, size_t count, size_t bits)
{
    if (! drvEtherIP_initialized())
    {
        errlogPrintf("devEtherIP (%s): Failed to all drvEtherIP_init()\n", rec->name);
        return S_db_badField;
    }
    DevicePrivate *pvt = calloc (sizeof (DevicePrivate), 1);
    if (! pvt)
    {
        errlogPrintf("devEtherIP (%s): cannot allocate DPVT\n", rec->name);
        return S_dev_noMemory;
    }
    if (link->type != INST_IO)
    {
        errlogPrintf("devEtherIP (%s): INP is not INST_IO\n", rec->name);
        return S_db_badField;
    }
    scanIoInit(&pvt->ioscanpvt);
    rec->dpvt = pvt;

    return analyze_link(rec, cbtype, link, count, bits);
}

/* ---------------------------------- */

static long ai_add_record(dbCommon *rec)
{
    return init_record(rec, scan_callback, &((aiRecord *)rec)->inp, 1, 0);
}

static long del_scan_callback_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, scan_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt ai_ext = { ai_add_record, del_scan_callback_record };

static long ai_init(int run)
{
    if (run == 0)
        devExtend(&ai_ext);
    return init(run);
}

static long ai_init_record(dbCommon *rec)
{
    aiRecord *ai = (aiRecord *)rec;
    /* Make sure record processing routine does not perform any conversion*/
    if (ai->linr != menuConvertSLOPE)
        ai->linr = 0;
    return 0;
}

#ifdef SUPPORT_LINT
/* ---------------------------------- */

static long int64in_add_record(dbCommon *rec)
{
    return init_record(rec, scan_callback, &((int64inRecord *)rec)->inp, 1, 0);
}

static struct dsxt int64in_ext = { int64in_add_record, del_scan_callback_record };

static long int64in_init(int run)
{
    if (run == 0)
        devExtend(&int64in_ext);
    return init(run);
}

static long int64in_init_record(dbCommon *rec)
{
    return 0;
}
#endif

/* ---------------------------------- */

static long bi_add_record(dbCommon *rec)
{
    return init_record(rec, scan_callback, &((biRecord *)rec)->inp, 1, 1);
}

static struct dsxt bi_ext = { bi_add_record, del_scan_callback_record };

static long bi_init(int run)
{
    if (run == 0)
        devExtend(&bi_ext);
    return init(run);
}

static long bi_init_record(dbCommon *rec)
{
    /* Handled by bi_add_record */
    return 0;
}

/* ---------------------------------- */

#ifdef BUILD_LONG_STRING_SUPPORT
static long lsi_add_record(dbCommon *rec)
{
    return init_record(rec, scan_callback, &((lsiRecord *)rec)->inp, 1, 0);
}

static struct dsxt lsi_ext = { lsi_add_record, del_scan_callback_record };

static long lsi_init(int run)
{
    if (run == 0)
        devExtend(&lsi_ext);
    return init(run);
}

static long lsi_init_record(dbCommon *rec)
{
    return 0;
}
#endif

/* ---------------------------------- */

static long mbbi_add_record(dbCommon *rec)
{
    mbbiRecord *mbbi = (mbbiRecord *)rec;
    return init_record(rec, scan_callback, &mbbi->inp, 1, mbbi->nobt);
}

static struct dsxt mbbi_ext = { mbbi_add_record, del_scan_callback_record };

static long mbbi_init(int run)
{
    if (run == 0)
        devExtend(&mbbi_ext);
    return init(run);
}

static long mbbi_init_record(dbCommon *rec)
{
    mbbiRecord *mbbi = (mbbiRecord *)rec;
    mbbi->shft = 0;
    return 0;
}

/* ---------------------------------- */

static long mbbi_direct_add_record(dbCommon *rec)
{
    mbbiDirectRecord *mbbi = (mbbiDirectRecord *)rec;
    return init_record(rec, scan_callback, &mbbi->inp, 1, mbbi->nobt);
}

static struct dsxt mbbi_direct_ext = { mbbi_direct_add_record, del_scan_callback_record };

static long mbbi_direct_init(int run)
{
    if (run == 0)
        devExtend(&mbbi_direct_ext);
    return init(run);
}

static long mbbi_direct_init_record(dbCommon *rec)
{
    mbbiDirectRecord *mbbi = (mbbiDirectRecord *)rec;
    mbbi->shft = 0;
    return 0;
}

/* ---------------------------------- */

static long si_add_record(dbCommon *rec)
{
    return init_record(rec, scan_callback, &((stringinRecord *)rec)->inp, 1, 0);
}

static struct dsxt si_ext = { si_add_record, del_scan_callback_record };

static long si_init(int run)
{
    if (run == 0)
        devExtend(&si_ext);
    return init(run);
}

static long si_init_record(dbCommon *rec)
{
    return 0;
}

/* ---------------------------------- */

static long wf_add_record(dbCommon *rec)
{
    waveformRecord *wf = (waveformRecord *) rec;
    return init_record(rec, scan_callback, &wf->inp, wf->nelm, 0);
}

static struct dsxt wf_ext = { wf_add_record, del_scan_callback_record };

static long wf_init(int run)
{
    if (run == 0)
        devExtend(&wf_ext);
    return init(run);
}

static long wf_init_record(dbCommon *rec)
{
    return 0;
}

/* ---------------------------------- */

static long ao_add_record(dbCommon *rec)
{
    return init_record(rec, check_ao_callback, &((aoRecord *)rec)->out, 1, 0);
}

static long ao_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_ao_callback, rec);
    free(pvt);

    return 0;
}

static struct dsxt ao_ext = { ao_add_record, ao_del_record };

static long ao_init(int run)
{
    if (run == 0)
        devExtend(&ao_ext);
    return init(run);
}

static long ao_init_record(dbCommon *rec)
{
    return 2; /* don't convert, we have no value, yet */
}


#ifdef SUPPORT_LINT
/* ---------------------------------- */

static long int64out_add_record(dbCommon *rec)
{
    return init_record(rec, check_int64out_callback, &((int64outRecord *)rec)->out, 1, 0);
}

static long int64out_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_int64out_callback, rec);
    free(pvt);

    return 0;
}

static struct dsxt int64out_ext = { int64out_add_record, int64out_del_record };

static long int64out_init(int run)
{
    if (run == 0)
        devExtend(&int64out_ext);
    return init(run);
}

static long int64out_init_record(dbCommon *rec)
{
    return 0;
}
#endif

/* ---------------------------------- */

static long bo_add_record(dbCommon *rec)
{
    return init_record(rec, check_bo_callback, &((boRecord *)rec)->out, 1, 1);
}

static long bo_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_bo_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt bo_ext = { bo_add_record, bo_del_record };

static long bo_init(int run)
{
    if (run == 0)
        devExtend(&bo_ext);
    return init(run);
}

static long bo_init_record(dbCommon *rec)
{
    return 2; /* don't convert, we have no value, yet */
}

/* ---------------------------------- */

#ifdef BUILD_LONG_STRING_SUPPORT
static long lso_add_record(dbCommon *rec)
{
    return init_record(rec, check_lso_callback, &((lsoRecord *)rec)->out, 1, 0);
}

static long lso_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_lso_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt lso_ext = { lso_add_record, lso_del_record };

static long lso_init(int run)
{
    if (run == 0)
        devExtend(&lso_ext);
    return init(run);
}

static long lso_init_record(dbCommon *rec)
{
    return 2; /* don't convert, we have no value, yet */
}
#endif

/* ---------------------------------- */

static long mbbo_add_record(dbCommon *rec)
{
    mbboRecord *mbbo = (mbboRecord *)rec;
    return init_record(rec, check_mbbo_callback, &mbbo->out, 1, mbbo->nobt);
}

static long mbbo_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_mbbo_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt mbbo_ext = { mbbo_add_record, mbbo_del_record };

static long mbbo_init(int run)
{
    if (run == 0)
        devExtend(&mbbo_ext);
    return init(run);
}

static long mbbo_init_record(dbCommon *rec)
{
    mbboRecord *mbbo = (mbboRecord *)rec;
    mbbo->shft = 0;
    return 2; /* don't convert, we have no value, yet */
}

/* ---------------------------------- */

static long mbbo_direct_add_record(dbCommon *rec)
{
    mbboDirectRecord *mbbo = (mbboDirectRecord *)rec;
    return init_record(rec, check_mbbo_direct_callback, &mbbo->out, 1, mbbo->nobt);
}

static long mbbo_direct_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_mbbo_direct_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt mbbo_direct_ext = { mbbo_direct_add_record, mbbo_direct_del_record };

static long mbbo_direct_init(int run)
{
    if (run == 0)
        devExtend(&mbbo_direct_ext);
    return init(run);
}

static long mbbo_direct_init_record(dbCommon *rec)
{
    mbboDirectRecord *mbbo = (mbboDirectRecord *)rec;
    mbbo->shft = 0;
    return 2; /* don't convert, we have no value, yet */
}

/* ---------------------------------- */

static long so_add_record(dbCommon *rec)
{
    return init_record(rec, check_so_callback, &((stringoutRecord *)rec)->out, 1, 0);
}

static long so_del_record(dbCommon *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    printf("Updating link for %s\n", rec->name);
    if (pvt->plc && pvt->tag)
        drvEtherIP_remove_callback(pvt->plc, pvt->tag, check_so_callback, rec);
    free(pvt);
    return 0;
}

static struct dsxt so_ext = { so_add_record, so_del_record };

static long so_init(int run)
{
    if (run == 0)
        devExtend(&so_ext);
    return init(run);
}

static long so_init_record(dbCommon *rec)
{
    return 2; /* don't convert, we have no value, yet */
}

/* ---------------------------------- */

static long ai_read(aiRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    long status = 0;
    eip_bool ok;
    CN_DINT rval;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if ((ok = lock_data((dbCommon *)rec)))
    {
        /* Most common case: ai reads a tag from PLC */
        if (pvt->special < SPCO_PLC_ERRORS)
        {
            if (pvt->tag->valid_data_size>0 && pvt->tag->elements>pvt->element)
            {
                if (get_CIP_typecode(pvt->tag->data) == T_CIP_REAL  ||
                    get_CIP_typecode(pvt->tag->data) == T_CIP_LREAL)
                {
                    ok = get_CIP_double(pvt->tag->data,
                                        pvt->element, &rec->val);
                    status = 2; /* don't convert */
                }
                else
                {
                    ok = get_CIP_DINT(pvt->tag->data, pvt->element, &rval);
                    rec->rval = rval;
                }
            }
            else
                ok = false;
        }
        else
        {
            /* special cases: ai reads driver stats */
            status = 2;
            if (pvt->special & SPCO_PLC_ERRORS)
                rec->val = (double) pvt->plc->plc_errors;
            else if (pvt->special & SPCO_PLC_TASK_SLOW)
                rec->val = (double) pvt->plc->slow_scans;
            else if (pvt->special & SPCO_LIST_ERRORS)
                rec->val = (double) pvt->tag->scanlist->list_errors;
            else if ((pvt->special & SPCO_LIST_TICKS) ||
                     (pvt->special & SPCO_LIST_TIME))
                rec->val = (double) pvt->tag->scanlist->scan_time.secPastEpoch;
            else if (pvt->special & SPCO_LIST_SCAN_TIME)
                rec->val = pvt->tag->scanlist->last_scan_time;
            else if (pvt->special & SPCO_LIST_MIN_SCAN_TIME)
                rec->val = pvt->tag->scanlist->min_scan_time;
            else if (pvt->special & SPCO_LIST_MAX_SCAN_TIME)
                rec->val = pvt->tag->scanlist->max_scan_time;
            else if (pvt->special & SPCO_TAG_TRANSFER_TIME)
                rec->val = pvt->tag->transfer_time;
            else
                ok = false;
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec,READ_ALARM,INVALID_ALARM);
    return status;
}

#ifdef SUPPORT_LINT
static long int64in_read(int64inRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    long status = 0;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if ((ok = lock_data((dbCommon *)rec)))
    {
        if (pvt->tag->valid_data_size>0 && pvt->tag->elements>pvt->element)
            ok = get_CIP_LINT(pvt->tag->data, pvt->element, &rec->val);
        else
            ok = false;
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    if (!ok)
        recGblSetSevr(rec,READ_ALARM,INVALID_ALARM);
    return status;
}
#endif


static long bi_read(biRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        ok = get_bits((dbCommon *)rec, 1, (epicsUInt32 *) &rec->rval);
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, READ_ALARM, INVALID_ALARM);
    return 0;
}

#ifdef BUILD_LONG_STRING_SUPPORT
static long lsi_read(lsiRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        ok = get_CIP_STRING(pvt->tag->data, rec->val, rec->sizv);
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
    {
        rec->len = strlen(rec->val) + 1;
        rec->udf = FALSE;
    }
    else
        recGblSetSevr(rec,READ_ALARM,INVALID_ALARM);
    return 0;
}
#endif

static long mbbi_read (mbbiRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate ((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        ok = get_bits((dbCommon *)rec, rec->nobt, (epicsUInt32 *) &rec->rval);
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, READ_ALARM, INVALID_ALARM);
    return 0;
}

static long mbbi_direct_read (mbbiDirectRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        ok = get_bits((dbCommon *)rec, rec->nobt, (epicsUInt32 *) &rec->rval);
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, READ_ALARM, INVALID_ALARM);
    return 0;
}

static long si_read(stringinRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        // Read up to MAX_STRING_SIZE chars, including '\0' terminator.
        // Record might actually allow MAX_STRING_SIZE chars without terminator,
        // but to be on the safe side we always include a terminator,
        // and thus can only fill the string record with MAX_STRING_SIZE-1 bytes.
        ok = get_CIP_STRING(pvt->tag->data, &rec->val[0], MAX_STRING_SIZE);
        // printf("Record %s read '%s' (%d)\n", rec->name, rec->val, strlen(rec->val));
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec,READ_ALARM,INVALID_ALARM);
    return 0;
}

static long wf_read(waveformRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool ok;
    CN_DINT *dint;
    CN_DINT dint_val;
    char    *s;
    double  *dbl;
    char    *c;
    size_t i;

    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if ((ok = lock_data((dbCommon *)rec)))
    {
        if (pvt->tag->valid_data_size > 0 &&  pvt->tag->elements >= rec->nelm)
        {
            if (get_CIP_typecode(pvt->tag->data) == T_CIP_REAL  ||
                get_CIP_typecode(pvt->tag->data) == T_CIP_LREAL)
            {
                if (rec->ftvl == menuFtypeDOUBLE)
                {
                    dbl = (double *)rec->bptr;
                    for (i=0; ok && i<rec->nelm; ++i, ++dbl)
                        ok = get_CIP_double(pvt->tag->data, i, dbl);
                    if (ok)
                        rec->nord = rec->nelm;
                }
                else
                {
                    recGblRecordError(S_db_badField, (void *)rec,
                                      "EtherIP: tag data type REAL or LREAL requires "
                                      "waveform FTVL==DOUBLE");
                    ok = false;
                }
            }
            else if (get_CIP_typecode(pvt->tag->data) == T_CIP_SINT)
            {
                if (rec->ftvl == menuFtypeCHAR || rec->ftvl == menuFtypeUCHAR)
                {
                    c = (char *)rec->bptr;
                    for (i=0; ok && i<rec->nelm; ++i, ++c)
                        ok = get_CIP_USINT(pvt->tag->data, i, (CN_USINT*)c);
                    if (ok)
                        rec->nord = rec->nelm;
                }
                else
                {
                    recGblRecordError(S_db_badField, (void *)rec,
                                      "EtherIP: tag data type requires "
                                      "waveform FTVL==CHAR/UCHAR");
                    ok = false;
                }
            }
            else
            {   /* CIP data is something other than REAL and SINT */
                if (rec->ftvl == menuFtypeLONG)
                {
                    dint = (CN_DINT *)rec->bptr;
                    for (i=0; ok && i<rec->nelm; ++i, ++dint)
                        ok = get_CIP_DINT(pvt->tag->data, i, dint);
                    if (ok)
                        rec->nord = rec->nelm;
                }
                else if (rec->ftvl == menuFtypeCHAR)
                {
                    s = (char *)rec->bptr;
                    for (i=0; ok && i<rec->nelm; ++i)
                    {
                        ok = get_CIP_DINT(pvt->tag->data, i, &dint_val);
                        *(s++) = dint_val;
                    }
                    if (ok)
                        rec->nord = rec->nelm;
                }
                else
                {
                    recGblRecordError(S_db_badField, (void *)rec,
                                      "EtherIP: tag type requires "
                                      "waveform FTVL==LONG or CHAR");
                    ok = false;
                }
            }
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    if (!ok)
        recGblSetSevr(rec,READ_ALARM,INVALID_ALARM);
    return 0;
}

static long ao_write(aoRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    long          status;
    double        dbl;
    CN_DINT       dint;
    eip_bool      ok = true;

    if (rec->pact) /* Second pass, called for write completion ? */
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    status = check_link((dbCommon *)rec, check_ao_callback, &rec->out, 1, 0);
    if (status)
    {
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
        return status;
    }
    if (lock_data((dbCommon *)rec))
    {   /* Check if record's (R)VAL is current */
        if (get_CIP_typecode(pvt->tag->data) == T_CIP_REAL  ||
            get_CIP_typecode(pvt->tag->data) == T_CIP_LREAL)
        {
            if (get_CIP_double(pvt->tag->data, pvt->element, &dbl) &&
                rec->val != dbl)
            {
                if (rec->tpro)
                    printf("'%s': write %g!\n", rec->name, rec->val);
                ok = put_CIP_double(pvt->tag->data, pvt->element, rec->val);
                if (pvt->tag->do_write)
                    EIP_printf(6,"'%s': already writing\n", rec->name);
                else
                    pvt->tag->do_write = true;
                rec->pact=TRUE;
            }
        }
        else
        {
            if (get_CIP_DINT(pvt->tag->data, pvt->element, &dint) &&
                rec->rval != dint)
            {
                if (rec->tpro)
                    printf("'%s': write %ld (0x%lX)!\n",
                           rec->name, (long)rec->rval, (long)rec->rval);
                ok = put_CIP_DINT(pvt->tag->data, pvt->element, rec->rval);
                if (pvt->tag->do_write)
                    EIP_printf(6,"'%s': already writing\n", rec->name);
                else
                    pvt->tag->do_write = true;
                rec->pact=TRUE;
            }
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}

#ifdef SUPPORT_LINT
static long int64out_write(int64outRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool      ok = true;

    if (rec->pact) /* Second pass, called for write completion ? */
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {   /* Check if record's VAL is current */
        CN_LINT val;
        ok = get_CIP_LINT(pvt->tag->data, pvt->element, &val);
        if (ok && rec->val != val)
        {
            if (rec->tpro)
                printf("'%s': write %lld!\n", rec->name, rec->val);
            ok = put_CIP_LINT(pvt->tag->data, pvt->element, rec->val);
            if (pvt->tag->do_write)
                EIP_printf(6,"'%s': already writing\n", rec->name);
            else
                pvt->tag->do_write = true;
            rec->pact=TRUE;
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}
#endif

static long bo_write(boRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32   rval;
    eip_bool      ok = true;

    if (rec->pact)
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        if (get_bits((dbCommon *)rec, 1, &rval))
        {
            if (rec->rval != rval)
            {
                if (rec->tpro)
                    printf("'%s': write %u\n", rec->name, (unsigned int) rec->rval);
                ok = put_bits((dbCommon *)rec, 1, rec->rval);
                if (pvt->tag->do_write)
                    EIP_printf(6,"'%s': already writing\n", rec->name);
                else
                    pvt->tag->do_write = true;
                rec->pact=TRUE;
            }
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}

static long bo_reset_stats(boRecord *rec)
{
    // Reset when non-zero value is written
    if (rec->rval)
    {
        printf("'%s': resetting PLC statistics\n", rec->name);
        drvEtherIP_reset_statistics();
    }
    return 0;
}

#ifdef BUILD_LONG_STRING_SUPPORT
static long lso_write(lsoRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool      ok = true;

    if (rec->pact) /* Second pass, called for write completion ? */
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {   /* Check if record's (R)VAL is current */
        char *data = dbmfMalloc(rec->sizv);

        ok = get_CIP_STRING(pvt->tag->data, data, rec->sizv);
        if (ok && strcmp(rec->val, data))
        {
            if (rec->tpro)
                printf("'%s': write %s!\n", rec->name, rec->val);
            ok = put_CIP_STRING(pvt->tag->data, rec->val, pvt->tag->data_size);
            if (pvt->tag->do_write)
                EIP_printf(6,"'%s': already writing\n", rec->name);
            else
                pvt->tag->do_write = true;
            rec->pact=TRUE;
        }
        dbmfFree(data);
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}
#endif

static long mbbo_write (mbboRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32   rval;
    eip_bool      ok = true;

    if (rec->pact)
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate ((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        if (get_bits((dbCommon *)rec, rec->nobt, &rval) && rec->rval != rval)
        {
            if (rec->tpro)
                printf("'%s': write %u\n", rec->name, (unsigned int) rec->rval);
            ok = put_bits((dbCommon *)rec, rec->nobt, rec->rval);
            if (pvt->tag->do_write)
                EIP_printf(6,"'%s': already writing\n", rec->name);
            else
                pvt->tag->do_write = true;
            rec->pact=TRUE;
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}

static long mbbo_direct_write (mbboDirectRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    epicsUInt32   rval;
    eip_bool      ok = true;

    if (rec->pact)
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {
        if (get_bits((dbCommon *)rec, rec->nobt, &rval)  &&  rec->rval != rval)
        {
            if (rec->tpro)
                printf("'%s': write %u\n", rec->name, (unsigned int) rec->rval);
            ok = put_bits((dbCommon *)rec, rec->nobt, rec->rval);
            if (pvt->tag->do_write)
                EIP_printf(6,"'%s': already writing\n", rec->name);
            else
                pvt->tag->do_write = true;
            rec->pact=TRUE;
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}

static long so_write(stringoutRecord *rec)
{
    DevicePrivate *pvt = (DevicePrivate *)rec->dpvt;
    eip_bool      ok = true;
    char          data[MAX_STRING_SIZE];

    if (rec->pact) /* Second pass, called for write completion ? */
    {
        if (rec->tpro)
            printf("'%s': written\n", rec->name);
        rec->pact = FALSE;
        return 0;
    }
    if (rec->tpro)
        dump_DevicePrivate((dbCommon *)rec);
    if (lock_data((dbCommon *)rec))
    {   /* Check if record's (R)VAL is current.
         * stringout record might allow MAX_STRING_SIZE chars
         * without terminator, but we only handle terminated strings
         * in the stringin support and in put_CIP_STRING,
         * so enforce terminator in case text used all MAX_STRING_SIZE chars
         */
        rec->val[MAX_STRING_SIZE-1] = '\0';
        /* Get a total of MAX_STRING_SIZE incl. terminator for comparison */
        ok = get_CIP_STRING(pvt->tag->data, data, MAX_STRING_SIZE);
        if (ok && strcmp(rec->val, data))
        {
            if (rec->tpro)
                printf("'%s': write %s!\n", rec->name, rec->val);
            ok = put_CIP_STRING(pvt->tag->data, rec->val, pvt->tag->data_size);
            if (pvt->tag->do_write)
                EIP_printf(6,"'%s': already writing\n", rec->name);
            else
                pvt->tag->do_write = true;
            rec->pact=TRUE;
        }
        epicsMutexUnlock(pvt->tag->data_lock);
    }
    else
        ok = false;
    if (ok)
        rec->udf = FALSE;
    else
        recGblSetSevr(rec, WRITE_ALARM, INVALID_ALARM);
    return 0;
}

/* Create the device support entry tables */
struct {
    dset common;
    long (*read)(aiRecord *rec);
    long (*special_linconv)(aiRecord *rec);
} devAiEtherIP = {
    {
        6,
        NULL,
        ai_init,
        ai_init_record,
        get_ioint_info
    },
    ai_read,
    NULL
};

#ifdef SUPPORT_LINT
struct {
    dset common;
    long (*read)(int64inRecord *rec);
} devInt64inEtherIP = {
    {
        5,
        NULL,
        int64in_init,
        int64in_init_record,
        get_ioint_info
    },
    int64in_read
};
struct {
    dset common;
    long (*write)(int64outRecord *rec);
} devInt64outEtherIP = {
    {
        5,
        NULL,
        int64out_init,
        int64out_init_record,
        NULL
    },
    int64out_write
};
#endif

struct {
    dset common;
    long (*read)(biRecord *rec);
} devBiEtherIP = {
    {
        5,
        NULL,
        bi_init,
        bi_init_record,
        get_ioint_info
    },
    bi_read
};

#ifdef BUILD_LONG_STRING_SUPPORT
struct {
    dset common;
    long (*read)(lsiRecord *rec);
} devLsiEtherIP = {
    {
        5,
        NULL,
        lsi_init,
        lsi_init_record,
        get_ioint_info
    },
    lsi_read
};
#endif

struct {
    dset common;
    long (*read)(mbbiRecord *rec);
} devMbbiEtherIP = {
    {
        5,
        NULL,
        mbbi_init,
        mbbi_init_record,
        get_ioint_info
    },
    mbbi_read
};

struct {
    dset common;
    long (*read)(mbbiDirectRecord *rec);
} devMbbiDirectEtherIP = {
    {
        5,
        NULL,
        mbbi_direct_init,
        mbbi_direct_init_record,
        get_ioint_info
    },
    mbbi_direct_read
};

struct {
    dset common;
    long (*read)(stringinRecord *rec);
} devSiEtherIP = {
    {
        5,
        NULL,
        si_init,
        si_init_record,
        get_ioint_info
    },
    si_read
};

struct {
    dset common;
    long (*read)(waveformRecord *rec);
} devWfEtherIP = {
    {
        5,
        NULL,
        wf_init,
        wf_init_record,
        get_ioint_info
    },
    wf_read
};

struct {
    dset common;
    long (*write)(aoRecord *rec);
    long (*special_linconv)(aoRecord *rec);
} devAoEtherIP = {
    {
        6,
        NULL,
        ao_init,
        ao_init_record,
        NULL
    },
    ao_write,
    NULL
};

struct {
    dset common;
    long (*write)(boRecord *rec);
} devBoEtherIP = {
    {
        6,
        NULL,
        bo_init,
        bo_init_record,
        NULL
    },
    bo_write
};

struct {
    dset common;
    long (*write)(boRecord *rec);
} devBoEtherIPReset = {
    {
        6,
        NULL,
        NULL,
        NULL,
        NULL
    },
    bo_reset_stats
};

#ifdef BUILD_LONG_STRING_SUPPORT
struct {
    dset common;
    long (*write)(lsoRecord *rec);
} devLsoEtherIP = {
    {
        5,
        NULL,
        lso_init,
        lso_init_record,
        NULL
    },
    lso_write
};
#endif

struct {
    dset common;
    long (*write)(mbboRecord *rec);
} devMbboEtherIP = {
    {
        5,
        NULL,
        mbbo_init,
        mbbo_init_record,
        NULL
    },
    mbbo_write
};

struct {
    dset common;
    long (*write)(mbboDirectRecord *rec);
} devMbboDirectEtherIP = {
    {
        5,
        NULL,
        mbbo_direct_init,
        mbbo_direct_init_record,
        NULL
    },
    mbbo_direct_write
};

struct {
    dset common;
    long (*write)(stringoutRecord *rec);
} devSoEtherIP = {
    {
        5,
        NULL,
        so_init,
        so_init_record,
        NULL
    },
    so_write
};

epicsExportAddress(dset,devAiEtherIP);
#ifdef SUPPORT_LINT
epicsExportAddress(dset,devInt64inEtherIP);
epicsExportAddress(dset,devInt64outEtherIP);
#endif
epicsExportAddress(dset,devBiEtherIP);
#ifdef BUILD_LONG_STRING_SUPPORT
epicsExportAddress(dset,devLsiEtherIP);
#endif
epicsExportAddress(dset,devMbbiEtherIP);
epicsExportAddress(dset,devMbbiDirectEtherIP);
epicsExportAddress(dset,devSiEtherIP);
epicsExportAddress(dset,devWfEtherIP);
epicsExportAddress(dset,devAoEtherIP);
epicsExportAddress(dset,devBoEtherIP);
epicsExportAddress(dset,devBoEtherIPReset);
#ifdef BUILD_LONG_STRING_SUPPORT
epicsExportAddress(dset,devLsoEtherIP);
#endif
epicsExportAddress(dset,devMbboEtherIP);
epicsExportAddress(dset,devMbboDirectEtherIP);
epicsExportAddress(dset,devSoEtherIP);

/* EOF devEtherIP.c */

