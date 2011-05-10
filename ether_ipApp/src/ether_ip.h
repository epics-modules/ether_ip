/* $Id: ether_ip.h,v 1.10 2011/04/12 18:08:48 saa Exp $
 *
 * ether_ip
 *
 * EtherNet/IP routines for Win32, Unix, vxWorks, and RTEMS.
 *
 * EtherNet/IP started as "ControlNet over Ethernet" (www.controlnet.org),
 * now defined as ODVA's "EtherNet/IP"   (www.odva.org)
 *
 * Docs:  "Spec" = ControlNet Spec. version 2.0, Errata 1
 *        "ENET" = AB Publication 1756-RM005A-EN-E
 *
 * kasemir@lanl.gov
 */

#ifndef NO_EPICS
#include"epicsVersion.h"
#endif


/* The socket compatibility stuff is stolen from R3.13 osiSock.h.
 * The R3.14 osiSock is a tad different, so we use neither "as is"
 * but have it all in here.
 */
/* sys-independ. socket stuff, basically stolen from osiSock.h  */
#ifdef _WIN32
#include <winsock2.h>
#pragma pack(push, 1)

#define EIP_SOCKET          SOCKET
#define EIP_INVALID_SOCKET  INVALID_SOCKET
#define EIP_SOCKERRNO       WSAGetLastError()
#define EIP_socket_close(S) closesocket(S)
#define EIP_socket_ioctl(A,B,C) ioctlsocket(A,B,C)
#define EIP_SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define EIP_SOCK_EINPROGRESS WSAEINPROGRESS
/* end of Win32 settings */

#else

#ifdef vxWorks
#include <vxWorks.h>
#include <sysLib.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <sockLib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <inetLib.h>
#include <ioLib.h>
#include <hostLib.h>
#include <selectLib.h>
#include <ctype.h>
#include <tickLib.h>
typedef int               EIP_SOCKET;
#define EIP_INVALID_SOCKET    (-1)
#define EIP_SOCKERRNO         errno
#define EIP_socket_close(S)   close(S)
#define EIP_socket_ioctl(A,B,C) ioctl(A,B,(int)C)
#define EIP_SOCK_EWOULDBLOCK  EWOULDBLOCK
#define EIP_SOCK_EINPROGRESS  EINPROGRESS
/* end of vxWorks settings */

#else

/* Unix settings */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
typedef int               EIP_SOCKET;
#define EIP_INVALID_SOCKET    (-1)
#define EIP_SOCKERRNO         errno
#define EIP_socket_close(S)   close(S)
#define EIP_socket_ioctl(A,B,C) ioctl(A,B,C)
#define EIP_SOCK_EWOULDBLOCK  EWOULDBLOCK
#define EIP_SOCK_EINPROGRESS  EINPROGRESS

#ifdef SOLARIS
#include <sys/filio.h>
#endif

/* end of Unix settings */
#endif
#endif

#include "eip_bool.h"

/* This could be an application on its own...
 * Rough idea:
 * 10: Dump all protocol details
 *  9: Hexdump each sent/received buffer
 *
 *  6: show driver details
 *  5: device will show write-related operations
 *
 *  2: show more error info: "xxx failed because..."
 *  1: show error messages: "xxx failed"
 *  0: keep quiet
 */
extern int EIP_verbosity;

/* print if EIP_verbosity >= level */
void EIP_printf(int level, const char *format, ...);

/* print with time stamp if EIP_verbosity >= level */
void EIP_printf_time(int level, const char *format, ...);

void EIP_hexdump(int level, const void *_data, int len);

