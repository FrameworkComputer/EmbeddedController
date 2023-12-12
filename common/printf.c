/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Printf-like functionality for Chrome EC */

/*
 * Define _POSIX_C_SOURCE to 200809 to unlock strnlen() when using Newlib
 * libc. Newlib's CMakeLists.txt file intentionally disables POSIX definitions
 * from Newlib to avoid conflicts with Zephyr POSIX library and networking
 * subsystem.
 *
 * Refer to the Zephyr's Github issue #52739 for more details.
 */
#ifdef CONFIG_NEWLIB_LIBC
#define _POSIX_C_SOURCE 200809
#endif /* CONFIG_NEWLIB_LIBC */

#include "builtin/assert.h"
#include "console.h"
#include "printf.h"
#include "timer.h"
#include "util.h"

static const char error_str[] = "ERROR";

#define MAX_FORMAT 1024 /* Maximum chars in a single format field */

/**
 * Convert the lowest nibble of a number to hex
 *
 * @param c	Number to extract lowest nibble from
 *
 * @return The corresponding ASCII character ('0' - 'f').
 */
static int hexdigit(int c)
{
	/* Strip off just the last nibble */
	c &= 0x0f;

	return c > 9 ? (c + 'a' - 10) : (c + '0');
}

/* Flags for vfnprintf() flags */
#define PF_LEFT BIT(0) /* Left-justify */
#define PF_PADZERO BIT(1) /* Pad with 0's not spaces */
#define PF_SIGN BIT(2) /* Add sign (+) for a positive number */
#define PF_64BIT BIT(3) /* Number is 64-bit */

test_export_static char *uint64_to_str(char *buf, int buf_len, uint64_t val,
				       int precision, int base, bool uppercase)
{
	int i;
	char *str;

	if (buf_len <= 1)
		return NULL;

	if (base <= 1)
		return NULL;

	/*
	 * Convert integer to string, starting at end of
	 * buffer and working backwards.
	 */
	str = buf + buf_len - 1;
	*(str) = '\0';

	/*
	 * Fixed-point precision must fit in our buffer.
	 * Leave space for "0." and the terminating null.
	 */
	if (precision > buf_len - 3) {
		precision = buf_len - 3;
		if (precision < 0)
			return NULL;
	}

	/*
	 * Handle digits to right of decimal for fixed point numbers.
	 */
	for (i = 0; i < precision; i++)
		*(--str) = '0' + uint64divmod(&val, 10);
	if (precision >= 0)
		*(--str) = '.';

	if (!val)
		*(--str) = '0';

	while (val) {
		int digit;

		if (str <= buf)
			return NULL;

		digit = uint64divmod(&val, base);
		if (digit < 10)
			*(--str) = '0' + digit;
		else if (uppercase)
			*(--str) = 'A' + digit - 10;
		else
			*(--str) = 'a' + digit - 10;
	}

	return str;
}

int snprintf_timestamp_now(char *str, size_t size)
{
	return snprintf_timestamp(str, size, get_time().val);
}

int snprintf_timestamp(char *str, size_t size, uint64_t timestamp)
{
	int len;
	int precision;
	char *tmp_str;
	char tmp_buf[PRINTF_TIMESTAMP_BUF_SIZE];
	int base = 10;

	if (size == 0)
		return -EC_ERROR_INVAL;

	/* Ensure string has terminating '\0' in error cases. */
	str[0] = '\0';

	if (IS_ENABLED(CONFIG_CONSOLE_VERBOSE)) {
		precision = 6;
	} else {
		precision = 3;
		timestamp /= 1000;
	}

	tmp_str = uint64_to_str(tmp_buf, sizeof(tmp_buf), timestamp, precision,
				base, false);
	if (!tmp_str)
		return -EC_ERROR_OVERFLOW;

	len = strlen(tmp_str);
	if (len + 1 > size)
		return -EC_ERROR_OVERFLOW;

	memcpy(str, tmp_str, len + 1);

	return len;
}

/*
 * Print the buffer as a string of bytes in hex.
 * Returns 0 on success or an error on failure.
 */
static int print_hex_buffer(int (*addchar)(void *context, int c), void *context,
			    const char *vstr, int precision, int pad_width,
			    int flags)

