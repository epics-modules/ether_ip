# Create binary record for testing EtherIP

$OUT=">bin.db";

open OUT or die "Cannot create $OUT";

select OUT;
for ($i=0; $i<352; ++$i)
{
    printf ("record(bi, \"\$(IOC):bi%d\")\n", $i);
    printf ("{\n");
    printf ("	field(SCAN, \"I/O Intr\")\n");
    printf ("	field(DTYP, \"EtherIP\")\n");
    printf ("	field(INP, \"\@\$(PLC) BOOLs[%d] S 0.1\")\n", $i);
    printf ("	field(ZNAM, \"False\")\n");
    printf ("	field(ONAM, \"True\")\n");
    printf ("	field(ZSV,  \"NO_ALARM\")\n");
    printf ("	field(OSV,  \"MAJOR\")\n");
    printf ("}\n");
    printf ("\n");
}

close OUT;