/* There is a limit for the PLC transfer buffer.
 * Spec page 2-5 mentions 504 bytes as the CIP UCMM limit,
 * Spec page 156 in version 2.0, errata 2 mentions 511 bytes
 * as the Forward_Open connection size limit.
 * None if this tells me exactly where the limit is
 * and whether it's 500 or 504 or ??
 * Is it in the PLC controller, so the Ethernet overhead
 * (encapsulation header) is handled by the ENET module
 * and does not count?
 * Is it in the ENET module, so the total including encapsulation
 * is limited?
 *
 * Test: Used command-line EtherIP tool to read elements of SINT[].
 * 492 elements: Results in 538 byte response, OK
 * 493 elements: Results in 538 byte response, Fault
 *               (error 0x06, Buffer too small, partial data only)
 * Likewise INT[] array: <=246 elements, <=538 byte response, OK
 * Note: That command-line tool sends a single read request.
 *       The EPICS driver sends read/write requests within
 *       a multi-request, so the individual array reads must
 *       be slightly smaller for the driver! While the command-line
 *       tool can read INT[246], the driver cannot!
 *
 * Possible conclusions:
 * -> Total response limited to 538 bytes?
 * 538 - EncapsulationHeader(24)                              = 514
 *     - RR Data handle & timeout(6)                          = 508
 *     - RR Data packet head.:count, address,..., length (10) = 498
 * -> MR_Response & data limited to 498 bytes?
 *
 * Tests w/ driver on IOC indicated that PLC also complains when
 * the _request_ reaches 538 bytes
 * -> request & response each limited to 538 bytes.
 *
 * These macros used to be the best guess for the buffer
 * plus the protocol overhead (encap. header), using
 * 538 - EIP_PROTOCOL_OVERHEAD
 * to fit the Multi-Request.
 * The response has an overhead of 40 bytes, see above: 24+6+10.
 * The request has an overhead of 52 bytes.
 *
 * However, the SNS CF PLCs seem to work best with a buffer limit
 * of 500, so that's now the EIP_DEFAULT_BUFFER_LIMIT.
 *
 * Overall, the driver uses a buffer of EIP_BUFFER_LIMIT bytes,
 * so nothing bigger than that can be transferred.
 */

/** Limit used by the driver when constructing requests and
 *  watching expected responses
 */
extern int EIP_buffer_limit;

/** Best estimate for EIP_buffer_limit */
#define EIP_DEFAULT_BUFFER_LIMIT 500

/** Used to be used to determine EIP_DEFAULT_BUFFER_LIMIT, but
 *  didn't work out
 */
#define EIP_PROTOCOL_OVERHEAD 52

/** Absolute maximum for EIP_buffer_limit because this is the
 *  buffer size that the driver uses to allocate the initial
 *  buffer.
 */
#define EIP_BUFFER_SIZE 600

/********************************************************
 * ControlNet data types
 * Spec 5 p 3
 ********************************************************/

 /* ControlNet uses Little Endian, not the common IP format!
  * Exception:
  * Addresses packed at sockaddr_in are in network order as usual.
  */
typedef signed char    CN_SINT;
typedef unsigned char  CN_USINT;
typedef unsigned short CN_UINT;
typedef short          CN_INT;
typedef unsigned int   CN_UDINT;
typedef int            CN_DINT;
typedef float          CN_REAL;

typedef enum
{
    C_Identity             = 0x01,
    C_MessageRouter        = 0x02,
    C_ConnectionManager    = 0x06
}   CN_Classes;

/********************************************************
 * Message Router PDU (Protocol Data Unit)
 ********************************************************/

typedef enum /* Spec 4, p.36 */
{
    S_Get_Attribute_All    = 0x01,
    S_Get_Attribute_Single = 0x0E,
    S_CIP_MultiRequest     = 0x0A,  /* Logix5000 Data Access */
    S_CIP_ReadData         = 0x4C,  /* Logix5000 Data Access */
    S_CIP_WriteData        = 0x4D,  /* Logix5000 Data Access */
    S_CM_Unconnected_Send  = 0x52,
    S_CM_Forward_Open      = 0x54,
    S_CM_Forward_Close     = 0x4E
}   CN_Services;

typedef struct
{
    CN_USINT   service;   /* one of CN_Services */
    CN_USINT   path_size; /* in number of UINTs ! */
    CN_USINT   path[1];   /* defined as UINT (padded), but easier to handle as USINT */
    /* request data: depends on service */
}   MR_Request;

typedef struct
{
    CN_USINT   service;
    CN_USINT   reserved;
    CN_USINT   general_status;
    CN_USINT   extended_status_size; /* .. in UINT words ! */
    /* maybe CN_UINT extended_status[] */
    CN_USINT   response[5];  /* depends on service */
}   MR_Response;

