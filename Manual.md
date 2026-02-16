"EtherIP" driver/device support module for EPICS
================================================

This module, found at https://github.com/EPICSTools/ether_ip,
allows EPICS IOCs to interface with Allen Bradley PLCs (see www.ab.com) via Ethernet.

It supports

 * ControlLogix 5000,
   both original versions with separate controller and ENET module,
   and L8x series that includes a network port in the controller.
 * Compact Logix devices

For details on the underlying protocol, see
"Interfacing the ControlLogix PLC Over EtherNet/IP",
 K.U. Kasemir, L.R. Dalesio
 ICALEPCS PSN THAP020
 LANL E-Print Archive: http://arXiv.org/abs/cs.NI/0110065

EtherNet/IP
-----------

EtherNet/IP, originally called "ControlNet over Ethernet"
as defined in the ControlNet Spec, Errata 2, is the protocol
used by Allen-Bradley ControlLogix PLCs.

This software is both a command-line test tool
and a driver/device module for EPICS IOCs.

Compilation
-----------

The software is supposed to compile like a normal EPICS module.

 1. Create a file ../RELEASE.local that contains a line `EPICS_BASE=/path/to/your/epics/base`
 2. Define `EPICS_HOST_ARCH` as usual for EPICS modules
 3. Run `make`


Command-line tool
-----------------

The executable `ether_ip_test`
allows for simple communication checks.
Invoke with `-?` to list options:
      
    ether_ip_test -h
    Usage:
        eipIoc st.cmd
    or
        eipIoc -v verbosity -p Plc1=IP,slot [-m macro=value] -d database.db [-d another.db]

    In the first case, this program acts as an ordinary IOC, executing a startup file

    The second invocation is for a command-line mode similar to the 'softIoc' from EPICS base,
    extended with options to communicate via etherIp.
    [ky9@ics-opi-remote1 ~]$ ether_ip/bin/linux-x86_64/ether_ip_test 
    Usage: ether_ip/bin/linux-x86_64/ether_ip_test <Options> [tag]
    Options:
    -l                                 List tags on PLC
    -v verbosity                       Set verbosity 1-10
    -i ip                              PLC IP as 123.456.789.001 or DNS name
    -p port                            Select non-default PLC TCP port
    -s PLC slot in ControlLogix crate  Default: 0
    -t timeout                         .. in ms
    -a array size                      To read array elements
    -w <double value to write>         Write tag (default: read)
    -W <64 bit value to write>         .. with larger data type
    -T times-to-do-all-this            Default: 1

      

Example: Read tag "REAL" from plc with IP 128.165.160.146,
PLC happens to be in slot 6 of the ControlLogix crate:

    ether_ip_test -i 128.165.160.146 -s 6 REAL
    Tag REAL
    REAL 0.002502

Add the `-v 10` option to see a dump of all the exchanged
EtherIP messages. The included error messages might help
to detect why some tag cannot be read.


"eipIoc"
--------

The executable `eipIoc` is a soft IOC similar to the
`softioc` provided by EPICS base that includes the `ether_ip` module:

    Usage:
       eipIoc st.cmd
    or
       eipIoc -p Plc1=IP,slot [-m macro=value] -d database.db [-d another.db]

In the first case, this program acts as an ordinary IOC, executing a startup file.

The second invocation is for a command-line mode similar to the 'softIoc' from EPICS base,
extended with options to communicate via etherIp.

If your IOC will simply need to run one or more `*.db` files that use the `ether_ip`
module, you can do that with the `eipIoc`.


Add "ether_ip" to your IOC
---------------------------

If you need to add the `ether_ip` module to an IOC which might also include `autosave`
and other EPICS modules of your choice,
add the `ether_ip.dbd` DBD file and the
`ether_ip` library to your IOC application.

This is typically done by adding this to your `RELEASE.local` file:
   
    ETHER_IP=/path/to/ether_ip
      
and this to your application `Makefile`:
   
    myapp_DBD += ether_ip.dbd
    myapp_LIBS += ether_ip


Network Settings
----------------

Since the driver uses TCP/IP, the route to the PLC has to defined.
For soft IOCs, there is typically nothing to do, but for vxWorks
you might want to add network information to your startup file:

    # Define the DNS name for the PLC, so we can it instead of the
    # raw IP address
    hostAdd "snsplc1", "128.165.160.146"

    # *IF* "128.165.160.146" is in a different subnet
    # that the IOC cannot get to directly, define
    # a route table entry. In this example, ..254 is the gateway
    routeAdd "128.165.160.146", "128.165.160.254"

    # Test: See if the IOC can get to "snsplc1":
    ping "snsplc1", 5


