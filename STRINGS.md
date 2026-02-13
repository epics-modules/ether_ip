String Notes
============

Allen Bradley PLCs report STRING tag data as custom stucture (0x02A0) of type 0x0FCE:
```
    4 bytes    length
    84 bytes   characters
```

Single STRING, or STRING array, reading one element
---------------------------------------------------
```
Embedded Message:
MR Request
    USINT service   = 0x4C (CIP_ReadData)
    USINT path size = 6 words
    Path: Tag 'PPHEBTCN10'
    UINT elements = 1
Data sent (70 bytes):
00000000 6F 00 2E 00 49 3A 02 1A 00 00 00 00 30 30 30 30 - o...I:......0000
00000010 30 30 30 38 00 00 00 00 00 00 00 00 00 00 02 00 - 0008............
00000020 00 00 00 00 B2 00 1E 00 52 02 20 06 24 01 0A F0 - ........R. .$...
00000030 10 00 4C 06 91 0A 50 50 48 45 42 54 43 4E 31 30 - ..L...PPHEBTCN10
00000040 01 00 01 00 01 00                               - ......
Data Received (136 bytes):
00000000 6F 00 70 00 49 3A 02 1A 00 00 00 00 30 30 30 30 - o.p.I:......0000
00000010 30 30 30 38 00 00 00 00 00 00 00 00 00 00 02 00 - 0008............
00000020 00 00 00 00 B2 00 60 00 CC 00 00 00 A0 02 CE 0F - ......`.........
00000030 0E 00 00 00 48 45 42 54 20 54 52 55 43 4B 20 45 - ....HEBT TRUCK E
00000040 4E 54 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - NT..............
00000050 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000060 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000070 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000080 00 00 00 00 00 00 00 00                         - ........
EncapsulationHeader:
    UINT  command   = 0x6F (SendRRData)
    UINT  length    = 112
    UDINT session   = 0x1A023A49
    UDINT status    = 0x00000000  (OK)
    USINT context[8]= '00000008'
    UDINT options   = 0x00000000
Received RR Data
    UDINT interface handle  0
    UINT timeout            0
    UINT count (addr+data)  2
    UINT address_type       0x0 (UCMM)
    UINT address_length     0
    UINT data_type          0xB2 (Unconnected Message)
    UINT data_length        96
MR_Response:
    USINT service         = 0xCC (Response to CIP_ReadData)
    USINT reserved        = 0x00
    USINT status          = 0x00 (Ok)
    USINT ext. stat. size = 0
    Data (net format) =
    00000000 A0 02 CE 0F 0E 00 00 00 48 45 42 54 20 54 52 55 - ........HEBT TRU
00000010 43 4B 20 45 4E 54 00 00 00 00 00 00 00 00 00 00 - CK ENT..........
00000020 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000030 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000040 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000050 00 00 00 00 00 00 00 00 00 00 00 00             - ............
    Data =  STRING 'HEBT TRUCK ENT'
STRING 'HEBT TRUCK ENT'
```

STRING array, reading two elements
----------------------------------
```
Embedded Message:
MR Request
    USINT service   = 0x4C (CIP_ReadData)
    USINT path size = 6 words
    Path: Tag 'PPHEBTCN10'
    UINT elements = 2
Data sent (70 bytes):
00000000 6F 00 2E 00 49 39 02 1A 00 00 00 00 30 30 30 30 - o...I9......0000
00000010 30 30 30 38 00 00 00 00 00 00 00 00 00 00 02 00 - 0008............
00000020 00 00 00 00 B2 00 1E 00 52 02 20 06 24 01 0A F0 - ........R. .$...
00000030 10 00 4C 06 91 0A 50 50 48 45 42 54 43 4E 31 30 - ..L...PPHEBTCN10
00000040 02 00 01 00 01 00                               - ......
Data Received (224 bytes):
00000000 6F 00 C8 00 49 39 02 1A 00 00 00 00 30 30 30 30 - o...I9......0000
00000010 30 30 30 38 00 00 00 00 00 00 00 00 00 00 02 00 - 0008............
00000020 00 00 00 00 B2 00 B8 00 CC 00 00 00 A0 02 CE 0F - ................
00000030 0E 00 00 00 48 45 42 54 20 54 52 55 43 4B 20 45 - ....HEBT TRUCK E
00000040 4E 54 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - NT..............
00000050 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000060 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000070 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000080 00 00 00 00 00 00 00 00 03 00 00 00 37 34 37 00 - ............747.
00000090 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000A0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000B0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000C0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000D0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 5E A1 - ..............^.
EncapsulationHeader:
    UINT  command   = 0x6F (SendRRData)
    UINT  length    = 200
    UDINT session   = 0x1A023949
    UDINT status    = 0x00000000  (OK)
    USINT context[8]= '00000008'
    UDINT options   = 0x00000000
Received RR Data
    UDINT interface handle  0
    UINT timeout            0
    UINT count (addr+data)  2
    UINT address_type       0x0 (UCMM)
    UINT address_length     0
    UINT data_type          0xB2 (Unconnected Message)
    UINT data_length        184
MR_Response:
    USINT service         = 0xCC (Response to CIP_ReadData)
    USINT reserved        = 0x00
    USINT status          = 0x00 (Ok)
    USINT ext. stat. size = 0
    Data (net format) =
    00000000 A0 02 CE 0F 0E 00 00 00 48 45 42 54 20 54 52 55 - ........HEBT TRU
00000010 43 4B 20 45 4E 54 00 00 00 00 00 00 00 00 00 00 - CK ENT..........
00000020 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000030 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000040 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000050 00 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 - ................
00000060 37 34 37 00 00 00 00 00 00 00 00 00 00 00 00 00 - 747.............
00000070 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000080 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
00000090 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000A0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 - ................
000000B0 00 00 5E A1                                     - ..^.
    Data =  STRING 'HEBT TRUCK ENT'
STRING 'HEBT TRUCK ENT'
```

Found no authorative definition, but a google search indicates that the string length
is limited to 82 chars. The meaning of the last two bytes in the 84 byte structure space
is unclear.
In the data shown above, the last two bytes of STRING[0] are `00 00`,
and the last bytes of STRING[1] are `5E A1`

