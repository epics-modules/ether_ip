/* drvEtherIP
 *
 * kasemir@lanl.gov
 */

#include "drvEtherIP.h"
#include "stdio.h"

/* THE structure for this driver
 * Note that each PLC entry has it's own lock
 * for the scanlists & statistics.
 * Each PLC's scan task uses that per-PLC lock,
 * calls to loop/add/list PLCs also use this
 * more global lock.
 */
static struct
{
    DL_List /*PLC*/ PLCs;
    SEM_ID          lock; /* lock for PLCs list */
}   drvEtherIP_private;

/* Locking:
 *
 * Issues:
 * a) Structures: adding PLCs, scanlists, tags
 *                moving tags between scanlists,
 *                modifying callbacks for tags
 * b) Data:       driver and device read/write tag's data
 *                and change the update flag
 *
 * Locks:
 *
 * 1) drvEtherIP_private.lock is for PLCs
 *    Everything that accesses >1 PLC takes this lock
 *
 * 2) PLC.lock is per-PLC
 *    All structural changes to a PLC take this lock
 *    Currently PLCs are added, never removed,
 *    so the global lock is not affected by this
 *    PLC.lock is for all data structures for this PLC:
 *    scanlists, tags, callbacks.
 *
 *    PLC_scan_task needs access to connection & scanlists,
 *    so it takes lock for each run down the scanlist.
 *    (actually might update lame_count when not locked,
 *     but not fatal since PLC is never deleted)
 *
 * 3) TagInfo.lock is the Data lock.
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
 *    -> write behaviour must not change accross a->c.
 *    The network transfer between b) and c) takes time,
 *    so we avoid locking the data and do_write flag all that time.
 *    -> Data is locked in c) only to keep the device from looking at
 *       immature data. The driver keeps the state of do_write from a)
 *       in is_writing. If the device sets do_write after a), it's
 *       ignored until the next scan. (Otherwise the device would
 *       be locked until the next scan!)
 */

/* ------------------------------------------------------------
 * TagInfo
 * ------------------------------------------------------------ */

static void dump_TagInfo (const TagInfo *info, int level)
{
    bool have_sem;
    
    printf ("*** Tag '%s' @ 0x%X:\n", info->string_tag, (unsigned int)info);
    if (level > 3)
    {
        printf ("  scanlist            : 0x%X\n",
                (unsigned int)info->scanlist);
        printf ("  compiled tag        : ");
        EIP_dump_ParsedTag (info->tag);
        printf ("  elements            : %d\n", info->elements);
        printf ("  cip_r_request_size  : %d\n", info->cip_r_request_size);
        printf ("  cip_r_response_size : %d\n", info->cip_r_response_size);
        printf ("  cip_w_request_size  : %d\n", info->cip_w_request_size);
        printf ("  cip_w_response_size : %d\n", info->cip_w_response_size);
        printf ("  data_lock ID        : 0x%X\n",
                (unsigned int) info->data_lock);
    }
    have_sem = semTake (info->data_lock, sysClkRateGet()) == OK;
    if (! have_sem)
        printf ("  (CANNOT GET DATA LOCK!)\n");
    if (level > 3)
    {
        printf ("  data_size (buffer)  : %d\n",  info->data_size);
        printf ("  valid_data_size     : %d\n",  info->valid_data_size);
        printf ("  do_write            : %s\n",
                (info->do_write ? "yes" : "no"));
        printf ("  is_writing          : %s\n",
                (info->is_writing ? "yes" : "no"));
        printf ("  data              : ");
    }
    if (info->valid_data_size > 0)
        dump_raw_CIP_data (info->data, info->elements);
    else
        printf ("-no data-\n");
    if (have_sem)
        semGive (info->data_lock);
    if (level > 3)
        printf ("  transfer tick-time  : %d\n",  info->transfer_ticktime);
}

