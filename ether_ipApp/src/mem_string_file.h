/* $Id$
 *
 * mem_string_file
 *
 * printf-style routines that write into
 * a ring-buffer in memory, dumping the
 * content into a file when requested.
 *
 * Most important: See comments on msfDump() below.
 *
 * Motivation:
 * When debugging a network protocol on an IOC,
 * the numerous lines of output, all printed
 * to stdout and viewed in a telnet session,
 * generated enough network traffic to sufficiently
 * delay the network protocol so that the
 * problem meant be studied doesn't happen.
 *
 * This memory buffer is of course fixed in size
 * but printing into it is much faster, hopefully
 * allowing to capture the problem.
 *
 * kasemir@lanl.gov
 */

#ifndef MEM_STRING_FILE_H
#define MEM_STRING_FILE_H

#include<stdlib.h>
#include<stdarg.h>
#include<string.h>
#include<stdio.h>

#ifndef true
typedef int bool;
#define true 1
#define false 0
#endif

/*
 * Default size of buffer.
 * Change it in vxWorks startup file
 */
extern size_t msfInitialBufferSize;

/* Max. length of one line */
#define MSF_LINE_LENGTH 200

void msfAdd(const char *text, size_t length);

/* Print into MSF Buffer.
 * Length of the printed string must not exceed
 * MSF_LINE_LENGTH
 */
void msfPrint(const char *format, va_list ap);
void msfPrintf(const char *format, ...);

/* Set buffer to empty */
void msfClear();

/* Print current contents of buffer to FILE *.
 * Buffer is empty after this call.
 *
 * Example: View current buffer on stdout:
 * -> msfDump(0)
 *
 * Example: Copy current buffer into file:
 * -> f=fopen("/tmp/eip.log", "w")
 * -> msfDump(f)
 * -> fclose(f)       
 */
void msfDump(FILE *f);

#endif
