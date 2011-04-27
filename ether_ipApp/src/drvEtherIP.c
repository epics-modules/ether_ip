/* $Id: drvEtherIP.c,v 1.14 2011/04/12 18:08:48 saa Exp $
 *
 * drvEtherIP
 *
 * IOC driver that uses ether_ip routines,
 * keeping lists of PLCs and tags and scanlists etc.
 *
 * kasemirk@ornl.gov
 */

/* System */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
/* Base */
#include <drvSup.h>
#include <errlog.h>
/* Local */
#include "drvEtherIP.h"
#ifdef HAVE_314_API
/* Base */
#include "epicsExport.h"
#include "initHooks.h"

/* See drvEtherIP_initHook() */
static int databaseIsReady = false;
#endif

double drvEtherIP_default_rate = 0.0;

DrvEtherIP_Private drvEtherIP_private = { {NULL, NULL}, 0 };

/* Locking:
 *
 * Issues:
 * a) Structures: adding PLCs, scanlists, tags
 *                moving tags between scanlists,
 *                modifying callbacks for tags
 * b) Data:       driver and device read/write tag's data
 *                and change the update flag
 *
 * The following 3 locks are used between threads.
 * To avoid deadlocks, they have to be taken in the listed order
 * by any code that uses more than one of the locks.
 *
 * 1) drvEtherIP_private.lock is for PLCs
 *    Everything that accesses >1 PLC takes this lock.
 *
 * 2) PLC.lock is per-PLC
 *    All structural changes to a PLC take this lock
 *    Currently PLCs are added, never removed,
 *    so the global lock is not affected by this.
 *    PLC.lock is for all data structures for this PLC:
 *    scanlists, tags, callbacks.
 *
 *    PLC_scan_task needs access to connection & scanlists,
 *    so it takes lock for each run down the scanlist.
 *
 * 3) TagInfo.data_lock is the Data lock.
 *    The scan task runs over the Tags in a scanlist three times:
 *    a) see how much can be handled in one network transfer,
 *       determine size of request/response
 *    b) setup the requests
 *    c) handle the response
 *
 *    The list of tags cannot change because of the PLC.lock.
 *    But the device might want to switch from read to write.
 *    In the protocol, the "CIP Read Data" and "CIP Write Data"
 *    request/response are different in length.
 *    So the do_write flag is checked in a), the driver has to
 *    know in b and c) if this is a write access.
 *    -> write behavior must not change across a->c.
 *    The network transfer between b) and c) takes time,
 *    so we avoid locking the data and do_write flag all that time
 *    from a) to c). Instead, the lock is released after b) and
 *    taken again in c).
 *    Data is locked in c) to keep the device from looking at
 *    immature data. The driver keeps the state of do_write from a)
 *    in is_writing. If the device sets do_write after a), it's
 *    ignored until the next scan. (Otherwise the device would
 *    be locked until the next scan!)
 *
 * do_write   is_writing
 *    1           0       -> Device support requested write
 *    0           1       -> Driver noticed the write request,
 *    0           1       -> sends it
 *    0           0       -> Driver received write result from PLC
 */

/* ------------------------------------------------------------
 * TagInfo
 * ------------------------------------------------------------ */
static void dump_TagInfo(const TagInfo *info, int level)
{
    char buffer[EIP_MAX_TAG_LENGTH];
    printf("*** Tag '%s' @ 0x%lX:\n", info->string_tag, (unsigned long)info);
    if (level > 3)
    {
        printf("  scanlist            : 0x%lX\n",
               (unsigned long)info->scanlist);
        EIP_copy_ParsedTag(buffer, info->tag);
        printf("  compiled tag        : '%s', %d elements\n",
        	   buffer, (unsigned)info->elements);
        printf("  cip read requ./resp.: %u / %u\n",
        	   (unsigned)info->cip_r_request_size, (unsigned)info->cip_r_response_size);
        printf("  cip write req./resp.: %u / %u\n",
        	   (unsigned)info->cip_w_request_size, (unsigned)info->cip_w_response_size);
        printf("  data_lock ID        : 0x%lX\n",
               (unsigned long) info->data_lock);
    }
    if (epicsMutexLock(info->data_lock) == epicsMutexLockOK)
    {
        if (level > 3)
        {
            printf("  data_size / valid   : %u / %u\n",
            	   (unsigned)info->data_size,  (unsigned)info->valid_data_size);
            printf("  do_write/is_writing : %s / %s\n",
                   (info->do_write ? "yes" : "no"),
                   (info->is_writing ? "yes" : "no"));
            EIP_printf(0, "  data                : ");
        }
        if (info->valid_data_size > 0)
            dump_raw_CIP_data(info->data, info->elements);
        else
            printf("-no data-\n");
        epicsMutexUnlock(info->data_lock);
    }
    else
        printf("  (CANNOT GET DATA LOCK!)\n");
    if (level > 3)
        printf("  transfer time       : %g secs\n", info->transfer_time);
}

static TagInfo *new_TagInfo(const char *string_tag, size_t elements)
{
    TagInfo *info = (TagInfo *) calloc(sizeof(TagInfo), 1);
    if (!info)
    	return 0;
    info->string_tag = EIP_strdup(string_tag);
    if (! info->string_tag)
        return 0;
    info->tag = EIP_parse_tag(string_tag);
    if (! info->tag)
    {
        EIP_printf(2, "new_TagInfo: failed to parse tag '%s'\n",
                   string_tag);
        return 0;
    }
    info->elements = elements;
    info->data_lock = epicsMutexCreate();
    if (! info->data_lock)
    {
        EIP_printf(0, "new_TagInfo (%s): Cannot create data lock\n",
                   string_tag);
        return 0;
    }
    DLL_init(&info->callbacks);
    return info;
}

/** Reserve buffer for TagInfo.data
 *  @return true when OK
 */
eip_bool reserve_tag_data(TagInfo *info, size_t requested_size)
{
	if (info->data_size >= requested_size)
		return true;
	if (requested_size >= EIP_BUFFER_SIZE)
	{
        EIP_printf(2, "EIP reserve_tag_data: rejecting tag '%s' data size of %d bytes\n",
                   info->string_tag, requested_size);
		return false;
	}
	if (info->data_size != 0  &&  info->data != 0)
	{
        EIP_printf(2, "EIP reserve_tag_data: tag '%s' value buffer grows from %d to %d bytes\n",
                   info->string_tag, info->data_size, requested_size);
        free(info->data);
	}
	info->data = (CN_USINT *) calloc(1, requested_size);
	if (! info->data)
	{
        EIP_printf(2, "EIP reserve_tag_data: tag '%s' failed to allocate buffer for %d bytes\n",
                   info->string_tag, requested_size);
		return false;
	}
	info->data_size = requested_size;
	return true;
}