static TagInfo *new_TagInfo (const char *string_tag, size_t elements)
{
    TagInfo *info = (TagInfo *) calloc (sizeof (TagInfo), 1);
    if (!info ||
        !EIP_strdup (&info->string_tag,
                     string_tag, strlen(string_tag)))
        return 0;
    info->tag = EIP_parse_tag (string_tag);
    if (! info->tag)
    {
        EIP_printf (2, "new_TagInfo: failed to parse tag '%s'\n",
                    string_tag);
        return 0;
    }
    info->elements = elements;
    info->data_lock = semMCreate (EIP_SEMOPTS);
    if (! info->data_lock)
    {
        EIP_printf (0, "new_TagInfo (%s): Cannot create semaphore\n",
                    string_tag);
        return 0;
    }
    DLL_init (&info->callbacks);

    return info;
}
    
static void free_TagInfo (TagInfo *info)
{
    EIP_free_ParsedTag (info->tag);
    free (info->string_tag);
    if (info->data_size > 0)
        free (info->data);
    semDelete (info->data_lock);
    free (info);
}

/* ------------------------------------------------------------
 * ScanList
 * ------------------------------------------------------------
 *
 * NOTE: None of these ScanList funcs do any locking,
 * The caller has to do that!
 */

static void dump_ScanList (const ScanList *list, int level)
{
    const TagInfo *info;

    printf ("Scanlist          %g secs (%d ticks) @ 0x%X:\n",
            list->period, list->period_ticks, (unsigned int)list);
    printf ("  Status        : %s\n",
            (list->enabled ? "enabled" : "DISABLED"));
    printf ("  Last scan     : %ld ticks\n", list->scan_ticktime);
    if (level > 4)
    {
        printf ("  Errors        : %u\n", list->list_errors);
        printf ("  Next scan     : %ld ticks\n", list->scheduled_ticktime);
        printf ("  Min. scan time: %d ticks (%g secs)\n",
                list->min_scan_ticks,
                (double)list->min_scan_ticks/sysClkRateGet());
        printf ("  Max. scan time: %d ticks (%g secs)\n",
                list->max_scan_ticks,
                (double)list->max_scan_ticks/sysClkRateGet());
        printf ("  Last scan time: %d ticks (%g secs)\n",
                list->last_scan_ticks,
                (double)list->last_scan_ticks/sysClkRateGet());
    }
    if (level > 5)
    {
        for (info=DLL_first(TagInfo, &list->taginfos); info;
             info=DLL_next(TagInfo, info))
            dump_TagInfo (info, level);
    }
}

/* Set counters etc. to initial values */
static void reset_ScanList (ScanList *scanlist)
{
    scanlist->enabled = true;
    scanlist->period_ticks = scanlist->period * sysClkRateGet();
    scanlist->list_errors = 0;
    scanlist->scheduled_ticktime = 0;
    scanlist->min_scan_ticks = ~((size_t)0);
    scanlist->max_scan_ticks = 0;
    scanlist->last_scan_ticks = 0;
}

static ScanList *new_ScanList (PLC *plc, double period)
{
    ScanList *list = (ScanList *) calloc (sizeof (ScanList), 1);
    if (!list)
        return 0;
    DLL_init (&list->taginfos);
    list->plc = plc;
    list->period = period;
    reset_ScanList (list);
    return list;
}

static void free_ScanList (ScanList *scanlist)
{
    TagInfo *info;
    while ((info = DLL_decap (&scanlist->taginfos)) != 0)
        free_TagInfo (info);
    free (scanlist);
}

/* Find tag by name, returns 0 for "not found" */
static TagInfo *find_ScanList_Tag (const ScanList *scanlist,
                                   const char *string_tag)
{
    TagInfo *info;
    for (info=DLL_first(TagInfo, &scanlist->taginfos); info;
         info=DLL_next(TagInfo, info))
    {
        if (strcmp (info->string_tag, string_tag)==0)
            return info;
    }
    return 0;
}

/* remove/add TagInfo */
static void remove_ScanList_TagInfo (ScanList *scanlist,
                                     TagInfo *info)
{
    info->scanlist = 0;
    DLL_unlink (&scanlist->taginfos, info);
}

