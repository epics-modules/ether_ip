/* $Id$
 *
 * EtherNet/IP: ControlNet over Ethernet
 *
 * Test program for hosts.
 *
 * kasemir@lanl.gov
 */

#include<memory.h>
#include<stdio.h>
#include<string.h>
#include<stddef.h>
#include<time.h>
#include"ether_ip.c"

#ifdef DEFINE_CONNECTED_METHODS

eip_bool ForwardOpen(EIPConnection *c)
{
    size_t      request_size = CM_Forward_Open_size ();
    MR_Request  *request;
    EncapsulationRRData      *data;
    CM_Forward_Open_Good_Response   *open_response;

    calc_tick_time (245760,
                    &c->params.priority_and_tick,
                    &c->params.connection_timeout_ticks);
    c->params.O2T_CID = 0;
    c->params.T2O_CID = 2;
    c->params.connection_serial = 3;
    c->params.vendor_ID = 0xface;
    c->params.originator_serial = 0xeffec;

    request = EIP_make_SendRRData (c, request_size);
    if (! request)
        return false;

    if (! make_CM_Forward_Open (request, &c->params))
        return false;
    /* dump_CM_Forward_Open (request); */

    if (! EIP_send_connection_buffer (c))
    {
        printf ("Forward_Open: send failed\n");
        return 0;
    }

    if (! EIP_read_connection_buffer (c))
    {
        printf ("Forward_Open: No response\n");
        EIP_dump_connection (c);
        return 0;
    }
    data = (EncapsulationRRData *) c->buffer;

    if (data->rr.response.general_status != 0)
    {
        dump_CM_Forward_Open_Response (&data->rr.response, data->data_length);
        return false;
    }

    open_response = (CM_Forward_Open_Good_Response *)
        EIP_MR_Response_data (&data->rr.response, data->data_length, 0);
    c->params.O2T_CID = open_response->O2T_CID;

#define CHECK(ITEM)                             \
    if (open_response->ITEM != c->params.ITEM)  \
    {                                           \
        printf ("different " # ITEM "\n");      \
        return false;                           \
    }
    CHECK (T2O_CID)
    CHECK (connection_serial)
    CHECK (vendor_ID)
    CHECK (originator_serial)
#undef CHECK

    return true;
}

eip_bool ForwardClose(EIPConnection *c)
{
    size_t      request_size = CM_Forward_Close_size ();
    MR_Request  *request;
    EncapsulationRRData      *data;
    CM_Forward_Close_Good_Response  *close_response;

    request = EIP_make_SendRRData (c, request_size);
    if (! request)
        return false;

    if (! make_CM_Forward_Close (request, &c->params))
        return false;
    /* hexdump (request, request_size); */
    if (! EIP_send_connection_buffer (c))
    {
        printf ("Forward_Close: send failed\n");
        return 0;
    }

    if (! EIP_read_connection_buffer (c))
    {
        printf ("Forward_Close: No response\n");
        EIP_dump_connection (c);
        return 0;
    }
    data = (EncapsulationRRData *) c->buffer;
    close_response = (CM_Forward_Close_Good_Response *)
        EIP_MR_Response_data (&data->rr.response, data->data_length, 0);
    if (data->rr.response.general_status)
    {
        printf ("Forward_Close Error: ");
        if (data->rr.response.general_status == 0x01  &&
            data->rr.response.extended_status_size == 1)
        {
            const CN_UINT *ext = MR_Response_ext_stat (&data->rr.response);
            switch (*ext)
            {
            case 0x0107: printf ("Connection not found\n");
                         return false;
            }
        }

        dump_MR_Response (&data->rr.response, data->data_length);
        return false;
    }

#define CHECK(ITEM)                             \
    if (close_response->ITEM != c->params.ITEM) \
    {                                           \
        printf ("different " # ITEM "\n");      \
        return false;                           \
    }
    CHECK (connection_serial)
    CHECK (vendor_ID)
    CHECK (originator_serial)
#undef CHECK

    return true;
}

eip_bool ReadConnected(EIPConnection *c, size_t count, const ParsedTag *tag[])
{
    size_t  i, msg_size[40], requests_size = 0, multi_size,
            single_response_size;
    MR_Request  *multi_request, *single_request;
    const MR_Response  *response, *single_response;
    EncapsulationUnitData      *unit_data;
    static CN_USINT sequence = 0;
    CN_USINT *data;
    size_t   data_size;

    /* size indiv. requests, pack into multi request, then Unconn. send */
    for (i=0; i<count; ++i)
    {
        msg_size[i] = CIP_ReadData_size (tag[i]);
        requests_size += msg_size[i];
    }
    multi_size = CIP_MultiRequest_size (count, requests_size);
    multi_request = make_SendUnitData (c, multi_size, ++sequence);
    if (! multi_request)
        return false;
    if (! prepare_CIP_MultiRequest (multi_request, count))
        return false;
    for (i=0; i<count; ++i)
    {
        single_request = CIP_MultiRequest_item (multi_request, i, msg_size[i]);
        if (! single_request)
            return false;
        if (! make_CIP_ReadData (single_request, tag[i], 1))
            return false;
    }

    if (! EIP_send_connection_buffer (c))
    {
        printf ("ReadConnected: send failed\n");
        return false;
    }

    if (! EIP_read_connection_buffer (c))
    {
        printf ("ReadConnected: No response\n");
        return false;
    }
    unit_data = (EncapsulationUnitData *) c->buffer;
    response = &unit_data->rr.response;

    if (! check_CIP_MultiRequest_Response (response))
    {
        EIP_dump_connection (c);
        return false;
    }

    for (i=0; i<count; ++i)
    {
        single_response =
            get_CIP_MultiRequest_Response (response,
                                           unit_data->data_length,
                                           i,
                                           &single_response_size);
        if (! single_response)
            return false;

        if (check_CIP_ReadData_Response (single_response, single_response_size))
        {
            data = EIP_raw_MR_Response_data (single_response, single_response_size,
                                             &data_size);
            dump_raw_CIP_data (data, data_size);
        }
    }

    return true;
}

void TestConnected(EIPConnection *c, const ParsedTag *tag[])
{
    printf ("ForwardOpen:\n");
    if (ForwardOpen (c))
        printf ("OK.\n");

    ReadConnected (c, 1, tag);

    printf ("ForwardClose:\n");
    if (ForwardClose (c))
        printf ("OK.\n");
}

#endif /* DEFINE_CONNECTED_METHODS */


#if 0
void stressC(EIPConnection *c, size_t count, const ParsedTag *tag[], size_t runs)
{
    TimerValue t;
    double secs;
    size_t i;

    ForwardOpen (c);
    TimerInit (&t);
    TimerStart (&t);
    for (i=0; i<runs; ++i)
    {
        ReadConnected (c, count, tag);
    }

    secs = TimerStop (&t);

    if (secs != 0.0)
    {
        printf ("%6.2f runs/sec, %6.2f msec/run ->",
                runs/secs, secs/runs*1000.0);
        printf ("%8.2f values/sec, %6.2f msec/value\n",
                runs*count/secs, secs/(runs*count)*1000.0);
    }
    else
        printf ("Try more runs\n");
    ForwardClose (c);
}

void stress(EIPConnection *c, size_t count, const ParsedTag *tags[], size_t runs)
{
    TimerValue t;
    double secs;
    size_t i;

    TimerInit (&t);
    TimerStart (&t);
    for (i=0; i<runs; ++i)
    {
        if (count == 1)
            ReadTag (c, tags[0], 1);
        else
            ReadTags (c, count, tags);
    }
    secs = TimerStop (&t);

    //count *= 40;
    if (secs != 0.0)
    {
        printf ("%6.2f runs/sec, %6.2f msec/run ->",
                runs/secs, secs/runs*1000.0);
        printf ("%8.2f values/sec, %6.2f msec/value\n",
                runs*count/secs, secs/(runs*count)*1000.0);
    }
    else
        printf ("Try more runs\n");
}
#endif

void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <flags> [tag]\n", progname);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -v verbosity\n");
    fprintf(stderr, "    -i ip  (as 123.456.789.001 or DNS name)\n");
    fprintf(stderr, "    -p port\n");
    fprintf(stderr, "    -s PLC slot in ControlLogix crate (default: 0)\n");
    fprintf(stderr, "    -t timeout (ms)\n");
    fprintf(stderr, "    -a array size\n");
    fprintf(stderr, "    -w <double value to write>\n");
    fprintf(stderr, "    -T times-to-do-all-this (default: 1)\n");
    exit(-1);
}

