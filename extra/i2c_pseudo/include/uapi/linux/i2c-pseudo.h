/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * i2c-pseudo.h - I2C userspace adapter char device interface
 *
 * Copyright 2023 Google LLC
 */

#ifndef _UAPI_LINUX_I2C_PSEUDO_H
#define _UAPI_LINUX_I2C_PSEUDO_H

#include <linux/compiler.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/* /dev/i2c-pseudo ioctl commands */
#define I2CP_IOCTL_CODE 0x2C

/*
 * Create the I2C adapter device.
 *
 * *arg must be struct i2cp_ioctl_start_arg.
 *
 * Will only succeed at most once per fd lifetime.
 *
 * Non-negative return indicates success, upon which:
 * - arg->output will have all fields overwritten.
 * - No other fields of *arg will be modified.
 *
 * Negative return indicates an errno, upon which:
 * - arg->output field values are undefined.
 * - No other fields of *arg will be modified.
 * - There is no I2C adapter device for this pseudo controller fd.
 * - Retrying may succeed if the error condition is corrected.
 */
#define I2CP_IOCTL_START _IOWR(I2CP_IOCTL_CODE, 0, struct i2cp_ioctl_start_arg)

/*
 * Take in the next requested I2C transfer transaction.
 *
 * *arg must be struct i2cp_ioctl_xfer_req_arg.
 *
 * May only be used between I2CP_IOCTL_START and I2CP_IOCTL_SHUTDOWN.
 *
 * If no I2C transfer requests are pending will either block or set errno
 * (to EAGAIN or EWOULDBLOCK) based on O_NONBLOCK flag.
 *
 * Non-negative return indicates success, upon which:
 * - arg->output will have all fields overwritten
 * - The first arg->output.num_msgs of arg->msgs will have all fields
 *   overwritten.
 * - Each such arg->msgs[i].buf will point to a section of arg->data_buf.
 * - Each buf section will have all of its arg->msgs[i].len bytes overwritten.
 * - The buf sections will not overlap between msgs, however multiple
 *   zero-length sections may point to the same address.
 * - No other fields of *arg will be modified.
 *
 * Negative return indicates an errno. Some errno have special output semantics.
 *
 * EMSGSIZE:
 * - arg->msgs_len indicates arg->msgs is of insufficient size to hold all
 *   i2c_msg for the requested transaction.
 * - arg->output will be filled in the same as a successful call, but no portion
 *   of either arg->msgs or arg->data_buf will be modified.
 * - Thus arg->output.num_msgs will indicate the arg->msgs_len needed for this
 *   transaction.
 * - A retry with with a sufficiently large arg->msgs_len may be successful.
 *   If no such retry will be attempted, it is best to use I2CP_IOCTL_XFER_REPLY
 *   with a non-zero error value to unblock the requesting I2C device driver
 *   from the I2C adapter timeout.
 *
 * ENOBUFS:
 * - arg->data_buf_len indicates arg->data_buf is of insufficient size to hold
 *   all i2c_msg.buf for the requested transaction.
 * - arg->output and each arg->msgs will be filled in the same as a successful
 *   call, except each i2c_msg.buf will be NULL and no portion of arg->data_buf
 *   will be modified.
 * - Thus the sum of each i2c_msg.len will indicate the arg->data_buf_len needed
 *   for this transaction.
 * - A retry with with a sufficiently large arg->data_buf may be successful.
 *   If no such retry will be attempted, it is best to use I2CP_IOCTL_XFER_REPLY
 *   with a non-zero error value to unblock the requesting I2C device driver
 *   from the I2C adapter timeout.
 */
#define I2CP_IOCTL_XFER_REQ \
	_IOWR(I2CP_IOCTL_CODE, 1, struct i2cp_ioctl_xfer_req_arg)

/*
 * Reply to the most recent I2C transfer request from I2CP_IOCTL_XFER_REQ.
 *
 * *arg must be struct i2cp_ioctl_xfer_reply_arg, which is never modified by
 * this.
 *
 * Non-negative return indicates success, negative indicates an errno.
 * Some errno have a specific meaning.
 *
 * ETIME: The *arg->xfer_id is for a transfer request that already received a
 * reply, either from a prior I2CP_IOCTL_XFER_REQ call, or from exceeding the
 * I2C adapter timeout.
 */
#define I2CP_IOCTL_XFER_REPLY \
	_IOW(I2CP_IOCTL_CODE, 2, struct i2cp_ioctl_xfer_reply_arg)

/*
 * Report counters of I2C transfers requested of this pseudo controller.
 *
 * *arg must be struct i2cp_ioctl_xfer_counters.
 *
 * Non-negative return indicates success, upon which *arg will have all fields
 * overwritten.
 *
 * Negative return indicates an errno, upon which all *arg fields are undefined.
 *
 * Each I2C transfer requested of this pseudo adapter by an I2C device driver
 * will result in exactly one counter field increment.
 *
 * Counter increment happens once success or failure is known. Transfers still
 * waiting for a reply are not yet counted.
 *
 * The sum of all *arg fields is the total number of I2C transfers requested of
 * this pseudo controller, except for those still waiting for a reply.
 */
#define I2CP_IOCTL_GET_COUNTERS \
	_IOR(I2CP_IOCTL_CODE, 3, struct i2cp_ioctl_xfer_counters)