static void add_ScanList_TagInfo (ScanList *scanlist,
                                  TagInfo *info)
{
    DLL_append (&scanlist->taginfos, info);
    info->scanlist = scanlist;
}

/* Add new tag to taglist, compile tag
 * returns 0 on error */
static TagInfo *add_ScanList_Tag (ScanList *scanlist,
                                  const char *string_tag,
                                  size_t elements)
{
    TagInfo *info = new_TagInfo (string_tag, elements);
    if (info)
        add_ScanList_TagInfo (scanlist, info);
    return info;
}

/* ------------------------------------------------------------
 * PLC
 * ------------------------------------------------------------ */

static PLC *new_PLC (const char *name)
{
    PLC *plc = (PLC *) calloc (sizeof (PLC), 1);
    if (! (plc && EIP_strdup (&plc->name, name, strlen(name))))
        return 0;
    DLL_init (&plc->scanlists);
    plc->lock = semMCreate (EIP_SEMOPTS);
    if (! plc->lock)
    {
        EIP_printf (0, "new_PLC (%s): Cannot create semaphore\n", name);
        return 0;
    }
    return plc;
}

static void free_PLC (PLC *plc)
{
    ScanList *list;
    
    semDelete (plc->lock);
    free (plc->name);
    free (plc->ip_addr);
    while ((list = DLL_decap (&plc->scanlists)) != 0)
        free_ScanList (list);
    free (plc);
}

/* After TagInfos are defined (tag & elements are set),
 * fill rest of TagInfo: request/response size.
 *
 * Returns OK if any TagInfo in the scanlists could be filled,
 * so we believe that scanning this PLC makes some sense.
 */
static bool complete_PLC_ScanList_TagInfos (PLC *plc)
{
    ScanList       *list;
    TagInfo        *info;
    const CN_USINT *data;
    bool           any_ok = false;
    size_t         type_and_data_len;

    for (list=DLL_first(ScanList, &plc->scanlists); list;
         list=DLL_next(ScanList, list))
    {
        for (info=DLL_first(TagInfo, &list->taginfos); info;
             info=DLL_next(TagInfo, info))
        {
            if (info->cip_r_request_size || info->cip_r_response_size)
                continue;           /* don't look twice */
            data = EIP_read_tag (&plc->connection,
                                 info->tag, info->elements,
                                 NULL /* data_size */,
                                 &info->cip_r_request_size,
                                 &info->cip_r_response_size);
            if (data)
            {
                any_ok = true;
                /* Estimate write sizes from the request/response for read */
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
                info->cip_r_request_size  = 0;
                info->cip_r_response_size = 0;
                info->cip_w_request_size  = 0;
                info->cip_w_response_size = 0;
            }
        }
    }
    return any_ok;
}

static void invalidate_PLC_tags (PLC *plc)
{
    ScanList *list;
    TagInfo  *info;
    
    for (list=DLL_first(ScanList,&plc->scanlists);
         list;
         list=DLL_next (ScanList,list))
    {
        for (info = DLL_first (TagInfo, &list->taginfos);
             info;
             info = DLL_next(TagInfo, info))
        {
            if (semTake (info->data_lock, sysClkRateGet()) == OK)
            {
                info->valid_data_size = 0;
                semGive (info->data_lock);
            }
        }
    }
}

/* Test if we are connected, if not try to connect to PLC */
static bool assert_PLC_connect (PLC *plc)
{
    if (plc->connection.sock)
        return true;
    return EIP_startup (&plc->connection, plc->ip_addr,
                        ETHERIP_PORT, ETHERIP_TIMEOUT)
        && complete_PLC_ScanList_TagInfos (plc);
}

