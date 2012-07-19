/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

#include "printf.h"
#include "timer.h"
#include "util.h"

static const char error_str[] = "ERROR";

#define MAX_FORMAT 1024  /* Maximum chars in a single format field */

static int hexdigit(int c)
{
	return c > 9 ? (c + 'a' - 10) : (c + '0');
}

int vfnprintf(int (*addchar)(void *context, int c), void *context,
	      const char *format, va_list args)
{
	char intbuf[34];
		/* Longest uint64 in decimal = 20
		 * longest uint32 in binary  = 32
		 */
	int dropped_chars = 0;
	int is_left;
	int pad_zero;
	int pad_width;
	int precision;
	char *vstr;
	int vlen;

	while (*format && !dropped_chars) {
		int c = *format++;

		/* Copy normal characters */
		if (c != '%') {
			dropped_chars |= addchar(context, c);
			continue;
		}

		/* Get first format character */
		c = *format++;

		/* Send "%" for "%%" input */
		if (c == '%' || c == '\0') {
			dropped_chars |= addchar(context, '%');
			continue;
		}

		/* Handle %c */
		if (c == 'c') {
			c = va_arg(args, int);
			dropped_chars |= addchar(context, c);
			continue;
		}

		/* Handle left-justification ("%-5s") */
		is_left = (c == '-');
		if (is_left)
			c = *format++;

		/* Handle padding with 0's */
		pad_zero = (c == '0');
		if (pad_zero)
			c = *format++;

		/* Count padding length */
		pad_width = 0;
		if (c == '*') {
			pad_width = va_arg(args, int);
			c = *format++;
		} else {
			while (c >= '0' && c <= '9') {
				pad_width = (10 * pad_width) + c - '0';
				c = *format++;
			}
		}
		if (pad_width < 0 || pad_width > MAX_FORMAT) {
			/* Sanity check for precision failed */
			format = error_str;
			continue;
		}

		/* Count precision */
		precision = 0;
		if (c == '.') {
			c = *format++;
			if (c == '*') {
				precision = va_arg(args, int);
				c = *format++;
			} else {
				while (c >= '0' && c <= '9') {
					precision = (10 * precision) + c - '0';
					c = *format++;
				}
			}
			if (precision < 0 || precision > MAX_FORMAT) {
				/* Sanity check for precision failed */
				format = error_str;
				continue;
			}
		}

		if (c == 's') {
			vstr = va_arg(args, char *);
			if (vstr == NULL)
				vstr = "(NULL)";
		} else if (c == 'h') {
			/* Hex dump output */
			vstr = va_arg(args, char *);

			if (!precision) {
				/* Hex dump requires precision */
				format = error_str;
				continue;
			}

			for ( ; precision; precision--, vstr++) {
				dropped_chars |=
					addchar(context,
						hexdigit((*vstr >> 4) & 0x0f));
				dropped_chars |=
					addchar(context,
						hexdigit(*vstr & 0x0f));
			}

			continue;
		} else {
			uint64_t v;
			int is_negative = 0;
			int is_64bit = 0;
			int base = 10;

			/* Handle length */
			if (c == 'l') {
				is_64bit = 1;
				c = *format++;
			}

			/* Special-case: %T = current time */
			if (c == 'T') {
				v = get_time().val;
				is_64bit = 1;
				precision = 6;
			} else if (is_64bit) {
				v = va_arg(args, uint64_t);
			} else {
				v = va_arg(args, uint32_t);
			}

			switch (c) {
			case 'd':
				if (is_64bit) {
					if ((int64_t)v < 0) {
						is_negative = 1;
						if (v != (1ULL << 63))
							v = -v;
					}
				} else {
					if ((int)v < 0) {
						is_negative = 1;
						if (v != (1ULL << 31))
							v = -(int)v;
					}
				}
				break;
			case 'u':
			case 'T':
				break;
			case 'X':
			case 'x':
			case 'p':
				base = 16;
				break;
			case 'b':
				base = 2;
				break;
			default:
				format = error_str;
			}
			if (format == error_str)
				continue; /* Bad format specifier */

			/* Convert integer to string, starting at end of
			 * buffer and working backwards. */
			vstr = intbuf + sizeof(intbuf) - 1;
			*(vstr) = '\0';

			/*
			 * Fixed-point precision must fit in our buffer.
			 * Leave space for "0." and the terminating null.
			 */
			if (precision > sizeof(intbuf) - 3)
				precision = sizeof(intbuf) - 3;

			/*
			 * Handle digits to right of decimal for fixed point
			 * numbers.
			 */
			for (vlen = 0; vlen < precision; vlen++)
				*(--vstr) = '0' + uint64divmod(&v, 10);
			if (precision)
				*(--vstr) = '.';

			if (!v)
				*(--vstr) = '0';

			while (v) {
				int digit = uint64divmod(&v, base);
				if (digit < 10)
					*(--vstr) = '0' + digit;
				else if (c == 'X')
					*(--vstr) = 'A' + digit - 10;
				else
					*(--vstr) = 'a' + digit - 10;
			}

			if (is_negative)
				*(--vstr) = '-';

			/*
			 * Precision field was interpreted by fixed-point
			 * logic, so clear it.
			 */
			precision = 0;
		}

		/* Copy string (or stringified integer) */
		vlen = strlen(vstr);

		/* No padding strings to wider than the precision */
		if (precision > 0 && pad_width > precision)
			pad_width = precision;

		/* If precision is zero, print everything */
		if (!precision)
			precision = MAX(vlen, pad_width);

		while (vlen < pad_width && !is_left) {
			dropped_chars |= addchar(context, pad_zero ? '0' : ' ');
			vlen++;
		}
		while (*vstr && --precision >= 0)
			dropped_chars |= addchar(context, *vstr++);
		while (vlen < pad_width && is_left) {
			dropped_chars |= addchar(context, ' ');
			vlen++;
		}
	}

	/* Successful if we consumed all output */
	return dropped_chars ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}


/* Context for snprintf() */
struct snprintf_context {
	char *str;
	int size;
};


/* Add a character to the string */
static int snprintf_addchar(void *context, int c)
{
	struct snprintf_context *ctx = (struct snprintf_context *)context;

	if (!ctx->size)
		return 1;

	*(ctx->str++) = c;
	ctx->size--;
	return 0;
}


/* Print formatted outut to a string */
int snprintf(char *str, int size, const char *format, ...)
{
	struct snprintf_context ctx;
	va_list args;
	int rv;

	if (!str || !size)
		return EC_ERROR_INVAL;

	ctx.str = str;
	ctx.size = size - 1;  /* Reserve space for terminating '\0' */

	va_start(args, format);
	rv = vfnprintf(snprintf_addchar, &ctx, format, args);
	va_end(args);

	/* Terminate string */
	*ctx.str = '\0';

	return rv;
}