/* Dump a raw (net format) MR Response */
const CN_USINT *EIP_dump_raw_MR_Response(const CN_USINT *response,
                                         size_t response_size);

/* MR_Response has fixed portion followed by (maybe) an extended
 * status and then (maybe) data.
 * Get pointer to data, fill data_size
 */
CN_USINT *EIP_raw_MR_Response_data (const CN_USINT *response,
                                    size_t response_size,
                                    size_t *data_size);

/********************************************************
 * CM, Connection Manager
 ********************************************************/

/* Network Connection Parameters, spec 4 p. 28 */
#define CM_NCP_Type0               0
#define CM_NCP_Multicast          (1 << 13)
#define CM_NCP_Point2Point        (2 << 13)

#define CM_NCP_LowPriority         0
#define CM_NCP_HighPriority       (1 << 10)
#define CM_NCP_ScheduledPriority  (2 << 10)
#define CM_NCP_Urgent             (3 << 10)

#define CM_NCP_Fixed               0
#define CM_NCP_Variable           (1 << 9)

/* Transport and trigger, spec 4 p. 35 and 151 */
#define CM_Trig_Cyclic             0
#define CM_Trig_Change            (1 << 4)
#define CM_Trig_App               (2 << 4)

#define CM_Transp_IsServer         0x80

/* Data portion of CM_Unconnected_Send_Request */
typedef struct
{
    CN_USINT   priority_and_tick;
    CN_USINT   connection_timeout_ticks;
    CN_UINT    message_size;            /* in bytes, w/o pad */
    MR_Request message_router_PDU;      /* padded to 16bit boundary */
    /* then the second part follows */
}   CM_Unconnected_Send_Request;

typedef struct
{
    CN_USINT   path_size; /* in UINT words */
    CN_USINT   reserved;
    CN_USINT    path[2];
}   CM_Unconnected_Send_Request_2;

/* Data portion of CM_Forward_Open */
typedef struct
{
    CN_USINT   priority_and_tick;
    CN_USINT   connection_timeout_ticks;
    CN_UDINT   O2T_CID;
    CN_UDINT   T2O_CID;
    CN_UINT    connection_serial;
    CN_UINT    vendor_ID;          /* vendor + originator_serial: */
    CN_UDINT   originator_serial;  /* unique, hardcoded */
    CN_USINT   connection_timeout_multiplier;
    CN_USINT   reserved[3];
    CN_UDINT   O2T_RPI; /* requested packet interval, microsecs */
    CN_UINT    O2T_connection_parameters;
    CN_UDINT   T2O_RPI;
    CN_UINT    T2O_connection_parameters;
    CN_USINT   xport_type_and_trigger;
    CN_USINT   connection_path_size;
    CN_USINT   connection_path[1 /* path_size*2 */];
}   CM_Forward_Open_Request;

/* Data portion of reply to CM_Forward_Open */
typedef struct
{
    CN_UDINT   O2T_CID;
    CN_UDINT   T2O_CID;
    CN_UINT    connection_serial;
    CN_UINT    vendor_ID;
    CN_UDINT   originator_serial;
    CN_UDINT   O2T_API; /* actual packet interval, microsecs */
    CN_UDINT   T2O_API;
    CN_USINT   application_reply_size;
    CN_USINT   reserved;
    CN_UINT    application_reply[1 /* size*2 */];
}   CM_Forward_Open_Good_Response;

/* Data portion of CM_Forward_Close */
typedef struct
{
    CN_USINT   priority_and_tick;
    CN_USINT   connection_timeout_ticks;
    CN_UINT    connection_serial;
    CN_UINT    vendor_ID;
    CN_UDINT   originator_serial;
    CN_USINT   connection_path_size;
    CN_USINT   reserved;
    CN_USINT   connection_path[1 /* path_size*2 */];
}   CM_Forward_Close_Request;

/* Data portion of reply to CM_Forward_Close */
typedef struct
{
    CN_UINT    connection_serial;
    CN_UINT    vendor_ID;
    CN_UDINT   originator_serial;
    CN_USINT   application_reply_size;
    CN_USINT   reserved;
    CN_UINT    application_reply[1 /* size*2 */];
}   CM_Forward_Close_Good_Response;