static void disconnect_PLC (PLC *plc)
{
    if (plc->connection.sock)
    {
        EIP_shutdown (&plc->connection);
        invalidate_PLC_tags (plc);
    }
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
static size_t determine_MultiRequest_count (size_t limit,
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
    for (; info; info = DLL_next(TagInfo, info))
    {
        if (info->cip_r_request_size <= 0)
            continue;
        if (info->do_write)
        {
            info->is_writing = true;
            try_req  = *requests_size  + info->cip_w_request_size;
            try_resp = *responses_size + info->cip_w_response_size;
        }
        else
        {
            try_req  = *requests_size  + info->cip_r_request_size;
            try_resp = *responses_size + info->cip_r_response_size;
        }
        *multi_request_size  = CIP_MultiRequest_size  (count+1, try_req);
        *multi_response_size = CIP_MultiResponse_size (count+1, try_resp);
        if (*multi_request_size  > limit ||
            *multi_response_size > limit)
        {   /* more won't fit */
            *multi_request_size =CIP_MultiRequest_size (count,*requests_size);
            *multi_response_size=CIP_MultiResponse_size(count,*responses_size);
            break;
        }
        ++count; /* ok, include another request */
        *requests_size  = try_req;
        *responses_size = try_resp;
    }

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
static bool process_ScanList (EIPConnection *c, ScanList *scanlist)
{
    TagInfo             *info, *info_position;
    size_t              count, requests_size, responses_size;
    size_t              multi_request_size, multi_response_size;
    size_t              send_size, i;
    CN_USINT            *send_request, *multi_request, *request;
    const CN_USINT      *response, *single_response, *data;
    EncapsulationRRData rr_data;
    size_t              single_response_size, data_size, transfer_ticktime;
    TagCallback         *cb;
    bool                ok;

    info = DLL_first (TagInfo, &scanlist->taginfos);
    while (info)
    {
        info_position = info; /* keep position, we'll loop several times */
        /* See how many transfers can be packed together */
        count = determine_MultiRequest_count (c->transfer_buffer_limit,
                                              info,
                                              &requests_size,
                                              &responses_size,
                                              &multi_request_size,
                                              &multi_response_size);
        if (count == 0)
            return true;
        /* send <count> requests */
        send_size = CM_Unconnected_Send_size (multi_request_size);
        if (!(send_request =EIP_make_SendRRData(c, send_size))            ||
            !(multi_request=make_CM_Unconnected_Send(send_request,
                                                     multi_request_size)) ||
            !prepare_CIP_MultiRequest(multi_request, count))
            return false;
        for (i=0; i<count; info=DLL_next(TagInfo, info))
        {
            if (info->cip_r_request_size <= 0)
                continue;
            if (info->is_writing)
            {
                info->do_write = false; /* ack. */
                request = CIP_MultiRequest_item (multi_request,
                                                 i, info->cip_w_request_size);
                if (!request ||
                    semTake (info->data_lock, sysClkRateGet()) != OK)
                    return false;
                ok = make_CIP_WriteData (request, info->tag,
                                         (CIP_Type)
                                         get_CIP_typecode(info->data),
                                         info->elements,
                                         info->data + CIP_Typecode_size)
                    != NULL;
                semGive (info->data_lock);
                if (!ok)
                    return false;
            }
            else
            {   /* reading, !is_writing */
                request = CIP_MultiRequest_item (multi_request,
                                                 i, info->cip_r_request_size);
                if (!request ||
                    !make_CIP_ReadData (request, info->tag, info->elements))
                    return false;
            }
            ++i; /* increment here, not in for() -> skip empty tags */
        } /* for i=0..count */
        transfer_ticktime = tickGet ();
        if (!EIP_send_connection_buffer (c))
            return false;
        /* read & disassemble response */
        if (!EIP_read_connection_buffer (c))
        {
            EIP_printf (2, "EIP process_ScanList: No response\n");
            return false;
        }
        transfer_ticktime = tickGet () - transfer_ticktime;

        response = EIP_unpack_RRData (c->buffer, &rr_data);
        if (! check_CIP_MultiRequest_Response (response))
        {
            EIP_printf (2, "EIP process_ScanList: Error in response\n");
            return false;
        }
        for (info=info_position, i=0; i<count; info=DLL_next(TagInfo, info))
        {
            if (info->cip_r_request_size <= 0)
                continue;
            info->transfer_ticktime = transfer_ticktime;
            single_response =
                get_CIP_MultiRequest_Response (response, rr_data.data_length,
                                               i, &single_response_size);
            if (! single_response)
                return false;
            if (info->is_writing)
            {
                /* Could call check_CIP_WriteData_Response, but then what ?
                 * User will see error in the next read when old value pops
                 * up again. */
                info->is_writing = false;
            }
            else
            {
                data = check_CIP_ReadData_Response (single_response,
                                                    single_response_size,
                                                    &data_size);
                if (semTake (info->data_lock, sysClkRateGet()) != OK)
                    return false;
                if (data_size > 0  &&
                    EIP_reserve_buffer ((void **)&info->data,
                                        &info->data_size, data_size))
                {
                    memcpy (info->data, data, data_size);
                    info->valid_data_size = data_size;
                }
                else
                    info->valid_data_size = 0;
                semGive (info->data_lock);
                /* Call all registered callback for this tag */
                for (cb = DLL_first(TagCallback,&info->callbacks);
                     cb; cb=DLL_next(TagCallback,cb))
                    (*cb->callback) (cb->arg);
            }
            ++i;
        }
        /* "info" now on next unread TagInfo or 0 */
    }
    return true;
}

/* Scan task, one per PLC */
static void PLC_scan_task (PLC *plc)
{
    ScanList *list;
    ULONG    next_schedule_ticks, start_ticks;
    int      timeout_ticks;
    bool     transfer_ok;

    timeout_ticks = sysClkRateGet() * plc->connection.millisec_timeout / 1000;
    if (timeout_ticks <= 100)
        timeout_ticks = 100;

  scan_loop:
    next_schedule_ticks = 0;
    if (semTake (plc->lock, WAIT_FOREVER) == ERROR)
    {
        EIP_printf (1, "drvEtherIP scan task for PLC '%s'"
                    " cannot take plc->lock\n", plc->name);
        return;
    }
    if (!assert_PLC_connect (plc))
    {   /* don't rush since connection takes network bandwidth */
        semGive (plc->lock);
        taskDelay (timeout_ticks);
        goto scan_loop;
    }
    for (list=DLL_first(ScanList,&plc->scanlists);
         list;
         list=DLL_next (ScanList,list))
    {
        if (! list->enabled)
            continue;
        
        start_ticks = tickGet ();
        if (start_ticks >= list->scheduled_ticktime)
        {
            list->scan_ticktime = start_ticks;
            transfer_ok = process_ScanList (&plc->connection, list);
            list->last_scan_ticks = tickGet() - start_ticks;
            /* update statistics */
            if (list->last_scan_ticks > list->max_scan_ticks)
                list->max_scan_ticks = list->last_scan_ticks;
            if (list->last_scan_ticks < list->min_scan_ticks)
                list->min_scan_ticks = list->last_scan_ticks;
            if (transfer_ok) /* reschedule exactly */
                list->scheduled_ticktime = start_ticks+list->period_ticks;
            else
            {   /* delay, ignore extra due to error/timeout */
                ++list->list_errors;
                ++plc->plc_errors;
                disconnect_PLC (plc);
                list->scheduled_ticktime = tickGet() + timeout_ticks;
            }
        }
        /* Update tick time for closest list */
        if (next_schedule_ticks <= 0 ||
            next_schedule_ticks > list->scheduled_ticktime)
            next_schedule_ticks = list->scheduled_ticktime;
    }
    semGive (plc->lock);
    /* sleep until next turn */
    if (next_schedule_ticks > 0)
    {
        start_ticks = tickGet();
        if (start_ticks < next_schedule_ticks)
            taskDelay (next_schedule_ticks - tickGet());
        else /* no time to spare, getting behind: */
            ++plc->slow_scans; /* hmm, "plc" not locked... */
    }
    else
        taskDelay (10); /* fallback for empty/degenerate scan list */
    goto scan_loop;
}

/* Find PLC entry by name, maybe create a new one if not found */
static PLC *get_PLC (const char *name, bool create)
{
    PLC *plc;
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc; plc=DLL_next(PLC,plc))
    {
        if (strcmp (plc->name, name) == 0)
            return plc;
    }
    if (! create)
        return 0;
    plc = new_PLC (name);
    if (plc)
        DLL_append (&drvEtherIP_private.PLCs, plc);
    return plc;
}

/* get (or create) ScanList for given rate */
static ScanList *get_PLC_ScanList (PLC *plc, double period,
                                   bool create)
{
    ScanList *list;
    for (list=DLL_first(ScanList, &plc->scanlists); list;
         list=DLL_next(ScanList, list))
    {
        if (list->period == period)
            return list;
    }
    if (! create)
        return 0;
    list = new_ScanList (plc, period);
    if (list)
        DLL_append (&plc->scanlists, list);
    
    return list;
}

/* Find ScanList and TagInfo for given tag.
 * On success, pointer to ScanList and TagInfo are filled.
 */
static bool find_PLC_tag (PLC *plc,
                          const char *string_tag,
                          ScanList **list,
                          TagInfo **info)
{
     for (*list=DLL_first(ScanList,&plc->scanlists);
         *list;
         *list=DLL_next (ScanList,*list))
    {
        *info = find_ScanList_Tag (*list, string_tag);
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
    drvEtherIP_private.lock = semMCreate (EIP_SEMOPTS);
    if (! drvEtherIP_private.lock)
        EIP_printf (0, "drvEtherIP_init cannot create semaphore!\n");
    DLL_init (&drvEtherIP_private.PLCs);
}

void drvEtherIP_help ()
{
    printf ("drvEtherIP, diagnostics routines:\n");
    printf ("    int EIP_verbosity:\n");
    printf ("    -  set to 0..10\n");
    printf ("    drvEtherIP_define_PLC <name>, <ip_addr>\n");
    printf ("    drvEtherIP_read_tag <ip>, <tag>, <elements>, <ms_timeout>\n");
    printf ("    -  call to test a round-trip single tag read\n");
    printf ("    drvEtherIP_report <level>\n");
    printf ("    -  level = 0..10\n");
    printf ("    drvEtherIP_dump\n");
    printf ("    drvEtherIP_reset_statistics\n");
    printf ("    drvEtherIP_restart\n");
    printf ("\n");
}

long drvEtherIP_report (int level)
{
    PLC *plc;
    ScanList *list;
    bool have_drvlock, have_PLClock;
    
    printf ("drvEtherIP V%d.%d report, -*- outline -*-\n",
            ETHERIP_MAYOR, ETHERIP_MINOR);
    if (level > 0)
        printf ("  SEM_ID lock: 0x%X\n",
                (unsigned int) drvEtherIP_private.lock);
    have_drvlock = semTake (drvEtherIP_private.lock, sysClkRateGet()*5) == OK;
    if (! have_drvlock)
        printf ("   CANNOT GET DRIVER'S LOCK!\n");
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc; plc=DLL_next(PLC,plc))
    {
        printf ("* PLC '%s', IP '%s':\n", plc->name, plc->ip_addr);
        if (level > 0)
        {
            printf ("  scan thread slow count: %u\n", plc->slow_scans);
            printf ("  connection errors     : %u\n", plc->plc_errors);
        }
        if (level > 1)
        {
            printf ("  SEM_ID lock           : 0x%X\n",
                    (unsigned int) plc->lock);
            printf ("  scan task ID          : 0x%X (%s)\n",
                    plc->scan_task_id,
                    (taskIdVerify (plc->scan_task_id) == OK ?
                     "running" : "-dead-"));
            have_PLClock = semTake (plc->lock, sysClkRateGet()*5) == OK;
            if (! have_PLClock)
                printf ("   CANNOT GET PLC'S LOCK!\n");
            if (level > 2)
            {
                printf ("** ");
                EIP_dump_connection (&plc->connection);
            }
            if (level > 3)
            {
                for (list=DLL_first(ScanList, &plc->scanlists); list;
                     list=DLL_next(ScanList, list))
                {
                    printf ("** ");
                    dump_ScanList (list, level);
                }
            }
            if (have_PLClock)
                semGive (plc->lock);
        }
    }
    if (have_drvlock)
        semGive (drvEtherIP_private.lock);
    printf ("\n");
    return 0;
}

void drvEtherIP_dump ()
{
    PLC      *plc;
    ScanList *list;
    TagInfo  *info;
    
    semTake (drvEtherIP_private.lock, WAIT_FOREVER);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc; plc=DLL_next(PLC,plc))
    {
        semTake (plc->lock, WAIT_FOREVER);
        printf ("PLC %s\n", plc->name);
        for (list=DLL_first(ScanList, &plc->scanlists); list;
             list=DLL_next(ScanList, list))
        {
            for (info=DLL_first(TagInfo, &list->taginfos); info;
                 info=DLL_next(TagInfo, info))
            {
                printf ("%s ", info->string_tag);
                if (info->valid_data_size >= 0)
                    dump_raw_CIP_data (info->data, info->elements);
                else
                    printf (" - no data -\n");
            }
        }
        semGive (plc->lock);
    }
    semGive (drvEtherIP_private.lock);
    printf ("\n");
}