{
	/*
	 * Divide pad_width instead of multiplying precision to avoid overflow
	 * error in the condition. The "/2" and "2*" can be optimized by
	 * the compiler.
	 */

	if ((pad_width / 2) >= precision)
		pad_width -= 2 * precision;
	else
		pad_width = 0;

	while (pad_width > 0 && !(flags & PF_LEFT)) {
		if (addchar(context, flags & PF_PADZERO ? '0' : ' '))
			return EC_ERROR_OVERFLOW;
		pad_width--;
	}

	for (; precision; precision--, vstr++) {
		if (addchar(context, hexdigit(*vstr >> 4)) ||
		    addchar(context, hexdigit(*vstr)))
			return EC_ERROR_OVERFLOW;
	}

	while (pad_width > 0 && (flags & PF_LEFT)) {
		if (addchar(context, ' '))
			return EC_ERROR_OVERFLOW;
		pad_width--;
	}

	return EC_SUCCESS;
}

struct hex_char_context {
	struct hex_buffer_params hex_buf_params;
	char *str;
	size_t size;
};

int add_hex_char(void *context, int c)
{
	struct hex_char_context *ctx = context;

	if (ctx->size == 0)
		return EC_ERROR_OVERFLOW;

	*(ctx->str++) = c;
	ctx->size--;

	return EC_SUCCESS;
}

size_t hex_str_buf_size(size_t num_bytes)
{
	return 2 * num_bytes + 1;
}

int snprintf_hex_buffer(char *str, size_t size,
			const struct hex_buffer_params *params)
{
	int rv;
	struct hex_char_context context = {
		.hex_buf_params = *params,
		.str = str,
		/*
		 * Reserve space for terminating '\0'.
		 */
		.size = size - 1,
	};

	if (size == 0)
		return -EC_ERROR_INVAL;

	rv = print_hex_buffer(add_hex_char, &context, params->buffer,
			      params->size, 0, 0);

	*context.str = '\0';

	return (rv == EC_SUCCESS) ? (context.str - str) : -rv;
}