#if 0
/* We never remove a tag */
static void free_TagInfo(TagInfo *info)
{
    EIP_free_ParsedTag(info->tag);
    free(info->string_tag);
    if (info->data_size > 0)
    {
        free(info->data);
        info->data_size = 0;
        info->data = 0;
    }
    epicsMutexDestroy(info->data_lock);
    free (info);
}
#endif

/* ------------------------------------------------------------
 * ScanList
 * ------------------------------------------------------------
 *
 * NOTE: None of these ScanList funcs do any locking,
 * The caller has to do that!
 */

#ifndef HAVE_314_API
void epicsTimeToStrftime(char *txt, size_t len, const char *fmt,
                         const epicsTimeStamp *stamp)
{
    sprintf(txt, "%f secs", ((double)*stamp)/sysClkRateGet());
}
#endif

static void dump_ScanList(const ScanList *list, int level)
{
    const TagInfo *info;
    char      tsString[50];
    printf("Scanlist %g secs @ 0x%lX:\n",
           list->period, (unsigned long)list);
    printf("  Status        : %s\n",
           (list->enabled ? "enabled" : "DISABLED"));
    epicsTimeToStrftime(tsString, sizeof(tsString),
                        "%Y/%m/%d %H:%M:%S.%04f", &list->scan_time);
    printf("  Last scan     : %s\n", tsString);
    if (level > 4)
    {
        printf("  Errors        : %u\n", (unsigned)list->list_errors);
        printf("  Schedule Errs : %u\n", (unsigned)list->sched_errors);
        epicsTimeToStrftime(tsString, sizeof(tsString),
                            "%Y/%m/%d %H:%M:%S.%04f", &list->scheduled_time);
        printf("  Next scan     : %s\n", tsString);
        printf("  Min. scan time: %g secs\n",
               list->min_scan_time);
        printf("  Max. scan time: %g secs\n",
               list->max_scan_time);
        printf("  Last scan time: %g secs\n",
               list->last_scan_time);
    }
    if (level > 5)
    {
        for (info=DLL_first(TagInfo, &list->taginfos); info;
             info=DLL_next(TagInfo, info))
            dump_TagInfo(info, level);
    }
}

/* Set counters etc. to initial values */
static void reset_ScanList(ScanList *scanlist)
{
    scanlist->enabled        = true;
    scanlist->list_errors    = 0;
    scanlist->sched_errors   = 0;
    memset(&scanlist->scan_time,      0, sizeof(epicsTimeStamp));
    memset(&scanlist->scheduled_time, 0, sizeof(epicsTimeStamp));
    scanlist->min_scan_time  = 0.0;
    scanlist->max_scan_time  = 0.0;
    scanlist->last_scan_time = 0.0;
}

static ScanList *new_ScanList(PLC *plc, double period)
{
    ScanList *list = (ScanList *) calloc(sizeof(ScanList), 1);
    if (!list)
        return 0;
    DLL_init(&list->taginfos);
    list->plc = plc;
    list->period = period;
    reset_ScanList (list);
    return list;
}

#if 0
/* We never remove a scan list */
static void free_ScanList(ScanList *scanlist)
{
    TagInfo *info;
    while ((info = DLL_decap(&scanlist->taginfos)) != 0)
        free_TagInfo(info);
    free(scanlist);
}
#endif

/* Find tag by name, returns 0 for "not found" */
static TagInfo *find_ScanList_Tag (const ScanList *scanlist,
                                   const char *string_tag)
{
    TagInfo *info;
    for (info=DLL_first(TagInfo, &scanlist->taginfos); info;
         info=DLL_next(TagInfo, info))
    {
        if (strcmp(info->string_tag, string_tag)==0)
            return info;
    }
    return 0;
}

/* remove/add TagInfo */
static void remove_ScanList_TagInfo(ScanList *scanlist, TagInfo *info)
{
    info->scanlist = 0;
    DLL_unlink(&scanlist->taginfos, info);
}

static void add_ScanList_TagInfo(ScanList *scanlist, TagInfo *info)
{
    DLL_append(&scanlist->taginfos, info);
    info->scanlist = scanlist;
}

/* Add new tag to taglist, compile tag
 * returns 0 on error */
static TagInfo *add_ScanList_Tag(ScanList *scanlist,
                                 const char *string_tag,
                                 size_t elements)
{
    TagInfo *info = new_TagInfo(string_tag, elements);
    if (info)
        add_ScanList_TagInfo(scanlist, info);
    return info;
}

/* ------------------------------------------------------------
 * PLC
 * ------------------------------------------------------------ */

static PLC *new_PLC(const char *name)
{
    PLC *plc = (PLC *) calloc(1, sizeof(PLC));
    if (! plc)
    	return 0;
    plc->name = EIP_strdup(name);
    if (! plc->name)
        return 0;
    DLL_init (&plc->scanlists);
    plc->lock = epicsMutexCreate();
    if (! plc->lock)
    {
        EIP_printf (0, "new_PLC (%s): Cannot create mutex\n", name);
        return 0;
    }
    plc->connection = EIP_init();
    if (! plc->connection)
    {
        EIP_printf (0, "new_PLC (%s): EIP_init failed\n", name);
        return 0;
    }
    return plc;
}

#if 0
/* We never really remove a PLC from the list,
 * but this is how it could be done. Maybe. */
static void free_PLC(PLC *plc)
{
    ScanList *list;

    epicsMutexDestroy(plc->lock);
    EIP_dispose(plc->connection);
    free(plc->name);
    free(plc->ip_addr);
    while ((list = DLL_decap(&plc->scanlists)) != 0)
        free_ScanList(list);
    free(plc);
}
#endif

/* After TagInfos are defined (tag & elements are set),
 * fill rest of TagInfo: request/response size.
 *
 * Returns OK if any TagInfo in the scanlists could be filled,
 * so we believe that scanning this PLC makes some sense.
 */