void drvEtherIP_reset_statistics ()
{
    PLC *plc;
    ScanList *list;

    semTake (drvEtherIP_private.lock, WAIT_FOREVER);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc; plc=DLL_next(PLC,plc))
    {
        semTake (plc->lock, WAIT_FOREVER);
        plc->plc_errors = 0;
        plc->slow_scans = 0;
        for (list=DLL_first(ScanList, &plc->scanlists); list;
             list=DLL_next(ScanList, list))
            reset_ScanList (list);
        semGive (plc->lock);
    }
    semGive (drvEtherIP_private.lock);
}

/* Create a PLC entry:
 * name : identifier
 * ip_address: DNS name or dot-notation
 */
bool drvEtherIP_define_PLC (const char *PLC_name, const char *ip_addr)
{
    PLC *plc;

    semTake (drvEtherIP_private.lock, WAIT_FOREVER);
    plc = get_PLC (PLC_name, true);
    if (! plc)
    {
        semGive (drvEtherIP_private.lock);
        return false;
    }
    EIP_strdup (&plc->ip_addr, ip_addr, strlen(ip_addr));
    semGive (drvEtherIP_private.lock);
    return true;
}

/* Returns PLC or 0 if not found */
PLC *drvEtherIP_find_PLC (const char *PLC_name)
{
    PLC *plc;

    semTake (drvEtherIP_private.lock, WAIT_FOREVER);
    plc = get_PLC (PLC_name, /*create*/ false);
    semGive (drvEtherIP_private.lock);

    return plc;
}