int vfnprintf(int (*addchar)(void *context, int c), void *context,
	      const char *format, va_list args)
{
	/*
	 * Longest uint64 in decimal = 20
	 * Longest uint32 in binary  = 32
	 * + sign bit
	 * + terminating null
	 */
	char intbuf[34];
	int flags;
	int pad_width;
	int precision;
	char *vstr;
	int vlen;

	while (*format) {
		int c = *format++;
		char sign = 0;

		/* Copy normal characters */
		if (c != '%') {
			if (addchar(context, c))
				return EC_ERROR_OVERFLOW;
			continue;
		}

		/* Zero flags, now that we're in a format */
		flags = 0;

		/* Get first format character */
		c = *format++;

		/* Send "%" for "%%" input */
		if (c == '%' || c == '\0') {
			if (addchar(context, '%'))
				return EC_ERROR_OVERFLOW;

			if (c == '\0')
				break;

			continue;
		}

		/* Handle %c */
		if (c == 'c') {
			c = va_arg(args, int);
			if (addchar(context, c))
				return EC_ERROR_OVERFLOW;
			continue;
		}

		/* Handle left-justification ("%-5s") */
		if (c == '-') {
			flags |= PF_LEFT;
			c = *format++;
		}

		/* Handle positive sign (%+d) */
		if (c == '+') {
			flags |= PF_SIGN;
			c = *format++;
		}

		/* Handle padding with 0's */
		if (c == '0') {
			flags |= PF_PADZERO;
			c = *format++;
		}

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
			/* Validity check for precision failed */
			format = error_str;
			continue;
		}

		/* Count precision */
		precision = -1;
		if (c == '.') {
			c = *format++;
			if (c == '*') {
				precision = va_arg(args, int);
				c = *format++;
			} else {
				precision = 0;
				while (c >= '0' && c <= '9') {
					precision = (10 * precision) + c - '0';
					c = *format++;
				}
			}
			if (precision < 0 || precision > MAX_FORMAT) {
				/* Validity check for precision failed */
				format = error_str;
				continue;
			}
		}

		if (c == 's') {
			vstr = va_arg(args, char *);
			if (vstr == NULL)
				vstr = "(NULL)";

		} else {
			int base = 10;
			uint64_t v;

			void *ptrval;

			/*
			 * Handle length:
			 * %l - supports 64-bit longs, 32-bit longs are
			 *      supported with a config flag, see comment
			 *      below for more details
			 * %ll - long long
			 * %z - size_t
			 */
			if (c == 'l') {
				if (sizeof(long) == sizeof(uint64_t))
					flags |= PF_64BIT;

				c = *format++;
				if (c == 'l') {
					flags |= PF_64BIT;
					c = *format++;
				}

				/*
				 * The CONFIG_PRINTF_LONG_IS_32BITS flag is
				 * required to enable the %l flag on systems
				 * where it would signify a 32-bit value.
				 * Otherwise, %l on 32-bit systems is
				 * deliberately deprecated. %l was originally
				 * used as shorthand for 64-bit values. When
				 * compile-time printf format checking was
				 * enabled, it had to be cleaned up to be
				 * sizeof(long), which is 32 bits on today's
				 * ECs. This presents a mismatch which can be
				 * dangerous if a new-style printf call is
				 * cherry-picked into an old firmware branch.
				 * For more context, see
				 * https://issuetracker.google.com/issues/172210614
				 */
				if (!IS_ENABLED(CONFIG_PRINTF_LONG_IS_32BITS) &&
				    !(flags & PF_64BIT)) {
					format = error_str;
					continue;
				}
			} else if (c == 'z') {
				if (sizeof(size_t) == sizeof(uint64_t))
					flags |= PF_64BIT;

				c = *format++;
			}

			if (c == 'p') {
				c = -1;
				ptrval = va_arg(args, void *);
				v = (unsigned long)ptrval;
				base = 16;
				if (sizeof(unsigned long) == sizeof(uint64_t))
					flags |= PF_64BIT;
			} else if (flags & PF_64BIT) {
				v = va_arg(args, uint64_t);
			} else {
				v = va_arg(args, uint32_t);
			}

			switch (c) {
#ifdef CONFIG_PRINTF_LONG_IS_32BITS
			case 'i':
#endif /* CONFIG_PRINTF_LONG_IS_32BITS */
			case 'd':
				if (flags & PF_64BIT) {
					if ((int64_t)v < 0) {
						sign = '-';
						if (v != (1ULL << 63))
							v = -v;
					} else if (flags & PF_SIGN) {
						sign = '+';
					}
				} else {
					if ((int)v < 0) {
						sign = '-';
						if (v != (1ULL << 31))
							v = -(int)v;
					} else if (flags & PF_SIGN) {
						sign = '+';
					}
				}
				break;
			case 'u':
			case 'T':
				break;
			case 'X':
			case 'x':
				base = 16;
				break;

			/* Int passthrough for pointers. */
			case -1:
				break;
			default:
				format = error_str;
			}
			if (format == error_str)
				continue; /* Bad format specifier */

			vstr = uint64_to_str(intbuf, sizeof(intbuf), v,
					     precision, base, c == 'X');
			ASSERT(vstr);

			if (sign)
				*(--vstr) = sign;

			/*
			 * Precision field was interpreted by fixed-point
			 * logic, so clear it.
			 */
			precision = -1;
		}

		/* No padding strings to wider than the precision */
		if (precision >= 0 && pad_width > precision)
			pad_width = precision;

		if (precision < 0) {
			/* If precision is unset, print everything */
			vlen = strlen(vstr);
			precision = MAX(vlen, pad_width);
		} else {
			/*
			 * If precision is set, ensure that we do not
			 * overrun it
			 */
			vlen = strnlen(vstr, precision);
		}

		while (vlen < pad_width && !(flags & PF_LEFT)) {
			if (addchar(context, flags & PF_PADZERO ? '0' : ' '))
				return EC_ERROR_OVERFLOW;
			vlen++;
		}
		while (--precision >= 0 && *vstr)
			if (addchar(context, *vstr++))
				return EC_ERROR_OVERFLOW;
		while (vlen < pad_width && flags & PF_LEFT) {
			if (addchar(context, ' '))
				return EC_ERROR_OVERFLOW;
			vlen++;
		}
	}

	/* If we're still here, we consumed all output */
	return EC_SUCCESS;
}