static eip_bool complete_PLC_ScanList_TagInfos(PLC *plc)
{
    ScanList       *list;
    TagInfo        *info;
    const CN_USINT *data;
    size_t         tried = 0, succeeded = 0;
    size_t         type_and_data_len;

    EIP_printf(5, "complete_PLC_ScanList_TagInfos PLC '%s':\n", plc->name);

    for (list=DLL_first(ScanList, &plc->scanlists);  list;
         list=DLL_next(ScanList, list))
    {
        EIP_printf(5, "- Scanlist %.2f\n", list->period);
        for (info=DLL_first(TagInfo, &list->taginfos);  info;
             info=DLL_next(TagInfo, info))
        {
            if (epicsMutexLock(info->data_lock) != epicsMutexLockOK)
            {
                EIP_printf(1, "EIP complete_PLC_ScanList_TagInfos cannot lock %s\n",
                           info->string_tag);
                continue;
            }
            /* Need to get the read sizes */
            ++tried;
            data = EIP_read_tag(plc->connection,
                                info->tag, info->elements,
                                NULL /* data_size */,
                                &info->cip_r_request_size,
                                &info->cip_r_response_size);
            if (data)
            {
                EIP_printf(5, "  tag '%s': req %d, resp %d bytes\n",
                           info->string_tag, info->cip_r_request_size, info->cip_r_response_size);
                ++succeeded;
                /* Estimate write sizes from the request/response for read
                 * because we don't want to issue a 'write' just for the
                 * heck of it.
                 * Nevertheless, the write sizes calculated in here
                 * should be exact since we can determine the write
                 * request package from the read request
                 * (CIP service code, tag name, elements)
                 * plus the raw data size.
                 */
                if (info->cip_r_response_size <= 4)
                {
                    info->cip_w_request_size  = 0;
                    info->cip_w_response_size = 0;
                }
                else
                {
                    type_and_data_len = info->cip_r_response_size - 4;
                    info->cip_w_request_size  = info->cip_r_request_size
                        + type_and_data_len;
                    info->cip_w_response_size = 4;
                }
            }
            else
            {
                EIP_printf(3, "tag '%s': Cannot read!\n", info->string_tag);
                info->cip_r_request_size  = 0;
                info->cip_r_response_size = 0;
                info->cip_w_request_size  = 0;
                info->cip_w_response_size = 0;
            }
            epicsMutexUnlock(info->data_lock);
        }
    }
    EIP_printf(5, "complete_PLC_ScanList_TagInfos PLC '%s': tried %lu tags, got %lu tags\n",
               plc->name, (unsigned long)tried, (unsigned long)succeeded);
    /* OK if we got at least one answer,
     * or we never really tried to get any tag */
    return (succeeded > 0) || (tried == 0);
}

static void invalidate_PLC_tags(PLC *plc)
{
    ScanList    *list;
    TagInfo     *info;
    TagCallback *cb;

    for (list=DLL_first(ScanList, &plc->scanlists);  list;
         list=DLL_next(ScanList, list))
    {
        for (info = DLL_first(TagInfo, &list->taginfos);  info;
             info = DLL_next(TagInfo, info))
        {
            if (epicsMutexLock(info->data_lock) == epicsMutexLockOK)
            {
            	/** Reset all write flags: After an error, we skip all
            	 *  writes to prevent writing garbage after a reconnect
            	 */
            	info->is_writing = false;
                info->valid_data_size = 0;
                epicsMutexUnlock(info->data_lock);
                /* Call all registered callbacks for this tag
                 * so that records can show INVALID */
                for (cb = DLL_first(TagCallback, &info->callbacks);  cb;
                     cb=DLL_next(TagCallback, cb))
                    (*cb->callback) (cb->arg);
            }
            else
            {
            	EIP_printf(1, "EIP invalidate_PLC_tags cannot lock %s",
            			   info->string_tag);
            }
        }
    }
}

static void disconnect_PLC(PLC *plc)
{
    if (plc->connection->sock)
    {
        EIP_printf_time(4, "EIP disconnecting %s\n", plc->name);
        EIP_shutdown(plc->connection);
        invalidate_PLC_tags(plc);
    }
}

/* Test if we are connected, if not try to connect to PLC */
static eip_bool assert_PLC_connect(PLC *plc)
{
    if (plc->connection->sock)
        return true;
    EIP_printf_time(4, "EIP connecting %s\n", plc->name);
    if (! EIP_startup(plc->connection, plc->ip_addr,
                      ETHERIP_PORT, plc->slot, ETHERIP_TIMEOUT))
    {
        errlogPrintf("EIP connection failed for %s:%d\n",
                      plc->ip_addr, ETHERIP_PORT);
        return false;
    }
    if (! complete_PLC_ScanList_TagInfos(plc))
    {
        errlogPrintf("EIP error during scan list completion for %s:%d\n",
                      plc->ip_addr, ETHERIP_PORT);
        disconnect_PLC(plc);
        return false;
    }
    return true;
}

/* Given a transfer buffer limit,
 * see how many requests/responses can be handled in one transfer,
 * starting with the current TagInfo and using the following ones.
 *
 * Returns count,
 * fills sizes for total requests/responses as well as
 * size of MultiRequest/Response.
 *
 * Called by scan task, PLC is locked.
 */