size_t CM_Unconnected_Send_size (size_t message_size);

/* Fill MR_Request with Unconnected_Send for message of given size,
 * path: backplane,  given slot.
 * Returns pointer to message location in there
 * (to be filled, that's a nested, DIFFERENT request!)
 * or 0 on error
 */
CN_USINT *make_CM_Unconnected_Send(CN_USINT *request, size_t message_size,
                                   int slot);

/********************************************************
 * Logix5000
 ********************************************************/

/* Parses tags of the form
 *     name.name[element].name[element].name
 * and converts them into list of path elements
 */
typedef struct __ParsedTag ParsedTag;
struct __ParsedTag
{
    enum
    {
        te_name,
        te_element
    }           type;
    union
    {
        char   *name;
        size_t element;
    }           value;
    ParsedTag   *next;
};

/* (Abbreviated) type codes for CIP data
 * ENET p. 11
 * There are no 'unsigned' designators.
 */
typedef enum
{
    T_CIP_BOOL   = 0x00C1,
    T_CIP_SINT   = 0x00C2,
    T_CIP_INT    = 0x00C3,
    T_CIP_DINT   = 0x00C4,
    T_CIP_REAL   = 0x00CA,
    T_CIP_BITS   = 0x00D3,
    T_CIP_STRUCT = 0x02A0
} CIP_Type;

/* These are experimental:
 * The ENEC doc just shows several structures
 * for TIMER, COUNTER, CONTROL and indicates that
 * the T_CIP_STRUCT = 0x02A0 is followed by
 * two more bytes, shown as "?? ??".
 * Looks like for strings, those are always 0x0FCE,
 * followed by INT length, INT 0, 82 characters and more zeroes
 */
typedef enum
{
    T_CIP_STRUCT_STRING = 0x0FCE
} CIP_STRUCT_Type;

/* Size of appreviated type code.
 * We only use those with size 2, no structures */
#define CIP_Typecode_size 2

/* Get typecode from type/data ptr "td".
 * (Does not work for structs) */
#define get_CIP_typecode(td)  ( (CIP_Type)  (((CN_USINT *)td)[0]) )

/* Determine byte size of CIP_Type */
size_t CIP_Type_size(CIP_Type type);

/* Turn tag string into ParsedTag,
 * convert back into string and free it
 */
#define EIP_MAX_TAG_LENGTH 100
ParsedTag *EIP_parse_tag(const char *tag);
void EIP_copy_ParsedTag(char *buffer, const ParsedTag *tag);
void EIP_free_ParsedTag(ParsedTag *tag);

CN_USINT *make_CIP_ReadData(CN_USINT *request,
                            const ParsedTag *tag, size_t elements);
const CN_USINT *check_CIP_ReadData_Response(const CN_USINT *response,
                                            size_t response_size,
                                            size_t *data_size);

/* Fill buffer with CIP WriteData request
 * for tag, type of CIP data, given number of elements.
 * Also copies data into buffer,
 * data has to be in network format already!
 */
CN_USINT *make_CIP_WriteData(CN_USINT *buf, const ParsedTag *tag,
                             CIP_Type type, size_t elements,
                             CN_USINT *raw_data);
void dump_CIP_WriteRequest(const CN_USINT *request);
/* Test CIP_WriteData response: If not OK, report error */
eip_bool check_CIP_WriteData_Response(const CN_USINT *response,
                                  size_t response_size);

size_t CIP_MultiRequest_size(size_t count, size_t requests_size);
size_t CIP_MultiResponse_size(size_t count, size_t responses_size);
eip_bool prepare_CIP_MultiRequest(CN_USINT *request, size_t count);

CN_USINT *CIP_MultiRequest_item(CN_USINT *request,
                                size_t request_no,
                                size_t single_request_size);

eip_bool check_CIP_MultiRequest_Response(const CN_USINT *response,
                                     size_t response_size);
void dump_CIP_MultiRequest_Response_Error(const CN_USINT *response,
                                          size_t response_size);
