/* ec_uart.h - UART module for Chrome EC
 *
 * (Chromium license) */

#ifndef __CROS_EC_UART_H
#define __CROS_EC_UART_H

#include "ec_common.h"

/*****************************************************************************/
/* Output functions */

/* Print formatted output to the UART, like printf().
 *
 * Returns error if output was truncated.
 *
 * Must support format strings for:
 *   char (%c)
 *   string (%s)
 *   native int (signed/unsigned) (%d / %u / %x)
 *   int32_t / uint32_t (%d / %x)
 *   int64_t / uint64_t (%ld / %lu / %lx)
 *   pointer (%p)
 * including padding (%-5s, %8d, %08x)
 *
 * Note: Floating point output (%f / %g) is not required.
 */
EcError UartPrintf(const char* format, ...);

/* Put a null-terminated string to the UART, like puts().
 *
 * Returns error if output was truncated. */
EcError UartPuts(const char* outstr);

/* Flushes output.  Blocks until UART has transmitted all output. */
void UartFlush(void);

/*****************************************************************************/
/* Input functions */

/* Flushes input buffer, discarding all input. */
void UartFlushInput(void);

/* Non-destructively checks for a character in the input buffer.
 *
 * Returns non-zero if the character <c> is in the input buffer.  If
 * c<0, returns non-zero if any character is in the input buffer. */
/* TODO: add boolean type? */
/* TODO: return offset of the character?  That is, how many bytes of
 * input up to and including the character. */
int UartPeek(int c);

/* Reads characters from the UART, similar to fgets().
 *
 * Reads input until one of the following conditions is met:
 *    (1)  <size-1> characters have been read.
 *    (2)  A newline ('\n') has been read.
 *    (3)  The input buffer is empty.
 *
 * Condition (3) means this call never blocks.  This is important
 * because it prevents a race condition where the caller calls
 * UartPeek() to see if input is waiting, or is notified by the
 * callack that input is waiting, but then the input buffer overflows
 * or someone else grabs the input before UartGets() is called.
 *
 * Characters are stored in <dest> and are null-terminated, and
 * include the newline if present.
 *
 * Returns the number of characters read (not counting the terminating
 * null). */
int UartGets(char* dest, int size);

/* Callback handler prototype, called when the UART has input. */
typedef void (*UartHasInputCallback)(void);

/* Registers an input callback handler, replacing any existing
 * callback handler.  If callback==NULL, disables callbacks.
 *
 * Callback will be called whenever the UART receives character <c>.
 * If c<0, callback will be called when the UART receives any
 * character. */
void UartRegisterHasInputCallback(UartHasInputCallback callback, int c);

/* TODO: what to do about input overflow?  That is, what if the input
 * buffer fills with 'A', and we're still waiting for a newline?  Do
 * we drop the oldest output, flush the whole thing, or call the input
 * callback with a message code?  Alternately, should the callback be able
 * to specify a buffer-full-threshold where it gets called?
 *
 * Simplest just to flush the input buffer on overflow. */
/* YJLOU: are you talking about the UartRegisterHasInputCallback()?
 * If yes, this is not a problem because for every input key, we can drop it
 *   if there is no callback registered for it.
 * Else if you are talking about the UartGets() function, I think you can add
 *   the third return critiria: internal key buffer reaches full.
 */

/* TODO: getc(), putc() equivalents? */

/* TODO: input handler assumes debug input new lines are '\n'-delimited? */

#endif  /* __CROS_EC_UART_H */