static size_t determine_MultiRequest_count(size_t limit,
                                           TagInfo *info,
                                           size_t *requests_size,
                                           size_t *responses_size,
                                           size_t *multi_request_size,
                                           size_t *multi_response_size)
{
    size_t try_req, try_resp, count;

    /* Sum sizes for requests and responses,
     * determine total for MultiRequest/Response,
     * stop if too big.
     * Skip entries with empty cip_*_request_size!
     */
    count = *requests_size = *responses_size = 0;
    EIP_printf(8, "EIP determine_MultiRequest_count, limit %lu\n",
               (unsigned long) limit);
    for (/**/; info; info = DLL_next(TagInfo, info))
    {
        if (info->cip_r_request_size <= 0  ||  info->cip_w_request_size <= 0)
            continue;
        if (epicsMutexLock(info->data_lock) != epicsMutexLockOK)
        {
            EIP_printf(1, "EIP determine_MultiRequest_count cannot lock %s\n",
                       info->string_tag);
            return 0;
        }
        /* Did device suppport request a 'write' cycle?
         * Or are we in one that's not completed?
         */
        info->is_writing = info->do_write | info->is_writing;
        if (info->is_writing)
        {   /* Yes, clear the flag, compute size of write command/reply */
            info->do_write = false;
            try_req  = *requests_size  + info->cip_w_request_size;
            try_resp = *responses_size + info->cip_w_response_size;
            EIP_printf(5, " tag %lu '%s' (write): %lu (0x%X), %lu (0x%X)\n",
                       (unsigned long)count, info->string_tag,
                       (unsigned long)info->cip_w_request_size,
                       (unsigned long)info->cip_w_request_size,
                       (unsigned long)info->cip_w_response_size,
                       (unsigned long)info->cip_w_response_size);
        }
        else
        {   /* Read cycle. Device support may set 'do_write' between now
             * and when we actually read, but we go by 'is_writing      */
            try_req  = *requests_size  + info->cip_r_request_size;
            try_resp = *responses_size + info->cip_r_response_size;
            EIP_printf(8, " tag %lu '%s' (read): %lu (0x%X), %lu (0x%X)\n",
                       (unsigned long)count, info->string_tag,
                       (unsigned long)info->cip_r_request_size,
                       (unsigned long)info->cip_r_request_size,
                       (unsigned long)info->cip_r_response_size,
                       (unsigned long)info->cip_r_response_size);
        }
        epicsMutexUnlock(info->data_lock);
        *multi_request_size  = CIP_MultiRequest_size (count+1, try_req);
        *multi_response_size = CIP_MultiResponse_size(count+1, try_resp);
        if (*multi_request_size  > limit ||
            *multi_response_size > limit)
        {
            *multi_request_size =CIP_MultiRequest_size (count,*requests_size);
            *multi_response_size=CIP_MultiResponse_size(count,*responses_size);
            EIP_printf(8, " Skipping tag '%s', reached buffer limit at req/resp: %lu, %lu\n",
            		   info->string_tag,
                       (unsigned long)*multi_request_size,
                       (unsigned long)*multi_response_size);
        	/* more won't fit */
            if (count <= 0)
            {   /* The one and only tag didn't fit?! */
                EIP_printf(2, "Tag '%s' can never be read because it alone exceeds buffer limit of %lu bytes,\n",
                           info->string_tag, (unsigned long) limit);
                EIP_printf(3, " Request   size: %10lu bytes\n", (unsigned long)try_req);
                EIP_printf(3, " Response  size: %10lu bytes\n", (unsigned long)try_resp);
            }
            return count;
        }
        ++count; /* ok, include another request */
        *requests_size  = try_req;
        *responses_size = try_resp;
    }
    EIP_printf(8, " End of list, total req/resp: %lu, %lu\n",
               (unsigned long)*multi_request_size,
               (unsigned long)*multi_response_size);
    return count;
}

/* Read all tags in Scanlist,
 * using MultiRequests for as many as possible.
 * Called by scan task, PLC is locked.
 *
 * Returns OK when the transactions worked out,
 * even if the read requests for the tags
 * returned no data.
 */
