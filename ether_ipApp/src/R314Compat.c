#include <sysLib.h>
#include <tickLib.h>
#include "R314Compat.h"

void epicsTimeGetCurrent(epicsTimeStamp *s)
{
    *s = tickGet();
}

double epicsTimeDiffInSeconds(epicsTimeStamp *B, epicsTimeStamp *A)
{   /* ULONG epicsTimeStamp ! */
    if (*B >= *A)
        return ((double)(*B - *A)) / sysClkRateGet();
    return -(((double)(*A - *B)) / sysClkRateGet());
}

int epicsTimeLessThan(epicsTimeStamp *A, epicsTimeStamp *B)
{
    return *A < *B;
}

int epicsTimeLessThanEqual(epicsTimeStamp *A, epicsTimeStamp *B)
{
    return *A <= *B;
}

void epicsTimeAddSeconds(epicsTimeStamp *T, double secs)
{
    *T += secs * sysClkRateGet();
}

double epicsThreadSleepQuantum()
{
    return 1.0/sysClkRateGet();
}

void epicsThreadSleep(double secs)
{
    taskDelay((int)(secs * sysClkRateGet()));
}