Driver Configuration in Startup File
------------------------------------

Before calling `iocInit` in your IOC startup file, the driver has to
be configured. 
Note that the IP address (128.165.160.146), the DNS name (snsplc1)
and the name that the driver uses (plc1) are all related but different!
    
    # Initialize EtherIP driver, define PLCs
    # -------------------------------------
    drvEtherIP_init

    # drvEtherIP_define_PLC <name>, <ip_addr>, <slot>
    # The driver/device uses the <name> to indentify the PLC.
    # 
    # <ip_addr> can be an IP address in dot-notation
    # or a name that the IOC knows about (defined via hostAdd).
    # The IP address gets us to the ENET interface.
    # To get to the PLC itself, we need the slot that
    # it resides in. The first, left-most slot in the
    # ControlLogix crate is slot 0.
    # (When omitting the slot number, the default is also 0)
    drvEtherIP_define_PLC "plc1", "snsplc1", 0
       
    # EtherIP driver verbosity, 0=silent, up to 10:
    # (on older vxWorks, use the syntax
    #    EIP_verbosity=4
    # )
    EIP_verbosity(4)
    
    # then load *.db files, and finally call iocInit




EPICS Record Guidelines
-----------------------

The EtherIP driver was designed to optimize tag transfers.
When multiple records are attached to the same tag, the
driver will transfer the tag only once, using the highest scan rate of
all attached records. When records refer to elements of an array,
the driver will transfer the array as a whole, from element 0
to the highest referenced array element.
Tags are arranged according to PLC and scan rate. In order to not disturb
processing of the EPICS database, the driver has separate task per
PLC to handle the network traffic between that PLC and the IOC. 

You should try to benefit from the driver optimization by arranging
tags in arrays. You can have alias tags on the PLC, so that a
meaningful alias like `InputFlow` is used in the ladder logic while
the data is held in an array element like `xfer[5]` which the EPICS
record can use for the network transfer.
Arrays should be one-directional: Use separate "EPICS to PLC" and "PLC
to EPICS" arrays. Because of PLC buffer limitations, the array size is
unfortunately limited to about `BOOL[350]` and `REAL[40]`. While you can
define bigger arrays, those cannot be transferred over the network
with EtherIP. Consequently you might end up with several transfer arrays.

You should also understand that the network transfer can be delayed or
even fail because of network problems. Consequently you must not
depend on "output" records to write to the PLC within milliseconds. If
e.g. an output on the PLC has to be "on" for a certain amount of time,
have the PLC ladder logic implement this critical timing. The EPICS
record can then write to a "start" tag on the PLC, the PLC handles the
exact timing in response to the command. When done, the PLC signals
success or failure via another "status" tag.
This way, network delays in the transfer of "start" and "status" tags
will not impact the critical timing.
                
EPICS records: Generic fields
-----------------------------

`DTYP`: Device type

Has to be "EtherIP" as defined in DBD file:
  
    field(DTYP, "EtherIP")

`SCAN`: Scan Field

The driver has to know how often it should communicate
with the PLC. Per default, it uses the SCAN field
of the record:
   
    field(SCAN, "1 second")
    field(SCAN, ".1 second")
    field(SCAN, "10 second")
    ...

The driver scans the PLC at the same rate.
The record simply reads the most recent value.
The scan tasks of the driver and the EPICS database
are not synchronized, so when the driver scans the PLC once per second,
and the record is also processed every second, you might see data
that is up to 2 seconds old.


When *multiple* records refer to the *same tag*, the driver
will scan that tag at the highest scan rate.
Given these example records, the driver will scan the tag "fred" at 10 Hz.


    record(ai, "A")
    {
      field(INP, "@plc1 fred")
      field(SCAN, "1 second")
      ...
    }
    record(ai, "B")
    {
      field(INP, "@plc1 fred")
      field(SCAN, ".1 second")
      ...
    }


This also applies to arrays.
Since requests to array elements `my_array[0]`, `my_array[2]`,
`my_array[5]` etc. are combined into a SINGLE transfer of the
tag `my_array[0 to 5]`, the rate of that transfer is the fastest rate
requested for any of the array elements.
(Unless you request single element requests with the 'E'
flag which you should try to avoid).

**SCAN: Passive**