/* After the PLC is defined with drvEtherIP_define_PLC,
 * tags can be added
 */
TagInfo *drvEtherIP_add_tag (PLC *plc, double period,
                             const char *string_tag, size_t elements)
{
    ScanList *list;
    TagInfo  *info;

    semTake (plc->lock, WAIT_FOREVER);
    if (find_PLC_tag (plc, string_tag, &list, &info))
    {   /* check if period is OK */
        if (list->period > period)
        {   /* current scanlist is too slow */
            remove_ScanList_TagInfo (list, info);
            list = get_PLC_ScanList (plc, period, true);
            if (!list)
            {
                semGive (plc->lock);
                EIP_printf (2, "drvEtherIP: cannot create list at %g secs"
                            "for tag '%s'\n", period, string_tag);
                return 0;
            }
            add_ScanList_TagInfo (list, info);
        }
        if (info->elements < elements)  /* maximize element count */
            info->elements = elements;
    }
    else
    {   /* new tag */
        list = get_PLC_ScanList (plc, period, true);
        if (list)
            info = add_ScanList_Tag (list, string_tag, elements);
        else
        {
            EIP_printf (2, "drvEtherIP: cannot create list at %g secs"
                        "for tag '%s'\n", period, string_tag);
            info = 0;
        }
    }
    semGive (plc->lock);
    
    return info;
}