int main (int argc, const char *argv[])
{
    EIPConnection   *c = EIP_init();
    const char      *ip = "172.31.72.94";
    unsigned short  port = 0xAF12;
    int             slot = 0;
    size_t          timeout_ms  = 5000;
    size_t          elements = 1;
    ParsedTag       *tag = 0;
    const char      *arg;
    size_t          i;
    CN_REAL         writeval;
    eip_bool        write = false;
    size_t          test_runs = 1;
#ifndef _WIN32
    struct timeval  now;
#endif
    double          start, end, duration;

#ifdef _WIN32
    /* Win32 socket init. */
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 2);
    WSAStartup(wVersionRequested, &wsaData);
#endif

    EIP_verbosity = 5;

    /* parse arguments */
    for (i=1; i<(size_t)argc; ++i)
    {
        if (argv[i][0] == '-')
        {
#define         GETARG                                         \
            if (argv[i][2])  { arg = &argv[i  ][2]; }          \
                else         { arg = &argv[i+1][0]; ++i; }
            switch (argv[i][1])
            {
            case 'v':
            GETARG
                EIP_verbosity = atoi(arg);
                break;
            case 'i':
            GETARG
                ip = arg;
                break;
            case 'p':
            GETARG
                port = (unsigned short) strtol(arg, 0, 0);
                break;
            case 's':
            GETARG
                slot = (int) strtol(arg, 0, 0);
                break;
            case 'a':
            GETARG
                elements = atoi(arg);
                break;
            case 't':
            GETARG
                timeout_ms = atol(arg);
                break;
            case 'w':
            GETARG
                writeval = strtod(arg, NULL);
                write = true;
                break;
            case 'T':
            GETARG
                test_runs = atol(arg);
                if (test_runs <= 0)
                    test_runs = 1;
                break;
            default:
                usage (argv[0]);
#undef          GETARG
            }
        }
        else
        {
           tag = EIP_parse_tag(argv[i]);
        }
    }
    if (tag && EIP_verbosity >= 3)
    {
        char buffer[EIP_MAX_TAG_LENGTH];
        EIP_copy_ParsedTag(buffer, tag);
        EIP_printf(3, "Tag '%s'\n", buffer);
    }