static eip_bool process_ScanList(EIPConnection *c, ScanList *scanlist)
{
    TagInfo             *info, *info_position;
    size_t              count, requests_size, responses_size;
    size_t              multi_request_size = 0, multi_response_size = 0;
    size_t              send_size, i, elements;
    CN_USINT            *send_request, *multi_request, *request;
    const CN_USINT      *response, *single_response, *data;
    EncapsulationRRData rr_data;
    size_t              single_response_size, data_size;
    epicsTimeStamp      start_time, end_time;
    double              transfer_time;
    TagCallback         *cb;
    eip_bool            ok;

    EIP_printf_time(10, "EIP process_ScanList %g s\n", scanlist->period);
    info = DLL_first(TagInfo, &scanlist->taginfos);
    while (info)
    {   /* keep position, we'll loop several times:
         * 0) in determine_MultiRequest_count
         * 1) to send out the requests
         * 2) to handle the responses
         */
        info_position = info;
        count = determine_MultiRequest_count(
            c->transfer_buffer_limit,
            info, &requests_size, &responses_size,
            &multi_request_size, &multi_response_size);
        EIP_printf(10, "EIP process_ScanList %lu items\n",
                   (unsigned long)count);
        if (count == 0) /* Empty, or nothing fits in one request. */
            return true;
        /* send <count> requests as one transfer */
        send_size = CM_Unconnected_Send_size(multi_request_size);
        EIP_printf(10, " ------------------- New Request ------------\n");
        if (!(send_request = EIP_make_SendRRData(c, send_size)))
            return false;
        multi_request = make_CM_Unconnected_Send(send_request,
                                                 multi_request_size, c->slot);
        if (!(multi_request && prepare_CIP_MultiRequest(multi_request, count)))
            return false;
        /* Add read/write requests to the multi requests */
        for (i=0;  i<count;  info=DLL_next(TagInfo, info))
        {
            if (info->cip_r_request_size <= 0  ||  info->cip_w_request_size <= 0)
                continue;
            EIP_printf(10, "Request #%d (%s):\n", i, info->string_tag);
            if (info->is_writing)
            {
                request = CIP_MultiRequest_item(multi_request,
                                                i, info->cip_w_request_size);
                if (epicsMutexLock(info->data_lock) != epicsMutexLockOK)
                {
                    EIP_printf_time(1, "EIP process_ScanList '%s': "
                               "no data lock (write)\n", info->string_tag);
                    info->is_writing = false;
                    return false;
                }
                ok = request &&
                    make_CIP_WriteData(
                        request, info->tag,
                        (CIP_Type)get_CIP_typecode(info->data),
                        info->elements, info->data + CIP_Typecode_size);
                epicsMutexUnlock(info->data_lock);
            }
            else
            {   /* reading, !is_writing */
                request = CIP_MultiRequest_item(
                    multi_request, i, info->cip_r_request_size);
                ok = request &&
                    make_CIP_ReadData(request, info->tag, info->elements);
            }
            if (!ok)
                return false;
            ++i; /* increment here, not in for() -> skip empty tags */
        } /* for i=0..count */
        epicsTimeGetCurrent(&start_time);
        if (!EIP_send_connection_buffer(c))
        {
            EIP_printf_time(2, "EIP process_ScanList: Error while sending request\n");
            return false;
        }
        /* read & disassemble response */
        if (!EIP_read_connection_buffer(c))
        {
            EIP_printf_time(2, "EIP process_ScanList: No response\n");
            return false;
        }
        epicsTimeGetCurrent(&end_time);
        transfer_time = epicsTimeDiffInSeconds(&end_time, &start_time);
        response = EIP_unpack_RRData(c->buffer, &rr_data);
        if (! check_CIP_MultiRequest_Response(response, rr_data.data_length))
        {
            EIP_printf_time(2, "EIP process_ScanList: Error in response\n");
            for (info=info_position,i=0; i<count; info=DLL_next(TagInfo, info))
            {
                if (info->cip_r_request_size <= 0)
                    continue;
                EIP_printf(2, "Tag %i: '%s'\n", i, info->string_tag);
                ++i;
            }
            if (EIP_verbosity >= 2)
                dump_CIP_MultiRequest_Response_Error(response,
                                                     rr_data.data_length);
            return false;
        }
        /* Handle individual read/write responses */
        for (info=info_position, i=0; i<count; info=DLL_next(TagInfo, info))
        {
            if (info->cip_r_request_size <= 0 ||  info->cip_w_request_size <= 0)
                continue;
            info->transfer_time = transfer_time;
            single_response = get_CIP_MultiRequest_Response(
                response, rr_data.data_length, i, &single_response_size);
            if (! single_response)
                return false;
            if (EIP_verbosity >= 10)
            {
                EIP_printf(10, "Response #%d (%s):\n", i, info->string_tag);
                EIP_dump_raw_MR_Response(single_response, 0);
            }
            if (epicsMutexLock(info->data_lock) != epicsMutexLockOK)
            {
                EIP_printf_time(1, "EIP process_ScanList '%s': "
                           "no data lock (receive)\n", info->string_tag);
                return false;
            }
            if (info->is_writing)
            {
                if (!check_CIP_WriteData_Response(single_response,
                                                  single_response_size))
                {
                    EIP_printf_time(0, "EIP: CIPWrite failed for '%s'\n",
                               info->string_tag);
                    info->valid_data_size = 0;
                }
                info->is_writing = false;
            }
            else /* not writing, reading */
            {
                data = check_CIP_ReadData_Response(
                    single_response, single_response_size, &data_size);
                if (info->do_write)
                {   /* Possible: Read request ... network delay ... response
                     * and record requested write during the delay.
                     * Ignore the read, because that would replace the data
                     * that device support wants us to write in the next scan */
                    EIP_printf(8, "EIP '%s': Device support requested write "
                               "in middle of read cycle.\n", info->string_tag);
                }
                else
                {
                    if (data_size > 0  && reserve_tag_data(info, data_size))
                    {
                        memcpy(info->data, data, data_size);
                        info->valid_data_size = data_size;
                        if (EIP_verbosity >= 10)
                        {
                            elements = CIP_Type_size(get_CIP_typecode(data));
                            if (elements > 0)
                            {   /* response = UINT type, raw data */
                                elements = (data_size-2) / elements;
                                EIP_printf(10, "Data (%d elements): ",
                                           elements);
                                dump_raw_CIP_data(data, elements);
                            }
                            else
                            {
                                EIP_printf(10, "Data: ");
                                EIP_hexdump(0, data, data_size);
                            }
                        }
                    }
                    else
                        info->valid_data_size = 0;
                }
            }
            epicsMutexUnlock(info->data_lock);
            /* Call all registered callbacks for this tag
             * so that records can show new value */
            for (cb = DLL_first(TagCallback, &info->callbacks);
                 cb; cb=DLL_next(TagCallback, cb))
                (*cb->callback) (cb->arg);
            ++i;
        }
        /* "info" now on next unread TagInfo or 0 */
    } /* while "info" ... */
    return true;
}

/* Scan task, one per PLC */
static void PLC_scan_task(PLC *plc)
{
    ScanList *list;
    epicsTimeStamp    next_schedule, start_time, end_time;
    double            timeout, delay, quantum;
    eip_bool          transfer_ok, reset_next_schedule;

    quantum = epicsThreadSleepQuantum();
    timeout = (double)ETHERIP_TIMEOUT/1000.0;
scan_loop: /* --------- The Scan Loop for one PLC -------- */
    if (epicsMutexLock(plc->lock) != epicsMutexLockOK)
    {
        EIP_printf_time(1, "drvEtherIP scan task for PLC '%s'"
                   " cannot take plc->lock\n", plc->name);
        return;
    }
    if (!assert_PLC_connect(plc))
    {   /* don't rush since connection takes network bandwidth */
        epicsMutexUnlock(plc->lock);
        EIP_printf_time(2, "drvEtherIP: PLC '%s' is disconnected\n", plc->name);
        epicsThreadSleep(timeout);
        goto scan_loop;
    }
    EIP_printf_time(10, "drvEtherIP scan PLC '%s'\n", plc->name);
    reset_next_schedule = true;
    epicsTimeGetCurrent(&start_time);
    for (list = DLL_first(ScanList,&plc->scanlists);
         list;  list = DLL_next(ScanList,list))
    {
        if (! list->enabled)
            continue;
        if (epicsTimeLessThanEqual(&list->scheduled_time, &start_time))
        {
            epicsTimeGetCurrent(&list->scan_time);
            transfer_ok = process_ScanList(plc->connection, list);
            epicsTimeGetCurrent(&end_time);
            list->last_scan_time =
                epicsTimeDiffInSeconds(&end_time, &list->scan_time);
            /* update statistics */
            if (list->last_scan_time > list->max_scan_time)
                list->max_scan_time = list->last_scan_time;
            if (list->last_scan_time < list->min_scan_time  ||
                list->min_scan_time == 0.0)
                list->min_scan_time = list->last_scan_time;
            if (transfer_ok) /* re-schedule exactly */
            {
                list->scheduled_time = list->scan_time;
                epicsTimeAddSeconds(&list->scheduled_time, list->period);
            }
            else
            {  	/* end_time+fixed delay, ignore extra due to error */
                list->scheduled_time = end_time;
                epicsTimeAddSeconds(&list->scheduled_time, timeout);
                ++list->list_errors;
                ++plc->plc_errors;
                disconnect_PLC(plc);
                epicsMutexUnlock(plc->lock);
                goto scan_loop;
            }
        }
        /* Update time for list that's due next */
        if (reset_next_schedule ||
            epicsTimeLessThan(&list->scheduled_time, &next_schedule))
        {
            reset_next_schedule = false;
            next_schedule = list->scheduled_time;
        }
    }
    epicsMutexUnlock(plc->lock);
    /* fallback for empty/degenerate scan list */
    if (reset_next_schedule)
        delay = EIP_MIN_TIMEOUT;
    else
    {
        epicsTimeGetCurrent(&start_time);
        delay = epicsTimeDiffInSeconds(&next_schedule, &start_time);
        if (delay > 60.0)
        {
            char      tsString[50];
            printf("Scanlist %g secs has scheduling problem, delay = %g sec\n",
                  list->period, delay);
            epicsTimeToStrftime(tsString, sizeof(tsString),
                                "%Y/%m/%d %H:%M:%S.%04f", &list->scan_time);
            printf("  'Scan time'    : %s\n", tsString);
            epicsTimeToStrftime(tsString, sizeof(tsString),
                                "%Y/%m/%d %H:%M:%S.%04f", &start_time);
            printf("  'Current time' : %s\n", tsString);
            epicsTimeToStrftime(tsString, sizeof(tsString),
                                "%Y/%m/%d %H:%M:%S.%04f", &next_schedule);
            printf("  'Next    time' : %s\n", tsString);
            /* Attempt to hack around this by waiting a minute,
             * hoping that the clock looks better by then.
             * Also resetting the scheduled time to 'now'.
             */
            delay = 60.0;
            list->scheduled_time = start_time;
            ++list->sched_errors;
        }
    }
    /* Sleep until next turn. */
    if (delay > 0.0)
        epicsThreadSleep(delay);
    else if (delay <= -quantum)
    {
        EIP_printf(8, "drvEtherIP scan task slow, %g sec delay\n", delay);
        ++plc->slow_scans; /* hmm, "plc" not locked... */
    }
    goto scan_loop;
}

