// SPDX-License-Identifier: GPL-2.0

#include <stdarg.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>

/*
 * vanprintf - Format a string and place it into a newly allocated buffer.
 * @out: Address of the pointer to place the buffer address into.  Will only be
 *     written to with a successful positive return value.
 * @max_size: If non-negative, the maximum buffer size that this function will
 *     attempt to allocate.  If the formatted string including trailing null
 *     character would not fit, no buffer will be allocated, and an error will
 *     be returned.  (Thus max_size of 0 will always result in an error.)
 * @gfp: GFP flags for kmalloc().
 * @fmt: The format string to use.
 * @ap: Arguments for the format string.
 *
 * Return value meanings:
 *
 *   >=0: A buffer of this size was allocated and its address written to *out.
 *        The caller now owns the buffer and is responsible for freeing it with
 *        kfree().  The final character in the buffer, not counted in this
 *        return value, is the trailing null.  This is the same return value
 *        meaning as snprintf(3).
 *
 *    <0: An error occurred.  Negate the return value for the error number.
 *        @out will not have been written to.  Errors that might come from
 *        snprintf(3) may come from this function as well.  Additionally, the
 *        following errors may occur from this function:
 *
 *        ERANGE: A buffer larger than @max_size would be needed to fit the
 *        formatted string including its trailing null character.
 *
 *        ENOMEM: Allocation of the output buffer failed.
 *
 *        ENOTRECOVERABLE: An unexpected condition occurred.  This may indicate
 *        a bug.
 */
static ssize_t vanprintf(char **out, ssize_t max_size, gfp_t gfp,
	const char *fmt, va_list ap)
{
	int ret;
	ssize_t buf_size;
	char *buf = NULL;
	va_list args1;

	va_copy(args1, ap);
	ret = vsnprintf(NULL, 0, fmt, ap);
	if (ret < 0) {
		pr_err("%s: Formatting failed with error %d.\n", __func__,
			-ret);
		goto fail_before_args1;
	}
	if (max_size >= 0 && ret > max_size) {
		ret = -ERANGE;
		goto fail_before_args1;
	}

	buf_size = ret + 1;
	buf = kmalloc(buf_size, gfp);
	if (buf == NULL) {
		pr_err("%s: kmalloc(%zd, %u) returned NULL\n", __func__,
			buf_size, gfp);
		ret = -ENOMEM;
		goto fail_before_args1;
	}

	ret = vsnprintf(buf, buf_size, fmt, args1);
	va_end(args1);
	if (ret < 0) {
		pr_err("%s: Second formatting pass produced error %d after the first pass succeeded.  This is a bug.\n",
			__func__, -ret);
		goto fail_after_args1;
	}
	if (ret + 1 != buf_size) {
		pr_err("%s: Second formatting pass produced a different formatted output size than the first.  This is a bug.  Will return -ENOTRECOVERABLE.  first_sans_null=%zd second_sans_null=%d\n",
			__func__, buf_size - 1, ret);
		ret = -ENOTRECOVERABLE;
		goto fail_after_args1;
	}

	*out = buf;
	return ret;

 fail_before_args1:
	va_end(args1);
 fail_after_args1:
	kfree(buf);
	if (ret >= 0) {
		pr_err("%s: Jumped to failure cleanup section with non-negative return value %d set.  This is a bug.  Will return -ENOTRECOVERABLE instead.\n",
			__func__, ret);
		ret = -ENOTRECOVERABLE;
	}
	return ret;
}

/*
 * anprintf - Format a string and place it into a newly allocated buffer.
 * @out: Address of the pointer to place the buffer address into.  Will only be
 *     written to with a successful positive return value.
 * @max_size: If non-negative, the maximum buffer size that this function will
 *     attempt to allocate.  If the formatted string including trailing null
 *     character would not fit, no buffer will be allocated, and an error will
 *     be returned.  (Thus max_size of 0 will always result in an error.)
 * @gfp: GFP flags for kmalloc().
 * @fmt: The format string to use.
 * @...: Arguments for the format string.
 *
 * Return value meanings:
 *
 *   >=0: A buffer of this size was allocated and its address written to *out.
 *        The caller now owns the buffer and is responsible for freeing it with
 *        kfree().  The final character in the buffer, not counted in this
 *        return value, is the trailing null.  This is the same return value
 *        meaning as snprintf(3).
 *
 *    <0: An error occurred.  Negate the return value for the error number.
 *        @out will not have been written to.  Errors that might come from
 *        snprintf(3) may come from this function as well.  Additionally, the
 *        following errors may occur from this function:
 *
 *        ERANGE: A buffer larger than @max_size would be needed to fit the
 *        formatted string including its trailing null character.
 *
 *        ENOMEM: Allocation of the output buffer failed.
 *
 *        ENOTRECOVERABLE: An unexpected condition occurred.  This may indicate
 *        a bug.
 */
static ssize_t anprintf(char **out, ssize_t max_size, gfp_t gfp,
	const char *fmt, ...)
{
	ssize_t ret;
	va_list args;

	va_start(args, fmt);
	ret = vanprintf(out, max_size, gfp, fmt, args);
	va_end(args);
	return ret;
}
