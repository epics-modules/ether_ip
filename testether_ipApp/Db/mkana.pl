$OUT=">ana.db";

open OUT or die "Cannot create $OUT";

select OUT;

for ($i=0; $i<40; ++$i)
{
    printf ("record(ai, \"\$(IOC):ai_REALs%d\")\n", $i);
    printf ("{\n");
    printf ("	field(SCAN, \"1 second\")\n");
    printf ("	field(DTYP, \"EtherIP\")\n");
    printf ("	field(INP, \"\@\$(PLC) REALs[%d]\")\n", $i);
    printf ("	field(EGUF, \"10\")\n");
    printf ("	field(EGU, \"socks\")\n");
    printf ("	field(HOPR, \"10\")\n");
    printf ("	field(LOPR, \"0\")\n");
    printf ("}\n");
    printf ("\n");
    printf ("record(ai, \"\$(IOC):ai_REALs2_%d\")\n", $i);
    printf ("{\n");
    printf ("	field(SCAN, \"1 second\")\n");
    printf ("	field(DTYP, \"EtherIP\")\n");
    printf ("	field(INP, \"\@\$(PLC) REALs2[%d]\")\n", $i);
    printf ("	field(EGUF, \"10\")\n");
    printf ("	field(EGU, \"socks\")\n");
    printf ("	field(HOPR, \"10\")\n");
    printf ("	field(LOPR, \"0\")\n");
    printf ("}\n");
    printf ("\n");
    printf ("record(ai, \"\$(IOC):ai_REALs3_%d\")\n", $i);
    printf ("{\n");
    printf ("	field(SCAN, \"1 second\")\n");
    printf ("	field(DTYP, \"EtherIP\")\n");
    printf ("	field(INP, \"\@\$(PLC) REALs3[%d]\")\n", $i);
    printf ("	field(EGUF, \"10\")\n");
    printf ("	field(EGU, \"socks\")\n");
    printf ("	field(HOPR, \"10\")\n");
    printf ("	field(LOPR, \"0\")\n");
    printf ("}\n");
    printf ("\n");
}