Output records are often passive.
They are only processed when the record is accessed via ChannelAccess
from an operator screen where someone entered a new value for this
record.

While the driver will only *write* to a tag when the record is
processed, it will still try to *read* the tag from the PLC in case it is
changed from another source (another IOC, PanelView, ...).
The section "Keeping things synchronized" gives details on this.
Since the driver cannot extract an update rate from the SCAN field
when it is set to "Passive", the "S" scan flag has to be used as
described in the INP/OUT link section.

**SCAN: I/O Intr**
Input records can be configured to use

    field(SCAN, "I/O Intr")

The driver causes the record to be processed as soon as a new value is
received. As in the Passive case, the driver needs the "S" scan
flag to determine the poll rate.

Record Input/Output Links
-------------------------

The `INP` field for input records respectively the `OUT` field for output records
has to match

    field(INP, "@<plc name> <tag> [flags]")
    field(OUT, "@<plc name> <tag> [flags]")

`<plc name>` is the driver's name for the PLC, defined in the IOC
startup script via

     drvEtherIP_define_PLC <name>, <ip_addr>, <slot>

Example:

     drvEtherIP_define_PLC "plc1", "dns-name-of-plc.site.org", 0


`<tag>` can be a single tag "fred" that is defined in the "Controller
Tags" section of the PLC ladder logic. It can also be an array tag
"my_array[5]" as well as a structure element "Local:2:I.Ch0Data".

Array elements are indexed beginning with 0. 
You can use decimals (2, 10, 15), hex numbers (0x0f) and
octal numbers (04, 07, 12).
Mind you this means 08 is invalid because
it is interpreted as an octal number!

The `<tag>` has to be a single elementary item (scalar tag, array
element, structure element), not a whole array or structure.

Common `<flags>` are `S` and `E`.
Record-specific flags that will be explained
later when detailing the support by record type.

**"S <scan period>" - Scan flag**

If the SCAN field does not specify a scan rate as in the case of
"Passive" output records or input records with SCAN="I/O Intr",
the S flag has to be used to inform the driver of the requested update
rate.

Note that the behavior of the scan flag is only defined for these cases:

Record Type  | SCAN
-----------  | ----
AI           | I/O Intr
BI             | I/O Intr
MBBI         | I/O Intr
MBBIDirect     | I/O Intr
AO             | Passive
BO             | Passive
MBBO         | Passive
MBBODirect     | Passive

In all other cases, the S flag should not be used, instead the
SCAN field must provide the needed period (e.g. SCAN=".5 second").  

The time format is in seconds, like the SCAN field, but without "seconds".
There has to be a space after the "S"!
    
    field(INP, "@snsioc1 temp S .1")
    field(INP, "@myplc xyz S 0.5")

If the record has neither a periodic SCAN rate nor an S flag in
the link field, you will get an error message similar to

    devEtherIP (Test_HPRF:Amp_Out:Pwr1_H):
    cannot decode SCAN field, no scan flag given
    Device support will use the default of 1 secs,
    please complete the record config

In the IOC startup file, you can define a default rate:

    drvEtherIP_default_rate(1.0)

If you do that, the warning will vanish.
The recommended practice, however, is to provide a per-record
"S" flag because then you can recollect the full configuration
from the record and avoid ambiguities.


**"E" - Flag to force elementary transfer**

If the tag refers to an array element,

    field(INP, "@snsioc1 arraytag[5]")

the driver will combine all array requests into a single array
transfer for this tag. This is meant to reduce network traffic:
Records scanning arraytag[0], ... arraytag[5] will result in a single
"arraytag" transfer for elements 0 to 5.

The "E" flag overrides this:

    field(INP, "@snsioc1 arraytag[5] E")

will result into an individual transfer of "arraytag" element 5,
not combined with other array elements.

Reasons for doing this:

* The software can only transfer array elements 0 to N, always
  beginning at 0. If you need array element 100 and only this element,
  so there is no point reading the array from 0 to 100.
* You want array elements 401, 402, ... 410. It's not possible
  for the driver to read 401-410 only, it has to read 0-410. This,
  however, might be impossible because the send/receive buffers of the
  PLC can only hold about 512 bytes. So in this case you have to read
  elements 401-410 one by one with the "E" flag.
* Binary record types (bi, bo, mbbi, ...) with a non-BOOL array
  element. See the binary record details below.

