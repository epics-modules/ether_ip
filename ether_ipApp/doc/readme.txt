-*- outline -*- $Id$

* Todo

** doc
** Check timeout choices in semTake calls
** Test on PPC
** make Unix/Win32 cmd tools more useful?

* EtherNet/IP

EtherNet/IP, originally called "ControlNet over Ethernet"
as defined in the ControlNet Spec, Errata 2, is the protocol
used by A/B ControlLogix PLCs.

This software is both a commandline test tool for Win32/Unix
and a driver/device for EPICS IOCs.

* Files

Makefile	 Unix (Linux) makefile
Makefile.Vx	 vxWorks makefile
ether_ip.dsp/dsw MS VC project files
ether_ip.[ch]	 EtherNet/IP protocol
main.c		 main for Unix/Win32
drvEtherIP*	 vxWorks driver
devEtherIP*      EPICS device support

Test Setup (dbd, IOC Database, startup-script, dm2k displays):
dbd/* 
db/*
dm2k/*
startups/*

* Supported Record types

** Analog input
   The analog input record can be connected to single analog tags,
   a single analog tag that is part of a structure as well
   as an array element.
   By default the tag itself is read. Additional input flags
   select several statistical pieces of information.
   When the tag on the PLC is of type CN_REAL, the VAL field is set
   directly (no conversion).
   Otherwise RVAL is set and conversions (linear, ...) can be used.

** Analog output
   Similar to analog input, no special flags are supported.

** Binary input, output, MBBI, MBBIDirect
   These expect to be connected to a BOOL or BOOL[].
   Note the comments on CIP data below.

* Installation, Setup for EPICS

  To use this software, several steps are required.
  Many of these are handled by the SNS ADE,
  but the following steps list the details in case
  you run into problems.
  1) Compile the driver with the ADE.

  2) The IOC's VxWorks startup file has to load the
     object files that were created for VxWorks in the initial step,
     either by themself or after combining
     all the needed object files into a library
     (this is what most EPICS ADEs do).

     The ether_ipApp creates a library "ether_ipLib"
     which contains only the driver code.

     The testether_ipApp combines that library with EPICS base
     objects into "testether_ip".
     The example in iocBoot/iocether_ip/st.cmd loads that library.

  3) To inform EPICS of this new driver/device,
     a DBD file like this one is used:
       include "base.dbd"
       include "ether_ip.dbd"
     Refer to the EPICS Application deveopers guide for details
     on DBD files and how to load them in the vxWorks startup-file
     with "dbLoadDatabase".

  4) Since the driver uses TCP/IP, the route to the PLC has to defined
     properly. In total, these steps include:

       # Define the DNS name for the PLC, so we can it instead of the
       # raw IP address
       hostAdd ("snsplc1", "128.165.160.146")  

       # *IF* "128.165.160.146" is in a different subnet
       # that the IOC cannot get to directly, define
       # a route table entry. In this example, ..254 is the gateway
       routeAdd ("128.165.160.146", "128.165.160.254")

       # Test: See if the IOC can get to "snsplc1":
       ping "snsplc1", 5

     The st.cmd example shows this.

  5) Driver configuration in the startup file:
     Before calling iocInit, the driver's PLC table has to be
     configured like this:
    
       # Initialize EtherIP driver, define PLCs
       # -------------------------------------
       drvEtherIP_init

       # drvEtherIP_define_PLC <name>, <ip_addr>
       # The driver/device use the <name>,
       # the <ip_addr> can be an IP address in dot-notation
       # or a name that the IOC knows about (defined via hostAdd,
       # see step 4)
       drvEtherIP_define_PLC "plc1", "snsplc1"
       
       # EtherIP driver verbosity, 0=silent, up to 10:
       EIP_verbosity=4

      Again the st.cmd example shows this.

   6) Driver Tests	
      Some of these work all the time, other require an actual
      EPICS database to be running with records that use
      the EtherIP driver/device.
   
       # List available commands (always available)
       drvEtherIP_help

       # Read Test: (always available)
       # drvEtherIP_read_tag <ip>, <tag>, <elements>, <ms_timeout>
       # 
       # This call uses the IP address or the name registered
       # with hostAdd, _not_ the PLC as defined in a call to
       # drvEtherIP_define_PLC
       drvEtherIP_read_tag "snsplc1", "my_tag", 1, 5000

       # Dump all tags that the driver is currently scanning
       # (requires EPICS database)
       # drvEtherIP_dump    

       # drvEtherIP_report <level>:
       # Dump various infos, also called by "dbior"
       # (requires EPICS database)
       drvEtherIP_report 5

* Record Configuration

** Device type
   Has to be "EtherIP" as defined in dbd file, example:

   field(DTYP, "EtherIP")

** Scan Field
   The driver has to know how often it should communicate
   with the PLC.

*** Periodic
   For periodically scanned records (e.g. SCAN="1 second")
   this is based on the SCAN field.
   The record then simply reads the most recent value.
   The scan tasks of the driver and the EPICS database
   are not synchronized.

*** I/O Intr
   For the case of SCAN="I/O Intr",
   the driver causes the record to be processed as soon
   as a new value is received.
   To determine the rate at which the driver communicates,
   the SCAN field can no longer be used
   because it does not specify a period as in the previous case.
   Instead, a scan flag has to be included in the link field
   like this:

   field(SCAN, "I/O Intr")
   field(INP, "@snsioc1 temp S .1")

   See below for more on the scan flag.

** Input/Output Link
   The input/output link (INP for Ai, Bi) has to match
    "@<plc name> <tag> [flags]".

   Example:

   field(INP, "@snsioc1 temp")
   field(INP, "@snsioc1 PVs.VAL[3]")
   field(INP, "@snsioc1 PVs.VAL[3] E")

   Options:
   a) The tag is a single elementary item, the type matches the record,
      i.e. REAL for AnalogInput, BOOL for BinaryInput.
      The tag might reference an element in a complicated
      structure of arrays of structure etc.,
      but the element it references in the end is a
      REAL, BOOL, .., no structure or array.
      In this case the record reads the single tag.
   b) The tag is an array of a matching elementary type,
      the array element to pick is specified.
      Then the driver reads the array
      (elements 0 up to the highest requested one)
      and the record picks the single element from that array.
   c) Tag is an array AND the element flag "E"
      is given.
      Then this record reads the single array element.
  
   For a single record, reading
      "arraytag[3]"
   is very similar to reading
      "arraytag[3] E"
   But when several records reference the "arraytag",
   in the latter case it'll be read once as a whole
   by the driver, instead of posting a seperate request
   per record.
   So there is rarely need to use the E flag because
   it will reduce performance.
   Exceptions:
   a) You need array element 100 and only this element,
      so there is no point reading the array from 0 to 100.
   b) You want array elements 401, 402, ... 410.
      It's not possible for the driver to read 401-410 only,
      it has to read 0-410. This, however, might be impossible
      because the send/receive buffers of the PLC can only
      hold about 512 bytes.
      So in this case you have to read elements 401-410
      one by one.

** Flags
   The "E" flag was already mentioned.

*** Scan Flag
   The time format is in seconds, like the SCAN field,
   but without "seconds".
   Examples:
   
   field(INP, "@snsioc1 temp S .1")
   field(INP, "@myplc xyz S 0.5")

   There has to be a space after the "S" to distinguish
   it from the other flags starting with "S".

*** Statistics Flags
   The driver holds statistics per Tag
   which can be accessed via the flag field.

   Allowed flags for the AI record:

   PLC_ERRORS      - # of timeouts/errors in communication with PLC [count]
   SLOW_SCANS      - # times when scan task was too slow [count]
   SCANLIST_ERRORS - Like PLC_ERRORS but per scanlist
   MIN_SCAN_TIME   - Tag's minimum scan time [secs]
   MAX_SCAN_TIME   - Tag's maximum scan time [secs]
   LAST_SCAN_TIME  - Tag's most recent scan time [secs]
   SCAN_TICKTIME   - vxWorks ticks when last scan occured
   AGE             - Time from last scan until Db processing [secs]

   When using the flags, a tag is always required.
   For e.g. "MIN_SCAN_TIME" this makes sense because
   you query per-tag information.
   In other cases it's used to find the scanlist.

   Examples:

   field(INP, "@snsioc1 PVs.VAL MIN_SCAN_TIME")
   field(INP, "@snsioc1 PVs.VAL MAX_SCAN_TIME")
   field(INP, "@snsioc1 PVs.VAL LAST_SCAN_TIME")


* Driver Operation

Example:
** Records
   "fred", 10 seconds
   "freddy", 10 seconds
   "jane", 10 seconds
   "analogs.temp[2].input", 5 seconds
   "binaries[3] E", 1 Hz
   "binaries", element 1, 10Hz
   "binaries", element 5, 10Hz
   "binaries", element 10, 10Hz

** Scanlist created from this
   10  Hz: "binaries", elements 0-10
    1  Hz: "binaries[3]"
    0.5Hz: "analogs.temp[2].input"
    0.1Hz: "fred", "freddy", "jane"

** Driver actions
   One thread and socket per PLC for communication.

   One TagInfo per tag: name, elements, sizes.

   ScanTask: runs over scanlists for its PLC.
   For each scanlist:
       Figure out how many requests can be combined
       into one request/response round-trip
       (~500 byte limit), record in TagInfo.


* CIP data details

Analog array REALs[40]:

Read "REALS",    2 elements -> REAL[0], REAL[1]
Read "REALS[0]", 2 elements -> REAL[0], REAL[1]
Read "REALS[1]", 2 elements -> REAL[1], REAL[2]

Binary array BOOLs[352]:

Read "BOOLs",     1 element  -> 32bit DINT with bits  0..31
Read "BOOLs[1]",  1 element  -> 32bit DINT with bits 32..63

Access to binaries is translated inside the driver.
Assume access to "fred[5]".

For analog records, a request to the 5th array element is assumed.
For binary records, we assume that the 5th _bit_ should be addressed.
Therefore the first element (bits 0-31) is read and the single
bit in there returned.

This might cause confusion if a binary record is used
with a non-bool tag on the PLC.
Therefore make sure that the types match:

AI:                    Tag should be Single BOOL, REAL, ...
		       or array of REAL
BI, MBBI, MBBIDirect:  Tag should be Single BOOL, Real, ...
		       or BOOL[]

* Write support

The problem is that the EPICS IOC and the crate that it's on
do not "own" the PLC. Someone else might write to the PLC's tag
(RSLogix, PanelMate, another IOC, command-line program).
The PLC can also be rebooted independent from the IOC.

Therefore the write records cannot just write once they
have a new value, they have to reflect the actual value
on the PLC.

** Implementation:
In order to learn about changes to the PLC from other sources, the
driver scans write tags just like read tags, so it always knows the
current value.

When the record is processed, it checks if the value to be written is
different from what the PLC has. If so, it puts its RVAL into the
driver's table and marks it for update
-> the driver writes the new value to the PLC.

Some glue code in the device is called for every value that the driver
gets. It checks if this still matches the record's value. If not, the
record's RVAL is updated and the record is processed. A user interface
tool that looks at the record sees the actual value of the PLC.
The record will not write the value that it just received because
it can see that RVAL matches what the driver has.

** Hint
This fails if two output records are connected to the same tag,
especially if one is a binary output that tries to write 0 or 1.
In that case the two records each try to write "their" value
into the tag, which is likely to make the value fluctuate.

* Debugging

  dbior or a direct call to drvEtherIP_report shows the driver's
  lists for PLCs, ScanLists and Tags.
  When setting "TPRO" for a record, device support shows some
  info on how it interpreted the INP/OUT link.