void  drvEtherIP_add_callback (PLC *plc, TagInfo *info,
                               EIPCallback callback, void *arg)
{
    TagCallback *cb;
    
    semTake (plc->lock, WAIT_FOREVER);
    for (cb = DLL_first(TagCallback, &info->callbacks);
         cb; cb = DLL_next(TagCallback, cb))
    {
        if (cb->callback == callback &&
            cb->arg      == arg)
        {
            semGive (plc->lock);
            return;
        }
    }
    /* Add new one */
    cb = (TagCallback *) malloc (sizeof (TagCallback));
    if (! cb)
        return;
    
    cb->callback = callback;
    cb->arg      = arg;
    DLL_append (&info->callbacks, cb);
    semGive (plc->lock);
}

void drvEtherIP_remove_callback (PLC *plc, TagInfo *info,
                                 EIPCallback callback, void *arg)
{
    TagCallback *cb;

    semTake (plc->lock, WAIT_FOREVER);
    for (cb = DLL_first(TagCallback, &info->callbacks);
         cb; cb=DLL_next(TagCallback, cb))
    {
        if (cb->callback == callback &&
            cb->arg      == arg)
        {
            DLL_unlink (&info->callbacks, cb);
            free (cb);
            break;
        }
    }
    semGive (plc->lock);
}