Unless you absolutely have to use the "E" flag for these reasons,
don't use it.
It is no problem to have one "BOOL[352]" tag for IOC->PLC
communication and another "BOOL[352]" array for PLC->IOC
communication, both at 10Hz. The result is a low and constant
network load, the transfers are almost predictable even though
Ethernet is not deterministic. If instead you use several "E"
flags, each of those tags ends up being a separate transfer,
leading to more network load and possible collisions and delays.


ai, Analog Input Record
-----------------------

By default the tag itself is read:

PLC Tag type    | Action
------------    | ---------------------------------------------------
REAL            | VAL field is set (no conversion).
INT, DINT, BOOL | RVAL is set, conversions (linear, ...) can be used.

The analog record cannot be used with BOOL array elements.
Elements of other numeric array types (REAL, INT, ...) are allowed.

**Statistics Flags**

The driver holds statistics for each tag and each scan list,
accessible via the `drvEtherIP_report 10` command in the
IOC console.

In addition, most of this information is also available to analog
input records by using flags in the INP link.
Note that a valid tag is *always* required. For `TAG_TRANSFER_TIME`
this makes sense because you query per-tag information.
In other cases it's used to find the internal scan list.

    # of timeouts/errors in communication with PLC [count]
    field(INP, "@$(PLC) $(TAG) PLC_ERRORS")

    # times when scan task was slow [count]
    field(INP, "@$(PLC) $(TAG) PLC_TASK_SLOW")

    Seconds since 1990 when tag's list was checked.
    Useful to monitor if the driver is still running.
    field(INP, "@$(PLC) $(TAG) LIST_TICKS")
    field(INP, "@$(PLC) $(TAG) LIST_TIME")

    Time for handling scanlist [secs]: last, minimum, maximum
    field(INP, "@$(PLC) $(TAG) LIST_SCAN_TIME"),
    field(INP, "@$(PLC) $(TAG) LIST_MIN_SCAN_TIME"),
    field(INP, "@$(PLC) $(TAG) LIST_MAX_SCAN_TIME"),

    Time for last round-trip data request for this tag
    field(INP, "@$(PLC) $(TAG) TAG_TRANSFER_TIME")

At least on vxWorks, the `PLC_TASK_SLOW` flag is of less use than anticipated.
It's incremented when the scan task is done processing the list and then
notices that it's already time to process the list again. The scheduling is rather 
coarse. With all the other task scheduling going on and ethernet delays, 
`PLC_TASK_SLOW` might increment every once in a while without a
noticeable impact on the data (no time-outs, no old data).

ao, Analog Output Record
------------------------

As with analog input records, tags of type REAL, INT, DINT, BOOL are supported as
well as REAL, INT, DINT arrays (no BOOL arrays). No statistics flags
are supported.

For REAL tags, the VAL field of the record is written to the tag.
Otherwise, the RVAL field is used and you can benefit from
the AO record's conversions VAL <-> RVAL.

If the SCAN field is "Passive", the "S" flag has to be used.

**Keeping things synchronized**
The problem is that the EPICS IOC does not "own" the PLC. Someone else
might write to the PLC's tag (RSLogix, PanelView, another IOC,
command-line program). The PLC can also be rebooted independent from
the IOC. Therefore the writing records cannot just write once they have
a new value, they have to reflect the actual value on the PLC.

In order to learn about changes to the PLC from other sources, the
driver scans (reads) write tags just like read tags, so it always knows the
current value. When the record is processed, it checks if the value to
be written is different from what the PLC has. If so, it puts its RVAL
into the driver's table and marks it for update,
so the driver then writes the new value to the PLC.

So in the case of output records the driver will still read from the PLC
periodically and only switch to write mode once after an output record
has been processed and provided a new value.

Some glue code in the device is called for every value that the driver
receives. It checks if this still matches the record's value. If not, the
record's RVAL is updated and the record is processed. A user interface
tool that looks at the record sees the actual value of the PLC.
The record will not write the value that it just received because
it can see that RVAL matches what the driver has.

This fails if two output records are connected to the same tag,
especially if one is a binary output that tries to write 0 or 1. In
that case the two records each try to write "their" value into the
tag, which is likely to make the value fluctuate.

Another side effect is that when processing an output record,
that record will not write immediately. The writing is handled
by a separate thread in the driver. The next time the tag is scanned,
the driver thread will notice the "update" flag and write to the PLC.
Consequently you adjust the write latency when you specify the scan
rate of the driver thread.