/* Find PLC entry by name, maybe create a new one if not found */
static PLC *get_PLC(const char *name, eip_bool create)
{
    PLC *plc;
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);  plc;
         plc = DLL_next(PLC,plc))
    {
        if (strcmp(plc->name, name) == 0)
            return plc;
    }
    if (! create)
        return 0;
    plc = new_PLC(name);
    if (plc)
        DLL_append(&drvEtherIP_private.PLCs, plc);
    return plc;
}

/* get (or create) ScanList for given rate */
static ScanList *get_PLC_ScanList(PLC *plc, double period, eip_bool create)
{
    ScanList *list;
    for (list = DLL_first(ScanList, &plc->scanlists); list;
         list = DLL_next(ScanList, list))
    {
        if (list->period == period)
            return list;
    }
    if (! create)
        return 0;
    list = new_ScanList(plc, period);
    if (list)
        DLL_append(&plc->scanlists, list);
    return list;
}

/* Find ScanList and TagInfo for given tag.
 * On success, pointer to ScanList and TagInfo are filled.
 */
static eip_bool find_PLC_tag(PLC *plc,
                             const char *string_tag,
                             ScanList **list,
                             TagInfo **info)
{
    for (*list = DLL_first(ScanList,&plc->scanlists); *list;
         *list = DLL_next(ScanList,*list))
    {
        *info = find_ScanList_Tag(*list, string_tag);
        if (*info)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------
 * public interface
 * ------------------------------------------------------------ */

void drvEtherIP_init ()
{
    if (drvEtherIP_private.lock)
    {
        EIP_printf (0, "drvEtherIP_init called more than once!\n");
        return;
    }
    drvEtherIP_private.lock = epicsMutexCreate();
    if (! drvEtherIP_private.lock)
        EIP_printf (0, "drvEtherIP_init cannot create mutex!\n");
    DLL_init (&drvEtherIP_private.PLCs);
#ifdef HAVE_314_API
    drvEtherIP_Register();
#endif
}

void drvEtherIP_help()
{
    printf("drvEtherIP V%d.%d diagnostics routines:\n",
           ETHERIP_MAYOR, ETHERIP_MINOR);
    printf("    int EIP_verbosity (currently %d)\n", EIP_verbosity);
    printf("    -  10: Dump all protocol details\n");
    printf("        9: Hexdump each sent/received buffer\n");
    printf("        6: show driver details\n");
    printf("        5: show write-related operations\n");
    printf("        4: DEFAULT: show basic startup plus error messages\n");
    printf("        2: show more error info\n");
    printf("        1: show severe error messages\n");
	printf("        0: keep quiet\n");
    printf("    double drvEtherIP_default_rate = <seconds>\n");
    printf("    -  define the default scan rate\n");
    printf("       (if neither SCAN nor INP/OUT provide one)\n");
    printf("    int EIP_buffer_limit = <bytes> (currently %d)\n", EIP_buffer_limit);
    printf("    -  Set buffer limit enforced by driver. Default: %d\n", EIP_DEFAULT_BUFFER_LIMIT);
    printf("       The actual PLC limit is unknown, it might depend on the PLC or ENET model.\n");
    printf("       Can only be set before driver starts up.\n");
    printf("    drvEtherIP_define_PLC <name>, <ip_addr>, <slot>\n");
    printf("    -  define a PLC name (used by EPICS records) as IP\n");
    printf("       (DNS name or dot-notation) and slot (0...)\n");
    printf("    drvEtherIP_read_tag <ip>, <slot>, <tag>, <elm.>, <timeout>\n");
    printf("    -  call to test a round-trip single tag read\n");
    printf("       ip: IP address (numbers or name known by IOC\n");
    printf("       slot: Slot of the PLC controller (not ENET). 0, 1, ...\n");
    printf("       timeout: milliseconds\n");
    printf("    drvEtherIP_report <level>\n");
    printf("    -  level = 0..10\n");
    printf("    drvEtherIP_dump\n");
    printf("    -  dump all tags and values; short version of ..._report\n");
    printf("    drvEtherIP_reset_statistics\n");
    printf("    -  reset error counts and min/max scan times\n");
    printf("    drvEtherIP_restart\n");
    printf("    -  in case of communication errors, driver will restart,\n");
    printf("       so calling this one directly shouldn't be necessary\n");
    printf("       but is possible\n");
    printf("\n");
}

/* Public, also driver's report routine */
long drvEtherIP_report(int level)
{
    PLC *plc;
    EIPIdentityInfo *ident;
    ScanList *list;
    epicsTimeStamp now;
    char tsString[50];

    if (level <= 0)
    {
        printf("drvEtherIP V%d.%d - ControlLogix 5000 PLC via EtherNet/IP\n",
               ETHERIP_MAYOR, ETHERIP_MINOR);
        return 0;
    }
    printf("drvEtherIP V%d.%d report, -*- outline -*-\n",
           ETHERIP_MAYOR, ETHERIP_MINOR);
    if (drvEtherIP_private.lock == 0)
    {
        printf(" drvEtherIP lock is 0, did you call drvEtherIP_init?\n");
        return 0;
    }
    if (level > 1)
        printf("  Mutex lock: 0x%lX\n",
               (unsigned long) drvEtherIP_private.lock);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc;  plc = DLL_next(PLC,plc))
    {
        printf ("* PLC '%s', IP '%s'\n", plc->name, plc->ip_addr);
        if (level > 1)
        {
            ident = &plc->connection->info;
            printf("  Interface name        : %s\n", ident->name);
            printf("  Interface vendor      : 0x%X\n", ident->vendor);
            printf("  Interface type        : 0x%X\n", ident->device_type);
            printf("  Interface revision    : 0x%X\n", ident->revision);
            printf("  Interface serial      : 0x%X\n",
                   (unsigned)ident->serial_number);

            printf("  scan thread slow count: %u\n", (unsigned)plc->slow_scans);
            printf("  connection errors     : %u\n", (unsigned)plc->plc_errors);
        }
        if (level > 2)
        {
            printf("  Mutex lock            : 0x%lX\n",
                   (unsigned long)plc->lock);
            printf("  scan task ID          : 0x%lX (%s)\n",
                   (unsigned long) plc->scan_task_id,
                   (plc->scan_task_id==0 ? "-dead-" :
#ifdef HAVE_314_API
                    epicsThreadIsSuspended(plc->scan_task_id)!=0 ? "suspended":
#else
                    taskIdVerify(plc->scan_task_id) != OK   ? "-dead-" :
#endif
                    "running"));
            epicsTimeGetCurrent(&now);
            epicsTimeToStrftime(tsString, sizeof(tsString),
                                "%Y/%m/%d %H:%M:%S.%04f", &now);
            printf("  Now                   : %s\n", tsString);
            if (level > 3)
            {
                printf("** ");
                EIP_dump_connection(plc->connection);
            }
            if (level > 4)
            {
                for (list=DLL_first(ScanList, &plc->scanlists); list;
                     list=DLL_next(ScanList, list))
                {
                    printf("** ");
                    dump_ScanList(list, level);
                }
            }
        }
    }
    printf("\n");
    return 0;
}

void drvEtherIP_dump ()
{
    PLC      *plc;
    ScanList *list;
    TagInfo  *info;

    epicsMutexLock(drvEtherIP_private.lock);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc;  plc=DLL_next(PLC,plc))
    {
        epicsMutexLock(plc->lock);
        printf ("PLC %s\n", plc->name);
        for (list=DLL_first(ScanList, &plc->scanlists); list;
             list=DLL_next(ScanList, list))
        {
            for (info=DLL_first(TagInfo, &list->taginfos); info;
                 info=DLL_next(TagInfo, info))
            {
                EIP_printf(0, "%s ", info->string_tag);
                epicsMutexLock(info->data_lock);
                if (info->valid_data_size > 0)
                    dump_raw_CIP_data(info->data, info->elements);
                else
                    printf(" - no data -\n");
                epicsMutexUnlock(info->data_lock);
            }
        }
        epicsMutexUnlock(plc->lock);
    }
    epicsMutexUnlock(drvEtherIP_private.lock);
    printf("\n");
}