#ifdef _WIN32
    start = (double) time(0);
#else
    gettimeofday(&now, NULL);
    start = now.tv_sec + now.tv_usec/1000000.0;
#endif
    for (i=0; i<test_runs; ++i)
    {
        if (EIP_startup(c, ip, port, slot, timeout_ms) && tag)
        {
            const CN_USINT *data = 0;
            size_t data_len;
            if (write)
            {
                CN_REAL real_buffer;
                pack_REAL((CN_USINT *)&real_buffer, writeval);
                EIP_write_tag(c, tag, T_CIP_REAL, 1,(CN_USINT*) &real_buffer, 0, 0);
            }
            else
                data = EIP_read_tag(c, tag, elements, &data_len, 0, 0);
            if (data)
                dump_raw_CIP_data (data, elements);
        }
        EIP_shutdown (c);
    }
    EIP_free_ParsedTag (tag);
    tag = 0;
#ifdef _WIN32
    end = (double) time(0);
#else
    gettimeofday(&now, NULL);
    end = now.tv_sec + now.tv_usec/1000000.0;
#endif
    duration = (double)(end - start);
    printf("%d test runs, %g seconds -> %f ms / tag\n",
           (unsigned)test_runs,
           duration,
           duration / test_runs * 1000.0);

    EIP_dispose(c);

#ifdef _WIN32
    /* Win32 socket shutdown */
    WSACleanup( );
#endif

    return 0;
}