const CN_USINT *get_CIP_MultiRequest_Response(const CN_USINT *response,
                                              size_t response_size,
                                              size_t reply_no,
                                              size_t *reply_size);

/* dump CIP data, type and data are in raw format */
void dump_raw_CIP_data(const CN_USINT *raw_type_and_data, size_t elements);
eip_bool get_CIP_double(const CN_USINT *raw_type_and_data,
                    size_t element, double *result);
eip_bool get_CIP_UDINT(const CN_USINT *raw_type_and_data,
                   size_t element, CN_UDINT *result);
eip_bool get_CIP_DINT(const CN_USINT *raw_type_and_data,
                  size_t element, CN_DINT *result);
eip_bool get_CIP_USINT(const CN_USINT *raw_type_and_data,
                       size_t element, CN_USINT *result);
/* Fill buffer with up to 'size' characters (incl. ending '\0').
 * Return true for success */
eip_bool get_CIP_STRING(const CN_USINT *raw_type_and_data,
                    char *buffer, size_t size);
eip_bool put_CIP_double(const CN_USINT *raw_type_and_data,
                    size_t element, double value);
eip_bool put_CIP_UDINT(const CN_USINT *raw_type_and_data,
                   size_t element, CN_UDINT value);
eip_bool put_CIP_DINT(const CN_USINT *raw_type_and_data,
                  size_t element, CN_DINT value);


/********************************************************
 * Encapsulation
 ********************************************************/

typedef struct
{
    CN_UINT     command;            /* Encapsulation_Command           */
    CN_UINT     length;             /* # bytes that follow this header */
    CN_UDINT    session;            /* returned by EC_RegisterSession  */
    CN_UDINT    status;
    CN_USINT    server_context[8];  /* anything I like                 */
    CN_UDINT    options;
}   EncapsulationHeader;

#define sizeof_EncapsulationHeader 24


/* Spec 4 p. 164 */
typedef enum
{
    EC_Nop                 = 0x0000,
    EC_ListInterfaces      = 0x0064,  /* "Encapsulation Overview" slides */
    EC_RegisterSession     = 0x0065,
    EC_UnRegisterSession   = 0x0066,
    EC_ListServices        = 0x0004,
    EC_SendRRData          = 0x006F,
    EC_SendUnitData        = 0x0070,
    EC_IndicateStatus      = 0x0072   /* "Encapsulation Overview" slides */
}   Encapsulation_Command;

typedef struct
{
    EncapsulationHeader header;
    /* "Common Packet Format": count + stuff */
    CN_UINT    count;
    struct ServiceInfo
    {
        CN_UINT     type;    /* 0x100: "Communications" */
        CN_UINT     length;
        CN_UINT     version;
        CN_UINT     flags;
        CN_USINT    name[16];
    }           service; /* 1 or more ... */
}   ListServicesReply;

typedef struct
{
    EncapsulationHeader header;
    CN_UINT             protocol_version;
    CN_UINT             options;
}   RegisterSessionData;

#define sizeof_RegisterSessionData (sizeof_EncapsulationHeader + 4)

/* A single PDU encapsulated for SendRRData commands */
typedef struct
{
    EncapsulationHeader header;
    CN_UDINT   interface_handle;
    CN_UINT    timeout;
    /* Common Packet Format: */
    CN_UINT     count; /* >= 2 */

    /* Item 1 */
    CN_UINT     address_type;
    CN_UINT     address_length;
    /* could have addr. info in here .. */

    /* Item 2 */
    CN_UINT     data_type;
    CN_UINT     data_length;

    /* Following: MR_Request or MR_Response */
}   EncapsulationRRData;

/* EncapsulationRRData up to the MR_Request/Response (exclusive) */
#define sizeof_EncapsulationRRData (sizeof_EncapsulationHeader + 16)


/* A single PDU encapsulated for SendUnitData commands */
typedef struct
{
    EncapsulationHeader header;
    CN_UDINT   interface_handle;
    CN_UINT    timeout;
    /* Common Packet Format: */
    CN_UINT     count; /* >= 2 */

    /* Item 1 */
    CN_UINT     address_type;
    CN_UINT     address_length;
    CN_UDINT    CID;

    /* Item 2 */
    CN_UINT     data_type;
    CN_UINT     data_length;
    CN_UINT     PDU_sequence_number;
    union
    {   /* PDU */
        MR_Request  request;
        MR_Response response;
    }   rr;
}   EncapsulationUnitData;