void drvEtherIP_reset_statistics ()
{
    PLC *plc;
    ScanList *list;

    epicsMutexLock(drvEtherIP_private.lock);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc;  plc = DLL_next(PLC,plc))
    {
        epicsMutexLock(plc->lock);
        plc->plc_errors = 0;
        plc->slow_scans = 0;
        for (list=DLL_first(ScanList, &plc->scanlists); list;
             list=DLL_next(ScanList, list))
            reset_ScanList (list);
        epicsMutexUnlock(plc->lock);
    }
    epicsMutexUnlock(drvEtherIP_private.lock);
}

/* Create a PLC entry:
 * name : identifier
 * ip_address: DNS name or dot-notation
 */
eip_bool drvEtherIP_define_PLC(const char *PLC_name,
                               const char *ip_addr, int slot)
{
    PLC *plc;

    epicsMutexLock(drvEtherIP_private.lock);
    plc = get_PLC(PLC_name, true);
    if (plc)
    {
    	if (plc->ip_addr)
    	{
    		EIP_printf(1, "Redefining IP address of PLC %s?\n", PLC_name);
    		free(plc->ip_addr);
    	}
    	plc->ip_addr = EIP_strdup(ip_addr);
        plc->slot = slot;
    }
    epicsMutexUnlock(drvEtherIP_private.lock);
    return plc  &&  plc->ip_addr;
}

/* Returns PLC or 0 if not found */
PLC *drvEtherIP_find_PLC (const char *PLC_name)
{
    PLC *plc;

    epicsMutexLock(drvEtherIP_private.lock);
    plc = get_PLC(PLC_name, /*create*/ false);
    epicsMutexUnlock (drvEtherIP_private.lock);
    return plc;
}

/* After the PLC is defined with drvEtherIP_define_PLC,
 * tags can be added
 */
TagInfo *drvEtherIP_add_tag(PLC *plc, double period,
                            const char *string_tag, size_t elements)
{
    ScanList *list;
    TagInfo  *info;

    epicsMutexLock(plc->lock);
    if (find_PLC_tag(plc, string_tag, &list, &info))
    {   /* check if period is OK */
        if (list->period > period)
        {   /* current scanlist is too slow */
            remove_ScanList_TagInfo(list, info);
            list = get_PLC_ScanList(plc, period, true);
            if (!list)
            {
                epicsMutexUnlock(plc->lock);
                EIP_printf(2, "drvEtherIP: cannot create list at %g secs"
                           "for tag '%s'\n", period, string_tag);
                return 0;
            }
            add_ScanList_TagInfo(list, info);
        }
        if (info->elements < elements)  /* maximize element count */
            info->elements = elements;
    }
    else
    {   /* new tag */
        list = get_PLC_ScanList(plc, period, true);
        if (list)
            info = add_ScanList_Tag(list, string_tag, elements);
        else
        {
            EIP_printf(2, "drvEtherIP: cannot create list at %g secs"
                       "for tag '%s'\n", period, string_tag);
            info = 0;
        }
    }
    epicsMutexUnlock(plc->lock);
    return info;
}

