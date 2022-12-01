"EtherIP" driver/device support module for EPICS.

Interfaces Allen Bradley PLCs (see www.ab.com) via Ethernet to EPICS IOCs
 * ControlLogix 5000,
   both original versions with separate controller and ENET module,
   and L8x series that includes a network port in the controller.
 * Compact Logix devices

For EPICS R3.14.8 and higher,
on Linux and several other operating systems supported by EPICS libCom.
For earlier version of EPICS base, including R3.13, see tags/releases older than ether_ip-3-0.
VxWorks 5.5 must also use an older version because its compiler requires
K&R style declarations of all variables at the start of a code block.

See 
 * https://controlssoftware.sns.ornl.gov/etherip for more
 * changes.txt for changes
 * https://github.com/ornl-epics/etherip for a Java version of the basic communication library, not connected to an EPCIS IOC.

See Manual.md for usage.