**Output records and arrays**
When using *input* records that reference array tags a[0], a[1],
a[9], the driver will read the whole referenced part of the array,
that is a[0...9]. While the array might have more elements, the driver
reads elements from zero up to the highest element referenced by a
record.

Likewise, when output records reference those array tags,
the whole section of the array from 0 to the highest element
referenced by a record gets written.
When no output record requested a 'write', it is read.

This is perfect for e.g. limit settings:
Most of the time, they are unchanged and the driver efficiently
monitors them. Should an operator change one of the limits on the IOC,
the whole array is written. Should the operator change a limit via
PanelView, the driver on the IOC notices the change and updates
the output record for this array entry.

There are problems when frequently processed records are combined in
such a bi-directional array tag.

Example: A heartbeat record, processed every second, is part of an
'output' array. Every second, that record marks the whole array(!) for
'write'.
If an operator now changes another array element on the IOC, that gets
written, too. But when the operator changes a value on the PLC via
PanelView, that change is very likely to be lost because the driver
doesn't get around to 'read' the tag since the heartbeat record causes
it to 'write' all the time. Consequently, most tag changes from
PanelView are almost immediately overwritten by the IOC's value.

Conclusion:
It's impossible to have truly 100% bi-directional communication.
If both the record and the tag on the PLC change, one may overrule
the other depending on timing (scanning, network).

Next Best Solution:
Bi-directional use of arrays for e.g. limits work well enough
if they are infrequently changed from either side.
Records that are frequently written should not be combined in such
arrays. If they happen to be in the same array, use the 'E' flag
in the OUT link of e.g. the heartbeat record. That way, the heartbeat
record will only write that single array element and not trigger a
write of the whole referenced subsection of the array.
One could conclude to add 'E' to every output record, but then you
loose all the possible array-transfer optimization.


**"FORCE" Flag**
Whenever an output record is processed, it will
update the driver's copy of a tag and mark it for "write".
The next time the driver processes the scan list which
contains the tag, it will write the tag to the PLC.

When the record is not processed, and therefore the tag
is not marked for write, the driver will read the
tag from the PLC.
What happens when the value of the tag differs from
the value of the record?

Per default, the record is updated to reflect the value of the
tag. This way, both the IOC and e.g. a PanelView display can change
the same PLC tag. Changes from "one" source are reflected on the
respective "other" side.
With TPRO set on the record, it looks like this:

    'Test_HPRF:Fil1:WrmRmp_Set': got 8 from driver
    'Test_HPRF:Fil1:WrmRmp_Set': updated record's value 8  

The "FORCE" flag will change this behavior.
When the driver notices a discrepancy, it will NOT
change the record but simply re-process it.
This causes the IOC to write to the tag on the PLC
again and again until the tag on the PLC matches
the value of the record. The record tries to "force"
its value into the tag.
With TPRO, it looks like this:

    'Test_HPRF:Xmtr1:FilOff_Cmd': got 0 from driver
    'Test_HPRF:Xmtr1:FilOff_Cmd': will re-write record's value 1

**Arrays**
When writing array tags, a single ao record (or bo, mbbo, ...)
is connected to a single element of the array.
When the record has a new value, it will update that array
element and mark the array as "please write to PLC during the
next scan cycle of the driver".
This is desirable because it allows several output records to
specify new values and then the WHOLE ARRAY is written as one unit.

Writing the values that didn't change doesn't matter because
the transfer time for a single tag and an array is almost
the same. Transferring an array where many items didn't change
is not costly, transferring two separate tags that did change
would take longer.
The PLC also doesn't seem to care if tags are written. There is no
"tag was written" event in the PLC that I know of.
Writing the same value again does not upset the ladder logic.

It is still important to NOT MIX DIRECTIONS within an array.
Do use arrays instead of single tags to speed up the transfer,
but keep different "EPICS to PLC" and "PLC to EPICS" arrays.
If you need handshake tags (EPICS writes, PLC uses
it and then PLC resets the tag), those bidirectional tags
should not be in arrays. They have to be standalone, scalar tags.


bi, Binary Input Record
-----------------------

Reads a single bit from a tag.

PLC Tag type | Action
------------ |    ---------------------------------------------------
BOOL         |    VAL field is set to the BOOL value
other        |    converted into UDINT, then bit 0 is read

BOOL Arrays can be used:

    field(INP, "@plc1 BOOLs[52]")

will read the 52nd element of the BOOL array.

INT, DINT arrays are treated as bit arrays:

    field(INP, "@plc1 DINTs[40]")