/* (Re-)connect to IOC,
 * (Re-)start scan tasks, one per PLC.
 * Returns number of tasks spawned.
 */
int drvEtherIP_restart ()
{
    PLC    *plc;
    char   taskname[20];
    int    tasks = 0;
    size_t len;
    
    semTake (drvEtherIP_private.lock, WAIT_FOREVER);
    for (plc = DLL_first(PLC,&drvEtherIP_private.PLCs);
         plc; plc=DLL_next(PLC,plc))
    {
        /* block scan task (if running): */
        semTake (plc->lock, WAIT_FOREVER);
        /* restart the connection:
         * disconnect, PLC_scan_task will reconnect */
        disconnect_PLC (plc);
        /* check the scan task */
        if (plc->scan_task_id==0 || taskIdVerify(plc->scan_task_id)==ERROR)
        {
            len = strlen (plc->name);
            if (len > 16)
                len = 16;
            taskname[0] = 'E';
            taskname[1] = 'I';
            taskname[2] = 'P';
            memcpy (&taskname[3], plc->name, len);
            taskname[len+3] = '\0';
            plc->scan_task_id = taskSpawn (taskname,
                                           EIPSCAN_PRI,
                                           EIPSCAN_OPT,
                                           EIPSCAN_STACK,
                                           (FUNCPTR) PLC_scan_task,
                                           (int) plc,
                                           0, 0, 0, 0, 0, 0, 0, 0, 0);
            EIP_printf (5, "drvEtherIP: launch scan task for PLC '%s'\n",
                        plc->name);
            ++tasks;
        }
        semGive (plc->lock);
    }
    semGive (drvEtherIP_private.lock);
    return tasks;
}
    
/* Command-line communication test,
 * not used by the driver */
int drvEtherIP_read_tag (const char *ip_addr,
                         const char *tag_name,
                         int elements,
                         int timeout)
{
    EIPConnection  c;
    unsigned short port = ETHERIP_PORT;
    size_t         millisec_timeout = timeout;
    ParsedTag      *tag;
    const CN_USINT *data;
    size_t         data_size;
            
    if (! EIP_startup (&c, ip_addr, port, millisec_timeout))
        return -1;
    tag = EIP_parse_tag (tag_name);
    if (tag)
    {
        data = EIP_read_tag (&c, tag, elements, &data_size, 0, 0);
        if (data)
            dump_raw_CIP_data (data, elements);
        EIP_free_ParsedTag (tag);
    }
    EIP_shutdown (&c);

    return 0;
}

/* EPICS driver support entry table */
struct
{
    long number;
    long (* report) ();
    long (* inie) ();
} drvEtherIP =
{
    2,
    drvEtherIP_report,
    NULL
};


    

