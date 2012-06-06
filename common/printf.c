/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

#include "printf.h"
#include "timer.h"
#include "util.h"

static const char error_str[] = "ERROR";

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
		while (c >= '0' && c <= '9') {
			pad_width = (10 * pad_width) + c - '0';
			c = *format++;
		}
		if (pad_width > 80) {
			/* Sanity check for width failed */
			format = error_str;
			continue;
		}

		if (c == 's') {
			vstr = va_arg(args, char *);
			if (vstr == NULL)
				vstr = "(NULL)";
		} else {
			uint64_t v;
			int is_negative = 0;
			int is_64bit = 0;
			int base = 10;
			int fixed_point = 0;

			/* Handle fixed point numbers */
			if (c == '.') {
				c = *format++;
				if (c < '0' || c > '9') {
					format = error_str;
					continue;
				}
				fixed_point = c - '0';
				c = *format++;
			}

			if (c == 'l') {
				is_64bit = 1;
				c = *format++;
			}

			/* Special-case: %T = current time */
			if (c == 'T') {
				v = get_time().val;
				is_64bit = 1;
				fixed_point = 6;
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

			/* Handle digits to right of decimal for fixed point
			 * numbers. */
			for (vlen = 0; vlen < fixed_point; vlen++)
				*(--vstr) = '0' + uint64divmod(&v, 10);
			if (fixed_point)
				*(--vstr) = '.';

			if (!v)
				*(--vstr) = '0';

			while (v) {
				int digit = uint64divmod(&v, base);
				if (digit < 10)
					*(--vstr) = '0' + digit;
				else if (c == 'X')
					*(--vstr) = 'A' + digit - 9;
				else
					*(--vstr) = 'a' + digit - 9;
			}

			if (is_negative)
				*(--vstr) = '-';
		}

		/* Copy string (or stringified integer) */
		vlen = strlen(vstr);
		while (vlen < pad_width && !is_left) {
			dropped_chars |= addchar(context, pad_zero ? '0' : ' ');
			vlen++;
		}
		while (*vstr)
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