will **NOT** read array element #40 but bit #40 which is bit # 8 in the
second DINT.

If you want to read the first bit of DINT #40, the "E" flag can be
used to make an elementary request for "DINTs[40]". The preferred solution,
though, is the Bit flag.
The TPRO field (see the section on debugging) is often helpful in
analyzing what array element and what bit is used.

**"B <bit>": Bit flag*

    field(INP, "@plc1 DINTs[1] B 8")

will read bit #8 in the second DINT array element.


The same write caveats as explained for the ao record apply,
i.e. do use separate arrays for writing to the PLC and reading from the PLC.


mbbi, mbbiDirect Multi-bit Binary Input Records
-----------------------------------------------

These records read multiple consecutive bits, the count is given in
the number-of-bits field:

    field(NOBT, "3")

The input specification follows the bi description,
except that the addressed bit is the first bit.

When using array elements, the same bit-addressing applies. As a
result, the "B <lit>" flag should be used for non-BOOL arrays.

The mbbiX records can read across array elements of DINT arrays.
This record reads element 4, bit 31 and element 5, bit 1:

    field(INP, "@$(PLC) DINTs[4] B 31")
    field(NOBT, "2")
    
But this feature is merely a side effect, it's safer to read
within one INT/DINT, or use BOOL arrays.


bo, mbbo, mbboDirect Binary Output Records
------------------------------------------

The output records use the same OUT configurations as the
corresponding input records.

If the SCAN field is "Passive", the "S" flag has to be used.

Note that if several records read and write different elements of an
array tag X, that tag is read once per cycle from element 0 up to the
highest element index N that any record refers to. If any output record
modifies an entry, the driver will write the array (0..N) in the next
cycle since it is marked as changed.

As a result, it is advisable to keep "read" and "write" arrays
separate, because otherwise elements meant for "read" will be written
whenever one or more other elements are changed by output records.


stringin and lsi String Input Records
-------------------------------------

String input records can be connected to STRING tags
on the PLC:

    field(DTYP, "EtherIP")
    field(INP,  "@$(PLC) text_tag")
    field(SCAN, "1 second")

STRING tags on the PLC default to an allowed length of up to 82
characters. The stringin record is limited to 40 characters.
Since we include the '\0' terminator, any STRING tag gets
truncated to 39 characters. There is no fault indication for this,
just a truncated string.

The lsi (long string input) record can hold any length, chosen by
the value in the SIZV (size of VAL) field at record initialization.
All strings stored by this record must include a '\0' terminator.
As with the stringin record, if the record's field length is too
short for the data, the data will be silently truncated.

These records works only with STRING tags.
Any other tag type will result in errors.
Likewise, only stringin or lsi records can be used with STRING tags.
Any other record type will fail with STRING tags.

Note that we did not find PLC documentation for the STRING tag data type.
To the driver, a STRING tag looks like a "CIP structure" and the
location of the string length and character data in there were
determined from tests.
It appears to be an auto-generated custom data type, i.e. in principle
you can delete it from your Ladder Logic code or re-define it with a different layout.

stringout and lso String Output Records
---------------------------------------

String output records can be connected to STRING tags
on the PLC:

    field(DTYP, "EtherIP")
    field(INP,  "@$(PLC) text_tag")
    field(SCAN, "1 second")

The stringout record is limited to 40 characters.
The lso (long string output) record can hold any length, chosen
by the SIZV (size of VAL) field at record initialization time.

The stringout and lso records work only with STRING tags.
Any other tag type will result in errors.
Likewise, only stringout records must be used with STRING tags.
Any other record type will fail with STRING tags.


waveform Array Input Records
----------------------------

Waveform records can be connected to REAL or DINT array tags
on the PLC:

    field(DTYP, "EtherIP")
    field(SCAN, "1 second")
    field(INP,  "@$(PLC) array_tag")
    field(NELM, "40")
    field(FTVL, "DOUBLE")

or

    field(FTVL, "LONG")

On the PLC, "array_tag" could be

      fred = REAL[40]

or   

      fred = DINT[80]

When specifying the array tag in INP, do not use
'fred[0]' or 'fred[any other number]', use only 'fred'.
The NELM field defines the number of elements read from the tag.
The record will read fred[0] ... fred[NELM-1].

For REAL[] array tags, FTVL must be DOUBLE.
For DINT[] array tags, FTVL must be LONG.
That way, the data type sizes match and no conversion
is necessary.
For other array tags, FTVL==LONG might work
but is not guaranteed to work.