void  drvEtherIP_add_callback (PLC *plc, TagInfo *info,
                               EIPCallback callback, void *arg)
{
    TagCallback *cb;
    epicsMutexLock(plc->lock);
    for (cb = DLL_first(TagCallback, &info->callbacks);
         cb;  cb = DLL_next(TagCallback, cb))
    {
        if (cb->callback == callback  &&  cb->arg == arg)
        {
            epicsMutexUnlock(plc->lock);
            return;
        }
    }
    /* Add new one */
    if (!(cb = (TagCallback *) malloc(sizeof (TagCallback))))
        return;
    cb->callback = callback;
    cb->arg      = arg;
    DLL_append(&info->callbacks, cb);
    epicsMutexUnlock(plc->lock);
}

void drvEtherIP_remove_callback (PLC *plc, TagInfo *info,
                                 EIPCallback callback, void *arg)
{
    TagCallback *cb;
    epicsMutexLock(plc->lock);
    for (cb = DLL_first(TagCallback, &info->callbacks);
         cb;  cb=DLL_next(TagCallback, cb))
    {
        if (cb->callback == callback  &&  cb->arg == arg)
        {
            DLL_unlink(&info->callbacks, cb);
            free(cb);
            break;
        }
    }
    epicsMutexUnlock(plc->lock);
}


/* (Re-)connect to IOC,
 * (Re-)start scan tasks, one per PLC.
 * Returns number of tasks spawned.
 */
int drvEtherIP_restart()
{
    PLC    *plc;
    char   taskname[20];
    int    tasks = 0;
    size_t len;

    if (drvEtherIP_private.lock == 0) return 0;
    epicsMutexLock(drvEtherIP_private.lock);

#ifdef HAVE_314_API
    if (!databaseIsReady) {
        epicsMutexUnlock(drvEtherIP_private.lock);
        for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
             plc;  plc = DLL_next(PLC,plc))
        {
            if (plc->name)
            {
                EIP_printf(4, "drvEtherIP: Delaying launch of scan task for PLC '%s' until database ready\n",
                   plc->name);
            }
        }
        return 0;
    }
#endif

    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc;  plc = DLL_next(PLC,plc))
    {
        /* block scan task (if running): */
        epicsMutexLock(plc->lock);
        /* restart the connection:
         * disconnect, PLC_scan_task will reconnect */
        disconnect_PLC(plc);
        /* check the scan task */
#ifdef HAVE_314_API
        if (plc->scan_task_id==0)
#else
        if (plc->scan_task_id==0 || taskIdVerify(plc->scan_task_id)==ERROR)
#endif
        {
            len = strlen(plc->name);
            if (len > 16)
                len = 16;
            taskname[0] = 'E';
            taskname[1] = 'I';
            taskname[2] = 'P';
            memcpy(&taskname[3], plc->name, len);
            taskname[len+3] = '\0';
#ifdef HAVE_314_API
            plc->scan_task_id = epicsThreadCreate(
              taskname,
              epicsThreadPriorityHigh,
              epicsThreadGetStackSize(epicsThreadStackMedium),
              (EPICSTHREADFUNC)PLC_scan_task,
              (void *)plc);
#else
            plc->scan_task_id = taskSpawn(taskname,
                                          EIPSCAN_PRI,
                                          EIPSCAN_OPT,
                                          EIPSCAN_STACK,
                                          (FUNCPTR) PLC_scan_task,
                                          (int) plc,
                                          0, 0, 0, 0, 0, 0, 0, 0, 0);
#endif
            EIP_printf(5, "drvEtherIP: launch scan task for PLC '%s'\n",
                       plc->name);
            ++tasks;
        }
        epicsMutexUnlock(plc->lock);
    }
    epicsMutexUnlock(drvEtherIP_private.lock);
    return tasks;
}

/* Command-line communication test,
 * not used by the driver */
int drvEtherIP_read_tag(const char *ip_addr,
                        int slot,
                        const char *tag_name,
                        int elements,
                        int timeout)
{
    EIPConnection  *c = EIP_init();
    unsigned short port = ETHERIP_PORT;
    size_t         millisec_timeout = timeout;
    ParsedTag      *tag;
    const CN_USINT *data;
    size_t         data_size;

    if (! EIP_startup(c, ip_addr, port, slot,
                      millisec_timeout))
        return -1;
    tag = EIP_parse_tag(tag_name);
    if (tag)
    {
        data = EIP_read_tag(c, tag, elements, &data_size, 0, 0);
        if (data)
            dump_raw_CIP_data(data, elements);
        EIP_free_ParsedTag(tag);
    }
    EIP_shutdown(c);
    EIP_dispose(c);
    return 0;
}


/* Jeff Hill noticed that driver could invoke for example ao record callbacks,
 * i.e. call scanOnce() on a record, while the IOC is still starting up
 * and the "onceQ" ring buffer is not initalized.
 *
 * drvEtherIP_restart will therefore not perform anything until the database
 * is available as indicated by the init hook setting databaseIsReady
 */
#ifdef HAVE_314_API
void drvEtherIP_initHook ( initHookState state )
{
    if (drvEtherIP_private.lock == 0) return;
    if ( state == initHookAfterScanInit ) {
        epicsMutexLock(drvEtherIP_private.lock);
        databaseIsReady = true;
        epicsMutexUnlock(drvEtherIP_private.lock);
        drvEtherIP_restart();
    }
}
#endif

static long drvEtherIP_drvInit ()
{
    /*
     * astonished to discover that initHookRegister is
     * in initHooks.h in R3.13, but not in iocCore
     * object file in R3.13
     */
#ifdef HAVE_314_API
    int status = initHookRegister ( drvEtherIP_initHook );
    if ( status ) {
        errlogPrintf (
              "drvEtherIP_drvInit: init hook install failed\n" );
    }
#endif
    return 0;
}

/* EPICS driver support entry table */
struct
{
    long number;
    long (* report) ();
    long (* init) ();
} drvEtherIP =
{
    2,
    drvEtherIP_report,
    drvEtherIP_drvInit
};

#ifdef HAVE_314_API
epicsExportAddress(drvet,drvEtherIP);
#endif