/*
 * Unblock all pseudo controller I/O and refuse further I2C transfer requests.
 *
 * Use of this is _optional_ for userspace convenience to unblock any threads
 * waiting or polling for I2C transfer requests.
 *
 * It is safe and allowed to call this repeatedly, though after any successful
 * call it is not expected for subsequent calls to do anything useful.
 *
 * arg must be NULL and is reserved for possible future use.
 *
 * Non-negative return indicates success, negative return indicates an errno.
 *
 * This will never fail if the fd is correct and arg is NULL.
 */
#define I2CP_IOCTL_SHUTDOWN _IO(I2CP_IOCTL_CODE, 4)

/* Filled in by I2CP_IOCTL_START */
struct i2cp_ioctl_start_output {
	/* I2C adapter number from I2C subsystem */
	__u64 adapter_num;
	/*
	 * The length of i2cp_ioctl_start_arg.name that was actually used, not
	 * including any Null termination.
	 */
	__u32 name_len;
};

/* Argument for I2CP_IOCTL_START */
struct i2cp_ioctl_start_arg {
	/* output; must be first struct field */
	struct i2cp_ioctl_start_output output;
	/*
	 * Bitmask of I2C_FUNC_* flags.
	 * I2C_FUNC_I2C is currently mandatory (this may change).
	 * I2C_FUNC_SMBUS_EMUL or any subset is optional.
	 * No other functionality flags are supported yet.
	 * Additional flags may be supported in the future.
	 * Setting unsupported flags is an error.
	 */
	__u32 functionality;
	/*
	 * I2C transaction timeout in milliseconds.
	 * If 0 the i2c-pseudo module parameter default_timeout_ms will be used.
	 * If default_timeout_ms itself is 0 the subsystem default will be used.
	 */
	__u32 timeout_ms;
	/*
	 * I2C adapter name, Null-terminated. Must be non-NULL.
	 * The maximum length allowed is an internal subsystem detail, so if
	 * exceeding that length is allowed and the name will simply be cut off
	 * at the maximum length permitted. See output.name_len for the actual
	 * length that was used, not including Null termination.
	 */
	const char __user *name;
};

/* Filled in by I2CP_IOCTL_XFER_REQ */
struct i2cp_ioctl_xfer_req_output {
	/*
	 * Identifier for this I2C transfer request.
	 * Will never be reused for this pseudo controller instance unless an
	 * internal counter wraps.
	 */
	__u64 xfer_id;
	/* Number of i2c_msg in this transfer request. */
	__u32 num_msgs;
};

/* Argument for I2CP_IOCTL_XFER_REQ */
struct i2cp_ioctl_xfer_req_arg {
	/* must be first struct field */
	struct i2cp_ioctl_xfer_req_output output;
	/* must point to array of at least msgs_len length */
	struct i2c_msg __user *msgs;
	/* must point to array of at least data_buf_len length */
	__u8 __user *data_buf;
	/* length of msgs array, must be positive */
	__u32 msgs_len;
	/* length of data_buf array, must be positive */
	__u32 data_buf_len;
};

/* Argument for I2CP_IOCTL_XFER_REPLY */
struct i2cp_ioctl_xfer_reply_arg {
	/*
	 * Must point to array of num_msgs length whose i2c_msg contents matches
	 * the msgs from I2CP_IOCTL_XFER_REQ for this xfer_id.
	 *
	 * For I2C reads (i2c_msg.flags & I2C_M_RD) the i2c_msg.buf content will
	 * be copied back to the requesting I2C device driver.
	 *
	 * This need not point to the same memory used with I2CP_IOCTL_XFER_REQ,
	 * nor do the i2c_msg.buf pointers, however simply reusing the same
	 * memory is permissible and expected.
	 */
	struct i2c_msg __user *msgs;
	/* i2cp_ioctl_xfer_req_arg.output.xfer_id this reply is for. */
	__u64 xfer_id;
	/*
	 * Number of I2C messages successfully processed.
	 * Gets returned to I2C device driver.
	 * Indicates length of msgs array.
	 * Must be <= i2cp_ioctl_xfer_req_output.num_msgs from the xfer_id.
	 * Any value < i2cp_ioctl_xfer_req_output.num_msgs implies a failure.
	 */
	__u32 num_msgs;
	/*
	 * Return value for master_xfer.
	 * Gets returned to I2C device driver.
	 * Set to 0 (zero) for a successful I2C transaction.
	 */
	__u32 error;
};

/* Argument for and filled in by I2CP_IOCTL_GET_COUNTERS */
struct i2cp_ioctl_xfer_counters {
	/* I2CP_IOCTL_XFER_REPLY received */
	__u64 controller_replied;
	/* kernel bug */
	__u64 unknown_failure;
	/* master_xfer after I2CP_IOCTL_SHUTDOWN */
	__u64 after_shutdown;
	/* master_xfer with num_msgs >= max_msgs_per_xfer */
	__u64 too_many_msgs;
	/* master_xfer with sum(i2c_msg.len) > max_total_data_per_xfer */
	__u64 too_much_data;
	/* interrupted before I2CP_IOCTL_XFER_REQ */
	__u64 interrupted_before_req;
	/* interrupted after I2CP_IOCTL_XFER_REQ before I2CP_IOCTL_XFER_REPLY */
	__u64 interrupted_before_reply;
	/* timed out before I2CP_IOCTL_XFER_REQ */
	__u64 timed_out_before_req;
	/* timed out after I2CP_IOCTL_XFER_REQ before I2CP_IOCTL_XFER_REPLY */
	__u64 timed_out_before_reply;
};

#endif /* _UAPI_LINUX_I2C_PSEUDO_H */