Debugging
---------
The driver can display information via the usual EPICS dbior call
on the IOC console (or a telnet connection to the IOC):

    dbior "drvEtherIP", 10

A direct call to

    drvEtherIP_report 10

yields the same result. Instead of 10, lower verbosity levels are
allowed.

`drvEtherIP_help` shows all user-callable driver routines:

    drvEtherIP_help
    drvEtherIP V3.7 diagnostics routines:
    EIP_verbosity(0-10)
    -  define logging detail, currently set to 4
    -  10: Dump all protocol details
        9: Hexdump each sent/received buffer
        6: show driver details
        5: show write-related operations
        4: DEFAULT: show basic startup plus error messages
        2: show more error info
        1: show severe error messages
        0: keep quiet
    EIP_timeout(<milliseconds>)
    -  define the default timeout for connecting to PLC and reading responses
       (default: 5000 ms)
    drvEtherIP_default_rate(<seconds>)
    -  define the default scan rate
       (if neither SCAN nor INP/OUT provide one)
    EIP_buffer_limit(<bytes>)
    -  Set buffer limit enforced by driver.
       Currently 480, default: 480
       The actual PLC limit is unknown, it might depend on the PLC or ENET model.
       Can only be set before driver starts up.
    drvEtherIP_define_PLC(<name>, <ip_addr>, <slot>)
    -  define a PLC name (used by EPICS records) as IP
       (DNS name or dot-notation) and slot (0...)
    drvEtherIP_read_tag(<ip>, <slot>, <tag>, <elm.>, <timeout>)
    -  call to test a round-trip single tag read
       ip: IP address (numbers or name known by IOC
       slot: Slot of the PLC controller (not ENET). 0, 1, ...
       timeout: milliseconds
    drvEtherIP_report(<level>)
    -  level = 0..10
    drvEtherIP_dump
    -  dump all tags and values; short version of ..._report
    drvEtherIP_list
    -  list all tags that the PLC publishes
    drvEtherIP_reset_statistics
    -  reset error counts and min/max scan times
    drvEtherIP_restart
    -  in case of communication errors, driver will restart,
       so calling this one directly shouldn't be necessary
       but is possible


A common problem is that a record does not seem to read/write the PLC tag
to which it was supposed to be connected.
When setting "TPRO" for a record, EPICS will log a message whenever a
record is processed. The EtherIP device support shows some additional
info on how it interpreted the INP/OUT link. Use a display manager, a
command line channel access tool or

    dbpf "record.TPRO", "1"

in the IOC shell to set TPRO. Set TPRO to "0" to switch this off again.

Example output for a binary input that addresses "DINTs[40]":

    process:   snsioc4:biDINTs40
     link_text  : 'plc1 DINTs[40]'
     PLC_name   : 'plc1'
     string_tag : 'DINTs'
     element    : 1          <- element 1!
     mask       : 0x100      <- mask selects bit 8!

As you see, the BI record is reading bit #8
in DINT[1], that's bit #40 when counting from the
beginning of the DINT array.
If that's what you wanted, OK.
If you entered "DINTs[40]" because you wanted bit #0
in array element 40, you should have used "DINTs[40] B 0"
(See the description of the bi record and the "B" flag)

Checklist
---------

1. Set the record's TPRO to "1".
   Does the record get processed when you want it to be processed?
   Does the link_text make sense?
   Is it parsed correctly, i.e. is the PLC_name what you
   meant to use for a PLC name?
   Does the combination of string_tag, element & mask make sense?
    
2. Call "drvEtherIP_report 10", locate the information for the tag that the record uses.
   If the "...._size" fields in there are zero, the driver
   could not learn anything about the tag.
   See if the tag actually exists on the PLC (next step).

3. Use the test tool, e.g. try `ether_ip_test -i 12.3.45.67 MyTag[12]`
   to see if you can get to the PLC and read the tag.

4. Note that array requests are combined.
   Assume that we are debugging a record
   that accesses tag FRED[7]. drvEtherIP_report might show
   that the driver is actually trying to access 10 elements
   for tag FRED. That means that some other record must
   try to get FRED[9], so altogether the driver reaches
   for FRED[0]...FRED[9] -> 10 elements.
   Assert that there are at least 10 elements for the tag FRED
   on the PLC!

5. Increase `EIP_verbosity` to see which requests the driver
   sends and what reply it receives.
   You might have to do this with a database reduced to just
   the troublesome record, because otherwise you get too much
   information.


Driver Operation Details
========================

These example records..

    "fred", 10 seconds
    "freddy", 10 seconds
    "jane", 10 seconds
    "analogs.temp[2].input", 5 seconds
    "binaries[3] E", 1 Hz
    "binaries", element 1, 10Hz
    "binaries", element 5, 10Hz
    "binaries", element 10, 10Hz

will result in the following scanlist entries:

    10  Hz: "binaries", elements 0-10
     1  Hz: "binaries[3]"
     0.5Hz: "analogs.temp[2].input"
     0.1Hz: "fred", "freddy", "jane"

The driver creates one thread and socket per PLC for communication.
The scan task runs over the scanlists for its PLC.

    For each scanlist:
       Figure out how many requests can be combined
       into one request/response round-trip
       (~480 byte limit), record in TagInfo.

The driver simply adds requests from the current scanlist
until the buffer limit is reached. The remaining tags are
placed in another transfer. The driver does not try every possible
combination of tags from the current scanlist to find the optimal
combination to reduce the number of transfers.
It does not combine tags from e.g. the 10 second scanlist
with tags from the 1 second scanlist every 10th turn.

PLC Buffer Limit
----------------

See ether_ip.h for details on the limit which is about 480 bytes.

The driver can only combine read/write requests into one multi-request
until either the combined request or the expected response reaches a
buffer limit. In practice, this means:

When reading many INT tags, each with a 4-character tag name,
32 read commands can be combined until hitting the request-size limit.
The response of 32 * 2 bytes (INT) plus some protocol overhead is much
smaller than the request.

When reading many REAL tags, each with a 1-character tag name, 39 read
commands combine into one request. Both the request and the response
are close to the limit.

When reading elements of a REAL array tag, 120 array elements can be read.
The request contains the single array tag, asking for 111 elements,
the response reaches the transfer buffer limit. Similarly, INT arrays
can use up to 240 element.

The guideline of "limit arrays to 40 elements" allow the driver a lot
of flexibility: It can combine three REAL[40] requests into one
transfer or add several single-tag requests with 2 x INT[40] requests etc.

CIP data details:
Analog array REALs[40], read "REALs", 2 elements
-> REALs[0], REALs[1]

Binary array BOOLs[352], read "BOOLs", 1 element
-> 32bit DINT with bits  0..31

Access to binaries is translated inside the driver.
Assume access to "fred[5]":
For analog records, a request to the 5th array element is assumed.
For binary records, we assume that the 5th _bit_ should be addressed.
Therefore the first element (bits 0-31) is read and the single
bit number 5 in there returned.

Message '<channel xxx> already writing'
---------------------------------------

This message is a result of how the device & driver support writes
to the PLC.
Remember that even _output_ records are periodically _read_ by the
driver, and in case the value of the tag on the PLC differs from
what's in the record, the record gets updated & processed.
Most of the time, the tag is thus read, the result matches what's
in the record, and nothing else happens.

When on the other hand an output record is updated via ChannelAccess
or database processing, the device support for this record type
deposits the new value to be written in the driver's tag table
(the entry for that tag or element of an array tag), and marks the tag
to be written.
The next time around in the driver scan task, the driver recognizes that
the tag should be written instead of read, and writes the tag to the PLC,
and resets the 'please write' flag, so the next time around, we're back
to reading the tag.

If you have various records all associated with elements of an array tag,
and these records get processed at about the same time, the following can happen:

1. Record A processes, updates array element Na of the array tag,
   and marks the array to be written.
2. If now records B, C, ... process, updating array elements Nb, Nc, ...,
   (doesn't matter if all the Nx are different or not),
   the array tag has already been marked for writing, and if the EIP_verbosity
   is high enough, you get the 'already writing' message.

Most of the time, this is not a problem.

If the affected records process at about the same time, it's to be expected,
and you can simply set EIP_verbosity=5 or lower to hide the message.
If, on the other hand, you would have expected the driver to handle the
'write' between record processings, this would indicate a problem.

Example:
The one and only output record with OUT="@plc tagname S 5"
configures the driver to scan the 'tagname' every 5 seconds.
If you now process the record every second by e.g. entering
new value via ChannelAccess, you'll see about 4 'already writing' 
messages, because the driver will only write every 5 seconds.
But if you only process the record every 10 seconds, you should
see no message, because the last new value should have been written
by the time you enter a new value.