/********************************************************
 * Utility types for this driver
 ********************************************************/

/* Parameters used to identify a Forward_Open connection */
typedef struct
{
    /* Have to be set for make_CM_Forward_Open */
    CN_USINT   priority_and_tick;
    CN_USINT   connection_timeout_ticks;
    CN_UDINT   O2T_CID;
    CN_UDINT   T2O_CID;
    CN_UINT    connection_serial;
    CN_UINT    vendor_ID;
    CN_UDINT   originator_serial;
}   EIPConnectionParameters;

/* Some values taken from the Itentity object
 * of the target
 */
typedef struct
{
    CN_UINT vendor;
    CN_UINT device_type;
    CN_UINT revision;
    CN_UDINT serial_number;
    /* Table 10.3 in spec limits string to 32 chars,
     * but CompactLogix seems to return 52 characters.
     */
   CN_USINT name[100];
} EIPIdentityInfo;

/* Parameters & buffers for one EtherNet/IP connection.
 * sock == 0 is used to detect unused/shutdown connections. */
typedef struct
{
    EIP_SOCKET              sock;       /* silk or nylon */
    int                     slot;       /* PLC's slot on backplane */
    size_t                  transfer_buffer_limit; /* PLC limit */
    size_t                  millisec_timeout; /* .. for socket calls */
    CN_UDINT                session;    /* session ID, generated by target */
    CN_USINT                *buffer;    /* buffer for read/write, EIP_BUFFER_SIZE */
    EIPIdentityInfo         info;
    EIPConnectionParameters params;
}   EIPConnection;

#ifdef _WIN32
#pragma pack(pop)
#endif

CN_USINT *EIP_make_SendRRData(EIPConnection *c, size_t length);

/* Unpack reponse to SendRRData.
 * Fills data with details, returns pointer to raw MRResponse
 * that's enclosed in the RRData
 */
const CN_USINT *EIP_unpack_RRData(const CN_USINT *response,
                                  EncapsulationRRData *data);

void EIP_dump_connection(const EIPConnection *c);

/** Allocate EIPConnection */
EIPConnection *EIP_init();

/** Dispose EIPConnection */
void EIP_dispose(EIPConnection *c);

/** Connect to PLC */
eip_bool EIP_startup(EIPConnection *c,
                 const char *ip_addr, unsigned short port,
                 int slot,
                 size_t millisec_timeout);

/** Disconnect from PLC */
void EIP_shutdown(EIPConnection *c);

/** Send content of buffer.
 *  Buffer must contain valid EncapsulationHeader
 *  which describes the size of the message.
 *  @return true when OK
 */
eip_bool EIP_send_connection_buffer(EIPConnection *c);

/** Read message from PLC into connection's buffer:
 *  EncapsulationHeader, which describes the length of
 *  the message, followed by actual message.
 *  @return true when a full reply was received
 */
eip_bool EIP_read_connection_buffer(EIPConnection *c);

/* VxWorks has no strdup */
char *EIP_strdup(const char *text);

/* Like EIP_strdup, but only copies the first 'len' chars */
char *EIP_strdup_n(const char *text, size_t len);

/* Read a single tag in a single CIP_ReadData request,
 * report data & data_length
 * as well as sizes of CIP_ReadData request/response
 */
const CN_USINT *EIP_read_tag(EIPConnection *c,
                             const ParsedTag *tag, size_t elements,
                             size_t *data_size,
                             size_t *request_size, size_t *response_size);

eip_bool EIP_write_tag(EIPConnection *c, const ParsedTag *tag,
                   CIP_Type type, size_t elements, CN_USINT *data,
                   size_t *request_size,
                   size_t *response_size);

void *EIP_Get_Attribute_Single(EIPConnection *c,
                               CN_Classes cls, CN_USINT instance,
                               CN_USINT attr, size_t *len);

/* EOF ether_ip.h */
