/* $Id$
 *
 * mem_string_file
 *
 * printf-style routines that write into
 * a ring-buffer in memory, dumping the
 * content into a file when requested.
 *
 * Motivation:
 * When debugging a network protocol on an IOC,
 * the numerous lines of output, all printed
 * to stdout and viewed in a telnet session,
 * generated enough network traffic to sufficiently
 * delay the network protocol so that the
 * problem that was to be studied didn't happen.
 *
 * This memory buffer is of course fixed in size
 * but printing into it is much faster, hopefully
 * allowing to capture the problem.
 */

#include "R314Compat.h"
#include "mem_string_file.h"
#include "eip_bool.h"

epicsMutexId msfLock = 0;
size_t msfInitialBufferSize = 2*1024*1024;

/* Ring buffer: start in memory, 1st byte beyond buffer, size */
char *msfBuffer = 0;
char *msfBufferEnd = 0;
size_t msfBufferSize = 0;

/* Ring buffer: Current start & end position.
 * head == tail: Ring buffer is empty
 * head != tail: head = first char, tail = just behind last char.
 * (tail actually points to a '\0' that delimits the string)
 */
char *msfHead = 0;
char *msfTail = 0;

static eip_bool msfInit()
{
    if (msfBuffer)
        return true;
    msfBuffer = (char *) malloc(msfInitialBufferSize);
    if (!msfBuffer)
    {
        fprintf(stderr, "MSF: Cannot allocate buffer\n");
        return false;
    }
    msfBufferSize = msfInitialBufferSize;
    msfBufferEnd = msfBuffer + msfBufferSize;
    msfHead = msfTail = msfBuffer;
    msfLock = epicsMutexCreate();
    return true;
}

void msfAdd(const char *text, size_t length)
{
    size_t l1, l2;
    
    if (!msfInit())
        return;
    epicsMutexLock(msfLock);
    if (msfTail + length < msfBufferEnd)
    {   /* Full string fits onto tail */
        memcpy(msfTail, text, length);
    }
    else
    {   /* String wraps around end of buffer.
         * Copy first section:  */
        l1 = msfBufferEnd - msfTail;
        memcpy(msfTail, text, l1);
        l2 = length - l1;
        if (l2 > 0)
            memcpy(msfBuffer, text+l1, l2);
    }

    /* advance tail by length, maybe correct head for overrun */
    if (msfHead <= msfTail)
    {
        msfTail += length;
        if (msfTail >= msfBufferEnd)
        {
            msfTail -= msfBufferSize;
            if (msfTail >= msfHead)
            {
                msfHead = msfTail + 1;
                if (msfHead >= msfBufferEnd)
                    msfHead -= msfBufferSize;
            }
        }
    }
    else
    {
        if (msfTail + length < msfHead)
            msfTail += length;
        else
        {
            msfTail += length;
            if (msfTail >= msfBufferEnd)
                msfTail -= msfBufferSize;
            msfHead = msfTail + 1;
            if (msfHead >= msfBufferEnd)
                msfHead -= msfBufferSize;
        }
    }
    *msfTail = '\0';
    epicsMutexUnlock(msfLock);
}

/* Print into MSF Buffer.
 * Length of the printed string must not exceed
 * MSF_LINE_LENGTH
 */
void msfPrint(const char *format, va_list ap)
{
    char line[MSF_LINE_LENGTH];
    vsprintf(line, format, ap);
    msfAdd(line, strlen(line));
}

void msfPrintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    msfPrint(format, ap);
    va_end(ap);              
}

/* Unclear if fwrite can handle HUGE values
 * for length -> feed it in BUNCHes
 */
static void msfWrite(FILE *f, const char *text, size_t length)
{
    #define BUNCH 100
    while (length >= BUNCH)
    {
        fwrite(text, BUNCH, 1, f);
        text += BUNCH;
        length -= BUNCH;
    }
    if (length > 0)
        fwrite(text, length, 1, f);
}

/* Set buffer to empty */
void msfClear()
{
    epicsMutexLock(msfLock);
    msfHead = msfTail;
    epicsMutexUnlock(msfLock);
}


/* Print buffer to FILE *.
 * Buffer is empty after this call.
 */
void msfDump(FILE *f)
{
    if (!f)
        f = stdout;
    epicsMutexLock(msfLock);
    if (msfHead < msfTail)
    {
        msfWrite(f, msfHead, msfTail - msfHead);
        msfWrite(f, "\n", 1);
    }
    else if (msfHead > msfTail)
    {
        msfWrite(f, msfHead, msfBufferEnd - msfHead);
        msfWrite(f, msfBuffer, msfTail - msfBuffer);
        msfWrite(f, "\n", 1);
    }
    msfHead = msfTail;
    epicsMutexUnlock(msfLock);
}

