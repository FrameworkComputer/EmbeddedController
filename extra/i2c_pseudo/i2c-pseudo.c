// SPDX-License-Identifier: GPL-2.0
/*
 * This Linux kernel module implements pseudo I2C adapters that can be backed
 * by userspace programs.  This allows for implementing an I2C bus from
 * userspace, which can tunnel the I2C commands through another communication
 * channel to a remote I2C bus.
 */

#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

/* Minimum i2cp_limit module parameter value. */
#define I2CP_ADAPTERS_MIN 0
/* Maximum i2cp_limit module parameter value. */
#define I2CP_ADAPTERS_MAX 256
/* Default i2cp_limit module parameter value. */
#define I2CP_DEFAULT_LIMIT 8
/* Value for alloc_chrdev_region() baseminor arg. */
#define I2CP_CDEV_BASEMINOR 0
#define I2CP_TIMEOUT_MS_MIN 0
#define I2CP_TIMEOUT_MS_MAX (60 * MSEC_PER_SEC)
#define I2CP_DEFAULT_TIMEOUT_MS (3 * MSEC_PER_SEC)

/* Used in struct device.kobj.name field. */
#define I2CP_DEVICE_NAME "i2c-pseudo-controller"
/* Value for alloc_chrdev_region() name arg. */
#define I2CP_CHRDEV_NAME "i2c_pseudo"
/* Value for class_create() name arg. */
#define I2CP_CLASS_NAME "i2c-pseudo"
/* Value for alloc_chrdev_region() count arg.  Should always be 1. */
#define I2CP_CDEV_COUNT 1

#define I2CP_ADAP_START_CMD "ADAPTER_START"
#define I2CP_ADAP_SHUTDOWN_CMD "ADAPTER_SHUTDOWN"
#define I2CP_GET_NUMBER_CMD "GET_ADAPTER_NUM"
#define I2CP_NUMBER_REPLY_CMD "I2C_ADAPTER_NUM"
#define I2CP_GET_PSEUDO_ID_CMD "GET_PSEUDO_ID"
#define I2CP_PSEUDO_ID_REPLY_CMD "I2C_PSEUDO_ID"
#define I2CP_SET_NAME_SUFFIX_CMD "SET_ADAPTER_NAME_SUFFIX"
#define I2CP_SET_TIMEOUT_CMD "SET_ADAPTER_TIMEOUT_MS"
#define I2CP_BEGIN_MXFER_REQ_CMD "I2C_BEGIN_XFER"
#define I2CP_COMMIT_MXFER_REQ_CMD "I2C_COMMIT_XFER"
#define I2CP_MXFER_REQ_CMD "I2C_XFER_REQ"
#define I2CP_MXFER_REPLY_CMD "I2C_XFER_REPLY"

/* Maximum size of a controller command. */
#define I2CP_CTRLR_CMD_LIMIT 255
/* Maximum number of controller read responses to allow enqueued at once. */
#define I2CP_CTRLR_RSP_QUEUE_LIMIT 256
/* The maximum size of a single controller read response. */
#define I2CP_MAX_MSG_BUF_SIZE 16384
/* Maximum size of a controller read or write. */
#define I2CP_RW_SIZE_LIMIT 1048576

/*
 * Marks the end of a controller command or read response.
 *
 * Fundamentally, controller commands and read responses could use different end
 * marker characters, but for validity they should be the same.
 *
 * This must be a variable, not a macro, because it is passed to copy_to_user()
 * by address.  Taking the address of a character literal causes a compiler
 * error.  Making these C strings instead of characters would allow for that
 * (with other implications), but then copy_to_user() itself refuses to compile,
 * because of an assertion that the copy size (1) must match the size of the
 * string literal (2 with its trailing null).
 */
static const char i2cp_ctrlr_end_char = '\n';
/* Separator between I2C message header fields in the controller bytestream. */
static const char i2cp_ctrlr_header_sep_char = ' ';
/* Separator between I2C message data bytes in the controller bytestream. */
static const char i2cp_ctrlr_data_sep_char = ':';

/*
 * This used instead of strcmp(in_str, other_str) because in_str may have null
 * characters within its in_size boundaries, which could cause an unintended
 * match.
 */
#define STRING_NEQ(in_str, in_size, other_str) \
	(in_size != strlen(other_str) || memcmp(other_str, in_str, in_size))

#define STR_HELPER(num) #num
#define STR(num) STR_HELPER(num)

#define CONST_STRLEN(str) (sizeof(str) - 1)

/*
 * The number of pseudo I2C adapters permitted.  This default value can be
 * overridden at module load time.  Must be in the range
 * [I2CP_ADAPTERS_MIN, I2CP_ADAPTERS_MAX].
 *
 * As currently used, this MUST NOT be changed during or after module
 * initialization.  If the ability to change this at runtime is desired, an
 * audit of the uses of this variable will be necessary.
 */
static unsigned int i2cp_limit = I2CP_DEFAULT_LIMIT;
module_param(i2cp_limit, uint, 0444);

/*
 * The default I2C pseudo adapter timeout, in milliseconds.
 * 0 means use Linux I2C adapter default.
 * Can be changed per adapter by the controller.
 */
static unsigned int i2cp_default_timeout_ms = I2CP_DEFAULT_TIMEOUT_MS;
module_param(i2cp_default_timeout_ms, uint, 0444);

struct i2cp_controller;

/* This tracks all I2C pseudo adapters. */
struct i2cp_counters {
	/* This must be held while accessing any fields. */
	struct mutex lock;
	unsigned int count;
	/*
	 * This is used to make a strong attempt at avoiding ID reuse,
	 * especially during the lifetime of a userspace i2c-dev client.  This
	 * can wrap by design, and thus makes no perfect guarantees.
	 */
	/* Same type as struct i2cp_controller.id field. */
	unsigned int next_ctrlr_id;
	struct i2cp_controller **all_controllers;
};

static struct class *i2cp_class;
static dev_t i2cp_dev_num;

struct i2cp_device {
	struct i2cp_counters counters;
	struct cdev cdev;
	struct device device;
};

static struct i2cp_device *i2cp_device;

/*
 * An instance of this struct in i2cp_cmds[] array defines a command that a
 * controller process may write to the I2C pseudo character device, hereafter a
 * "write command."
 *
 * A write command consists of one or more header fields, followed optionally by
 * data.  Each header field is fully buffered before being sent to
 * header_receiver().  Data is not fully buffered, it is chunked in fixed
 * increments set by the return value of the final header_receiver() call.
 *
 * Every write command begins with its name.  The name is used both to map the
 * command to an instance of this struct, and as the first header field.
 *
 * A header field ends at either i2cp_ctrlr_end_char or
 * i2cp_ctrlr_header_sep_char, neither of which is ever included in header field
 * values passed to a callback.
 *
 * A command always ends at i2cp_ctrlr_end_char.  Anything written after that by
 * the controller is treated as a new command.
 *
 * After i2cp_ctrlr_header_sep_char the return value of header_receiver() from
 * the previous header field is used to determine whether subsequent input is
 * another header field, or data.
 *
 * Once header_receiver() has indicated that data is expected, all input until
 * i2cp_ctrlr_end_char will be handled as data, and header_receiver() will not
 * be called again for the command.
 *
 * For a given I2C pseudo controller instance there will never be more than one
 * write command in flight at once, and there will never be more than one of
 * these callbacks executing at once.  These callbacks need not do any
 * cross-thread synchronization among themselves.
 *
 * Note: Data may contain i2cp_ctrlr_header_sep_char.
 *
 * Note: There are no restrictions on the use of the null char ('\0') in either
 * header fields or data.  (If either i2cp_ctrlr_header_sep_char or
 * i2cp_ctrlr_end_char is null then the respective restrictions around those
 * characters apply as usual, of course.)  Write command implementations need
 * not use or expect null, but they must at least handle it gracefully and fail
 * without bad side effects, same as with any unexpected input.
 */
struct i2cp_cmd {
	/*
	 * Set these to the command name.
	 *
	 * The command name must not contain i2cp_ctrlr_header_sep_char or
	 * i2cp_ctrlr_end_char.  The behavior otherwise is undefined; such a
	 * command would be uncallable, and could become either a build-time or
	 * runtime error.
	 *
	 * The command name must be unique in the i2cp_cmds[] array.  The
	 * behavior with duplicate command names is undefined, subject to
	 * change, and subject to become either a build-time or runtime error.
	 */
	char *cmd_string; /* Must be non-NULL. */
	size_t cmd_size; /* Must be non-zero. */

	/*
	 * This is called once for each I2C pseudo controller to initialize
	 * *data, prior to that pointer being passed to any other callbacks.
	 *
	 * This will only be called before the I2C adapter device is added.
	 *
	 * *data will be set to NULL before this is called.
	 *
	 * This callback may be NULL, in which case *data will remain NULL upon
	 * initialization.
	 *
	 * This should return -errno upon failure, 0 upon success.  All
	 * non-negative return values are currently treated as success but
	 * positive values are reserved for potential future use.
	 *
	 * Initialization failure will cause the whole I2C pseudo controller to
	 * fail to initialize or function, thus *data will not be passed to any
	 * other callbacks.
	 */
	int (*data_creator)(void **data);
	/*
	 * This is called once when shutdown of an I2C pseudo controller is
	 * imminent, and no further I2C replies can be processed.
	 *
	 * This callback may be NULL.
	 */
	void (*data_shutdown)(void *data);
	/*
	 * This is called once upon termination of each I2C pseudo controller to
	 * free any resources held by @data.
	 *
	 * This will never be called while the I2C adapter device is active.
	 * Normally that means this is called after the I2C adapter device has
	 * been deleted, but it is also possible for this to be called during
	 * I2C pseudo controller initialization if a subsequent initialization
	 * step failed, as part of failure handling cleanup.
	 *
	 * This will only be called after a successful return value from
	 * data_creator().
	 *
	 * This will be passed the same *data pointer that data_creator() placed
	 * in its **data output arg.
	 *
	 * The *data pointer will not be used again by the write command system
	 * after the start of this function call.
	 *
	 * This callback may be NULL.
	 */
	void (*data_destroyer)(void *data);
	/*
	 * This is called to process write command header fields, including the
	 * command name itself as the first header field in every command.
	 *
	 * This is called once for each header field, in order, including the
	 * initial command name.
	 *
	 * @data is the value of *data from data_creator().  (Thus NULL if
	 * data_creator field is NULL.)
	 *
	 * @in and @in_size are the header value.  It will never contain
	 * i2cp_ctrlr_header_sep_char or i2cp_ctrlr_end_char.
	 *
	 * in[in_size] is guaranteed to be null.  There may be null characters
	 * inside the buffer boundary indicated by @in_size as well though!
	 *
	 * @non_blocking indicates whether O_NONBLOCK is set on the controller
	 * file descriptor.  This is not expected to be relevant to most write
	 * command callback implementations, however it should be respected if
	 * relevant.  In other words, if this is true do not block indefinitely,
	 * instead return EAGAIN or EWOULDBLOCK.  If this is false never return
	 * EAGAIN or EWOULDBLOCK.
	 *
	 * Return -errno to indicate a failure.  After a failure the next and
	 * final callback invocation for the command will be cmd_completer().
	 *
	 * Return 0 to indicate success _and_ that another header field is
	 * expected next.  The next header field will be fully buffered before
	 * being sent to this callback, just as the current one was.
	 *
	 * Return a positive value to indicate success _and_ that data is
	 * expected next.  The exact positive value sets the chunk size used to
	 * buffer the data and pass it to data_receiver.  All invocations of
	 * data_receiver are guaranteed to receive data in a _multiple_ of the
	 * chunk size, except the final invocation, because
	 * i2cp_ctrlr_end_char could be received on a non-chunk-size boundary.
	 * The return value should be less than I2CP_CTRLR_CMD_LIMIT, as that
	 * minus one is the maximum that will ever be buffered at once, and thus
	 * the maximum that will ever be sent to a single invocation of
	 * data_receiver.
	 *
	 * If the command is expected to end after a header field without any
	 * data, it is encouraged to return 1 here and have data_receiver
	 * indicate a failure if it is called.  That avoids having the
	 * unexpected input buffered unnecessarily.
	 *
	 * This callback MUST NOT be NULL.
	 */
	int (*header_receiver)(void *data, char *in, size_t in_size,
			       bool non_blocking);
	/*
	 * This is called to process write command data, when requested by the
	 * header_receiver() return value.
	 *
	 * This may be invoked multiple times for each data field, with the data
	 * broken up into sequential non-overlapping chunks.
	 *
	 * @in and @in_size are data.  The data will never contain
	 * i2cp_ctrlr_end_char.
	 *
	 * in[in_size] is guaranteed to be null.  There may be null characters
	 * inside the buffer boundary indicated by @in_size as well though!
	 *
	 * @in_size is guaranteed to be a multiple of the chunk size as
	 * specified by the last return value from header_receiver(), unless
	 * either the chunk size is >= I2CP_CTRLR_CMD_LIMIT, or
	 * i2cp_ctrlr_end_char was reached on a non-chunk-sized boundary.
	 *
	 * @in_size is guaranteed to be greater than zero, and less than
	 * I2CP_CTRLR_CMD_LIMIT.
	 *
	 * @non_blocking indicates whether O_NONBLOCK is set on the controller
	 * file descriptor.  This is not expected to be relevant to most write
	 * command callback implementations, however it should be respected if
	 * relevant.  In other words, if this is true do not block indefinitely,
	 * instead return EAGAIN or EWOULDBLOCK.  If this is false never return
	 * EAGAIN or EWOULDBLOCK.
	 *
	 * This should return -errno upon failure, 0 upon success.  All
	 * non-negative return values are currently treated as success but
	 * positive values are reserved for potential future use.  After a
	 * failure the next and final callback invocation for the command will
	 * be cmd_completer().
	 *
	 * If header_receiver() never returns a positive number, this callback
	 * should be NULL.  Otherwise, this callback MUST NOT be NULL.
	 */
	int (*data_receiver)(void *data, char *in, size_t in_size,
			     bool non_blocking);
	/*
	 * This is called to complete processing of a command, after it has been
	 * received in its entirety.
	 *
	 * If @receive_status is positive, it is an error code from the invoking
	 * routines themselves, e.g. if the controller process wrote a header
	 * field >= I2CP_CTRLR_CMD_LIMIT.
	 *
	 * If @receive_status is zero, it means all invocations of
	 * header_receiver and data_receiver returned successful values and the
	 * entire write command was received successfully.
	 *
	 * If @receive_status is negative, it is the value returned by the last
	 * header_receiver or data_receiver invocation.
	 *
	 * @non_blocking indicates whether O_NONBLOCK is set on the controller
	 * file descriptor.  This is not expected to be relevant to most write
	 * command callback implementations, however it should be respected if
	 * relevant.  In other words, if this is true do not block indefinitely,
	 * instead return EAGAIN or EWOULDBLOCK.  If this is false never return
	 * EAGAIN or EWOULDBLOCK.
	 *
	 * This is called exactly once for each write command.  This is true
	 * regardless of the value of @non_blocking and regardless of the return
	 * value of this function, so it is imperative that this function
	 * perform any necessary cleanup tasks related to @data, even if
	 * non_blocking=true and blocking is required!
	 *
	 * Thus, even with non_blocking=true, it would only ever make sense to
	 * return -EAGAIN from this function if the struct i2cp_cmd
	 * implementation is able to perform the would-be blocked cmd_completer
	 * operation later, e.g. upon invocation of a callback for the next
	 * write command, or by way of a background thread.
	 *
	 * This should return -errno upon failure, 0 upon success.  All
	 * non-negative return values are currently treated as success but
	 * positive values are reserved for potential future use.
	 *
	 * An error should be returned only to indicate a new error that
	 * happened during the execution of this callback.  Any error from
	 * @receive_status should *not* be copied to the return value of this
	 * callback.
	 *
	 * This callback may be NULL.
	 */
	int (*cmd_completer)(void *data, struct i2cp_controller *pdata,
			     int receive_status, bool non_blocking);
};

/*
 * These are indexes of i2cp_cmds[].  Every element in that array should have a
 * corresponding value in this enum, and the enum value should be used in the
 * i2cp_cmds[] initializer.
 *
 * Command names are matched in this order, so sort by expected frequency.
 */
enum {
	I2CP_CMD_MXFER_REPLY_IDX = 0,
	I2CP_CMD_ADAP_START_IDX,
	I2CP_CMD_ADAP_SHUTDOWN_IDX,
	I2CP_CMD_GET_NUMBER_IDX,
	I2CP_CMD_GET_PSEUDO_ID_IDX,
	I2CP_CMD_SET_NAME_SUFFIX_IDX,
	I2CP_CMD_SET_TIMEOUT_IDX,
	/* Keep this at the end! This must equal ARRAY_SIZE(i2cp_cmds). */
	I2CP_NUM_WRITE_CMDS,
};

/*
 * All values must be >= 0.  This should not contain any error values.
 *
 * The state for a new controller must have a zero value, so that
 * zero-initialized memory results in the correct default value.
 */
enum i2cp_ctrlr_state {
	/*
	 * i2c_add_adapter() has not been called yet, or has only returned
	 * failure.
	 */
	I2CP_CTRLR_STATE_NEW = 0,
	/*
	 * i2c_add_adapter() has return success, and the controller has not
	 * requested shutdown yet.
	 */
	I2CP_CTRLR_STATE_RUNNING,
	/*
	 * i2c_add_adapter() has returned success, and the controller has
	 * requested shutdown.
	 *
	 * Note that it is perfectly acceptable for a pseudo controller fd to be
	 * closed and released without shutdown having been requested
	 * beforehand.  Thus, this state is purely optional in the lifetime of a
	 * controller.
	 */
	I2CP_CTRLR_STATE_SHUTDN_REQ,
};

/*
 * Avoid allocating this struct on the stack, it contains a large buffer as a
 * direct member.
 *
 * To avoid deadlocks, never attempt to hold more than one of the locks in this
 * structure at once, with the following exceptions:
 *   - It is permissible to acquire read_rsp_queue_lock while holding cmd_lock.
 *   - It is permissible to acquire read_rsp_queue_lock while holding rsp_lock.
 */
struct i2cp_controller {
	unsigned int index;
	/*
	 * Never modify the ID after initialization.
	 *
	 * This should be an unsigned integer type large enough to hold
	 * I2CP_ADAPTERS_MAX.
	 */
	unsigned int id;
	/*
	 * Only i2cp_cdev_open() and i2cp_cdev_release() may access this field.
	 * Other functions called by them, or called by the I2C subsystem, may
	 * of course take a reference to this same struct i2c_adapter.  However
	 * no other functions besides the aforementioned two may access the
	 * i2c_adapter field of struct i2cp_controller.
	 */
	struct i2c_adapter i2c_adapter;

	struct mutex startstop_lock;
	enum i2cp_ctrlr_state startstop_state;

	wait_queue_head_t poll_wait_queue;

	/* This must be held while read or writing cmd_* fields. */
	struct mutex cmd_lock;
	/*
	 * This becomes the @receive_status arg to struct i2cp_cmd.cmd_completer
	 * callback.
	 *
	 * A negative value is an error number from
	 * struct i2cp_cmd.header_receiver or struct i2cp_cmd.data_receiver.
	 *
	 * A zero value means no error has occurred so far in processing the
	 * current write reply command.
	 *
	 * A positive value is an error number from a non-command-specific part
	 * of write command processing, e.g. from the
	 * struct file_operations.write callback itself, or function further up
	 * its call stack that is not specific to any particular write command.
	 */
	int cmd_receive_status;
	/*
	 * Index of i2cp_cmds[] and .cmd_data[] plus one, i.e. value of 1 means
	 * 0 index.  Value of 0 (zero) means the controller is waiting for a new
	 * command.
	 */
	int cmd_idx_plus_one;
	int cmd_data_increment;
	size_t cmd_size;
	/* Add one for trailing null character. */
	char cmd_buf[I2CP_CTRLR_CMD_LIMIT + 1];
	void *cmd_data[I2CP_NUM_WRITE_CMDS];

	struct completion read_rsp_queued;
	/* This must be held while read or writing read_rsp_queue_* fields. */
	struct mutex read_rsp_queue_lock;
	/*
	 * This is a FIFO queue of struct i2cp_rsp.queue .
	 *
	 * This MUST be strictly used as FIFO.  Only consume or pop the first
	 * item.  Only append to the end.  Users of this queue assume this FIFO
	 * behavior is strictly followed, and their uses of read_rsp_queue_lock
	 * would not be safe otherwise.
	 */
	struct list_head read_rsp_queue_head;
	unsigned int read_rsp_queue_length;

	/* This must be held while reading or writing rsp_* fields. */
	struct mutex rsp_lock;
	bool rsp_invalidated;
	/*
	 * Holds formatted string from most recently popped item of
	 * read_rsp_queue_head if it was not wholly consumed by the last
	 * controller read.
	 */
	char *rsp_buf_start;
	char *rsp_buf_pos;
	ssize_t rsp_buf_remaining;
};

struct i2cp_cmd_mxfer_reply {
	/*
	 * This lock MUST be held while reading or modifying any part of this
	 * struct i2cp_cmd_mxfer_reply, unless you can guarantee that nothing
	 * else can access this struct concurrently, such as during
	 * initialization.
	 *
	 * The struct i2cp_cmd_mxfer_reply_data.reply_queue_lock of the
	 * struct i2cp_cmd_mxfer_reply_data.reply_queue_head list which contains
	 * this struct i2cp_cmd_mxfer_reply.reply_queue_item MUST be held when
	 * attempting to acquire this lock.
	 *
	 * It is NOT required to keep
	 * struct i2cp_cmd_mxfer_reply_data.reply_queue_lock held after
	 * acquisition of this lock (unless also manipulating
	 * struct i2cp_cmd_mxfer_reply_data.reply_queue_* of course).
	 */
	struct mutex lock;

	/*
	 * Never modify the ID after initialization.
	 *
	 * This should be an unsigned integer type large enough to hold
	 * I2CP_CTRLR_RSP_QUEUE_LIMIT.  If changing this type, audit for printf
	 * format strings that need updating!
	 */
	unsigned int id;
	/* Number of I2C messages successfully processed, or negative error. */
	int ret;
	/* Same type as struct i2c_algorithm.master_xfer @num arg. */
	int num_msgs;
	/* Same type as struct i2c_algorithm.master_xfer @msgs arg. */
	struct i2c_msg *msgs;
	/* Same length (not size) as *msgs array. */
	bool *completed;
	/* Number of completed[] array entries with true value. */
	int num_completed_true;

	/*
	 * This is for use in struct i2cp_cmd_mxfer_reply_data.reply_queue_head
	 * FIFO queue.
	 *
	 * Any time this is deleted from its containing
	 * struct i2cp_cmd_mxfer_reply_data.reply_queue_head list, either
	 * list_del_init() MUST be used (not list_del()), OR this whole
	 * struct i2cp_cmd_mxfer_reply MUST be freed.
	 *
	 * That way, if this struct is not immediately freed, the code which
	 * eventually frees it can test whether it still needs to be deleted
	 * from struct i2cp_cmd_mxfer_reply_data.reply_queue_head by using
	 * list_empty() on reply_queue_item.  (Calling list_del() on an
	 * already-deleted list item is unsafe.)
	 */
	struct list_head reply_queue_item;
	struct completion data_filled;
};

/*
 * The state for receiving the first field must have a zero value, so that
 * zero-initialized memory results in the correct default value.
 */
enum i2cp_cmd_mxfer_reply_state {
	I2CP_CMD_MXFER_REPLY_STATE_CMD_NEXT = 0,
	I2CP_CMD_MXFER_REPLY_STATE_ID_NEXT,
	I2CP_CMD_MXFER_REPLY_STATE_INDEX_NEXT,
	I2CP_CMD_MXFER_REPLY_STATE_ADDR_NEXT,
	I2CP_CMD_MXFER_REPLY_STATE_FLAGS_NEXT,
	I2CP_CMD_MXFER_REPLY_STATE_ERRNO_NEXT,
	I2CP_CMD_MXFER_REPLY_STATE_DATA_NEXT,
	/*
	 * This is used to tell subsequent callback invocations that the write
	 * command currently being received is invalid, when the receiver wants
	 * to quietly discard the write command instead of loudly returning an
	 * error.
	 */
	I2CP_CMD_MXFER_REPLY_STATE_INVALID,
};

struct i2cp_cmd_mxfer_reply_data {
	/* This must be held while read or writing reply_queue_* fields. */
	struct mutex reply_queue_lock;
	/*
	 * This is used to make a strong attempt at avoiding ID reuse,
	 * especially for overlapping master_xfer() calls.
	 *
	 * This can wrap by design, and thus makes no perfect guarantees over
	 * the lifetime of an I2C pseudo adapter.
	 *
	 * No code should assume uniqueness, not even for master_xfer() calls of
	 * overlapping lifetimes.  When the controller writes a master_xfer()
	 * reply command, assume that it is for the oldest outstanding instance
	 * of the ID number specified.
	 */
	/* Same type as struct i2cp_cmd_mxfer_reply.id field. */
	unsigned int next_mxfer_id;
	/*
	 * This is a FIFO queue of struct i2cp_cmd_mxfer_reply.reply_queue_item.
	 *
	 * This MUST be strictly used as FIFO.  Only consume or pop the first
	 * item.  Only append to the end.  Users of this queue assume this FIFO
	 * behavior is strictly followed, and their uses of reply_queue_lock may
	 * not be safe otherwise.
	 */
	struct list_head reply_queue_head;
	unsigned int reply_queue_length;
	struct i2cp_cmd_mxfer_reply *reply_queue_current_item;

	enum i2cp_cmd_mxfer_reply_state state;

	/* Same type as struct i2cp_cmd_mxfer_reply.id field. */
	unsigned int current_id;
	/* Same type as struct i2c_msg.addr field. */
	u16 current_addr;
	/* Same type as struct i2c_msg.flags field. */
	u16 current_flags;
	/* Same type as struct i2c_algorithm.master_xfer @num arg. */
	int current_msg_idx;
	/* Same type as struct i2c_msg.len field. */
	u16 current_buf_idx;
};

struct i2cp_cmd_set_name_suffix_data {
	char name_suffix[sizeof_field(struct i2c_adapter, name)];
	size_t name_suffix_len;
};

struct i2cp_cmd_set_timeout_data {
	int field_pos;
	unsigned int timeout_ms;
};

struct i2cp_rsp {
	/*
	 * This callback is invoked to format its associated data for passing to
	 * the userspace controller process when it read()s the I2C pseudo
	 * controller character device.
	 *
	 * @data will be the data pointer from this struct instance.
	 *
	 * @out is an output argument.  Upon positive return value, *out must be
	 * set to a buffer which the caller will take ownership of, and which
	 * can be freed with kfree().
	 *
	 * Upon positive return value, @data must NOT be freed.
	 *
	 * The formatter will be called repeatedly for the same data until it
	 * returns non-positive.
	 *
	 * Upon non-positive return value, *out should not be modified.
	 *
	 * Upon non-positive return value, the formatter should have freed data
	 * with kfree().  Implicitly this means any allocations owned by *data
	 * should have been freed by the formatter as well.
	 *
	 * A negative return value indicates an error occurred and the data
	 * cannot be formatted successfully.  The error code may or may not
	 * eventually be propagated back to the I2C pseudo adapter controller.
	 *
	 * A positive return value is the number of characters/bytes to use from
	 * the *out buffer, always starting from index 0.  It should NOT include
	 * a trailing NULL character unless that character should be propagated
	 * to the I2C pseudo adapter controller!  It therefore does NOT need to
	 * be the full size of the allocated *out buffer, instead it can be
	 * less.  (The size is not needed by kfree().)
	 *
	 * The formatter owns the memory pointed to by data.  The invoking code
	 * will never mutate or free data.  Thus, upon non-positive return value
	 * from the formatter, the formatter must have already performed any
	 * reference counting decrement or memory freeing necessary to ensure
	 * data does not live beyond its final use.
	 *
	 * There will never be more than one formatter callback in flight at
	 * once for a given I2C pseudo controller.  This is true even in the
	 * face of concurrent reads by the controller.
	 *
	 * The formatter must NOT use i2cp_ctrlr_end_char in anywhere in *out
	 * (within the size range indicated by the return value; past that does
	 * not matter).  The i2cp_ctrlr_end_char will be added automatically by
	 * the caller after a zero return value (successful completion) from the
	 * formatter.
	 *
	 * The formatter must never create or return a buffer larger than
	 * I2CP_MAX_MSG_BUF_SIZE.  The formatter is encouraged to avoid that by
	 * generating and returning the output in chunks, taking advantage of
	 * the guarantee that it will be called repeatedly until  exhaustion
	 * (zero return value) or failure (negative return value).  If the
	 * formatter expects its formatted output or natural subsets of it to
	 * always fit within I2CP_MAX_MSG_BUF_SIZE, and it is called with input
	 * data not meeting that expectation, the formatter should return
	 * -ERANGE to indicate this condition.
	 */
	ssize_t (*formatter)(void *data, char **out);
	void *data;

	struct list_head queue;
};

struct i2cp_rsp_buffer {
	char *buf;
	ssize_t size;
};

struct i2cp_rsp_master_xfer {
	/* Never modify the ID after initialization. */
	/* Same type as struct i2cp_cmd_mxfer_reply.id field. */
	unsigned int id;

	/* These types match those of struct i2c_algorithm.master_xfer args. */
	struct i2c_msg *msgs;
	int num;

	/*
	 * Always initialize fields below here to zero.  They are for internal
	 * use by i2cp_rsp_master_xfer_formatter().
	 */
	int num_msgs_done; /* type of @num field */
	size_t buf_start_plus_one;
};

/* vanprintf - See anprintf() documentation. */
static ssize_t vanprintf(char **out, ssize_t max_size, gfp_t gfp,
			 const char *fmt, va_list ap)
{
	int ret;
	ssize_t buf_size;
	char *buf = NULL;
	va_list args1;

	va_copy(args1, ap);
	ret = vsnprintf(NULL, 0, fmt, ap);
	if (ret < 0)
		goto fail_before_args1;
	if (max_size >= 0 && ret > max_size) {
		ret = -ERANGE;
		goto fail_before_args1;
	}

	buf_size = ret + 1;
	buf = kmalloc(buf_size, gfp);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto fail_before_args1;
	}

	ret = vsnprintf(buf, buf_size, fmt, args1);
	va_end(args1);
	if (ret < 0)
		goto fail_after_args1;
	if (ret + 1 != buf_size) {
		ret = -ENOTRECOVERABLE;
		goto fail_after_args1;
	}

	*out = buf;
	return ret;

fail_before_args1:
	va_end(args1);
fail_after_args1:
	kfree(buf);
	if (ret >= 0)
		ret = -ENOTRECOVERABLE;
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

static ssize_t i2cp_rsp_buffer_formatter(void *data, char **out)
{
	struct i2cp_rsp_buffer *rsp_buf;

	rsp_buf = data;
	if (rsp_buf->buf) {
		if (rsp_buf->size > 0) {
			*out = rsp_buf->buf;
			rsp_buf->buf = NULL;
			return rsp_buf->size;
		}
		kfree(rsp_buf->buf);
	}
	kfree(rsp_buf);
	return 0;
}

static ssize_t i2cp_rsp_master_xfer_formatter(void *data, char **out)
{
	ssize_t ret;
	size_t i, buf_size, byte_start, byte_limit;
	char *buf_start, *buf_pos;
	struct i2cp_rsp_master_xfer *mxfer_rsp;
	struct i2c_msg *i2c_msg;

	mxfer_rsp = data;

	/*
	 * This condition is set by a previous call to this function with the
	 * same data, when it returned an error but was not consuming the final
	 * i2c_msg.
	 */
	if (!mxfer_rsp->msgs) {
		++mxfer_rsp->num_msgs_done;
		ret = 0;
		goto maybe_free;
	}

	i2c_msg = &mxfer_rsp->msgs[mxfer_rsp->num_msgs_done];

	/*
	 * If this is a read, or if this is a write and we've finished writing
	 * the data buffer, we are done with this i2c_msg.
	 */
	if (mxfer_rsp->buf_start_plus_one >= 1 &&
	    (i2c_msg->flags & I2C_M_RD ||
	     mxfer_rsp->buf_start_plus_one >= (size_t)i2c_msg->len + 1)) {
		++mxfer_rsp->num_msgs_done;
		mxfer_rsp->buf_start_plus_one = 0;
		ret = 0;
		goto maybe_free;
	}

	if (mxfer_rsp->buf_start_plus_one <= 0) {
		/*
		 * The length is not strictly necessary with the explicit
		 * end-of-message marker (i2cp_ctrlr_end_char), however it
		 * serves as a useful validity check for controllers to verify
		 * that no bytes were lost in kernel->userspace transmission.
		 */
		ret = anprintf(&buf_start, I2CP_MAX_MSG_BUF_SIZE, GFP_KERNEL,
			       "%*s%c%u%c%d%c0x%04X%c0x%04X%c%u",
			       (int)strlen(I2CP_MXFER_REQ_CMD),
			       I2CP_MXFER_REQ_CMD, i2cp_ctrlr_header_sep_char,
			       mxfer_rsp->id, i2cp_ctrlr_header_sep_char,
			       mxfer_rsp->num_msgs_done,
			       i2cp_ctrlr_header_sep_char, i2c_msg->addr,
			       i2cp_ctrlr_header_sep_char, i2c_msg->flags,
			       i2cp_ctrlr_header_sep_char, i2c_msg->len);
		if (ret > 0) {
			*out = buf_start;
			mxfer_rsp->buf_start_plus_one = 1;
			/*
			 * If we have a zero return value, it means the output
			 * buffer was allocated as size one, containing only a
			 * terminating null character.  This would be a bug
			 * given the requested format string above.  Also,
			 * formatter functions must not mutate *out when
			 * returning zero.  So if this matches, free the useless
			 * buffer and return an error.
			 */
		} else if (ret == 0) {
			ret = -EINVAL;
			kfree(buf_start);
		}
		goto maybe_free;
	}

	byte_start = mxfer_rsp->buf_start_plus_one - 1;
	byte_limit = min_t(size_t, i2c_msg->len - byte_start,
			   I2CP_MAX_MSG_BUF_SIZE / 3);
	/* 3 chars per byte == 2 chars for hex + 1 char for separator */
	buf_size = byte_limit * 3;

	buf_start = kzalloc(buf_size, GFP_KERNEL);
	if (!buf_start) {
		ret = -ENOMEM;
		goto maybe_free;
	}

	for (buf_pos = buf_start, i = 0; i < byte_limit; ++i) {
		*buf_pos++ = (i || byte_start) ? i2cp_ctrlr_data_sep_char :
						 i2cp_ctrlr_header_sep_char;
		buf_pos = hex_byte_pack_upper(buf_pos,
					      i2c_msg->buf[byte_start + i]);
	}
	*out = buf_start;
	ret = buf_size;
	mxfer_rsp->buf_start_plus_one += i;

maybe_free:
	if (ret <= 0) {
		if (mxfer_rsp->num_msgs_done >= mxfer_rsp->num) {
			kfree(mxfer_rsp->msgs);
			kfree(mxfer_rsp);
			/*
			 * If we are returning an error but have not consumed
			 * all of mxfer_rsp yet, we must not attempt to output
			 * any more I2C messages from the same mxfer_rsp.
			 * Setting mxfer_rsp->msgs to NULL tells the remaining
			 * invocations with this mxfer_rsp to output nothing.
			 *
			 * There can be more invocations with the same mxfer_rsp
			 * even after returning an error here because
			 * i2cp_adapter_master_xfer() reuses a single
			 * struct i2cp_rsp_master_xfer (mxfer_rsp) across
			 * multiple struct i2cp_rsp (rsp_wrappers), one for each
			 * struct i2c_msg within the mxfer_rsp.
			 */
		} else if (ret < 0) {
			kfree(mxfer_rsp->msgs);
			mxfer_rsp->msgs = NULL;
		}
	}
	return ret;
}

static ssize_t i2cp_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int ret;
	struct i2c_adapter *adap;
	struct i2cp_controller *pdata;

	adap = container_of(dev, struct i2c_adapter, dev);
	pdata = container_of(adap, struct i2cp_controller, i2c_adapter);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", pdata->id);
	if (ret >= PAGE_SIZE)
		return -ERANGE;
	return ret;
}

static const struct device_attribute i2cp_id_dev_attr = {
	.attr = {
		.name = "i2c-pseudo-id",
		.mode = 0444,
	},
	.show = i2cp_id_show,
};

static enum i2cp_ctrlr_state i2cp_adap_get_state(struct i2cp_controller *pdata)
{
	enum i2cp_ctrlr_state ret;

	mutex_lock(&pdata->startstop_lock);
	ret = pdata->startstop_state;
	mutex_unlock(&pdata->startstop_lock);
	return ret;
}

static int i2cp_cmd_mxfer_reply_data_creator(void **data)
{
	struct i2cp_cmd_mxfer_reply_data *cmd_data;

	cmd_data = kzalloc(sizeof(*cmd_data), GFP_KERNEL);
	if (!cmd_data)
		return -ENOMEM;
	mutex_init(&cmd_data->reply_queue_lock);
	INIT_LIST_HEAD(&cmd_data->reply_queue_head);
	*data = cmd_data;
	return 0;
}

/*
 * Notify pending I2C requests of the shutdown.  There is no possibility of
 * further I2C replies at this point.  This stops the I2C requests from waiting
 * for the adapter timeout, which could have been set arbitrarily long by the
 * userspace controller.
 */
static void i2cp_cmd_mxfer_reply_data_shutdown(void *data)
{
	struct list_head *list_ptr;
	struct i2cp_cmd_mxfer_reply_data *cmd_data;
	struct i2cp_cmd_mxfer_reply *mxfer_reply;

	cmd_data = data;
	mutex_lock(&cmd_data->reply_queue_lock);
	list_for_each(list_ptr, &cmd_data->reply_queue_head)
	{
		mxfer_reply = list_entry(list_ptr, struct i2cp_cmd_mxfer_reply,
					 reply_queue_item);
		mutex_lock(&mxfer_reply->lock);
		complete_all(&mxfer_reply->data_filled);
		mutex_unlock(&mxfer_reply->lock);
	}
	mutex_unlock(&cmd_data->reply_queue_lock);
}

static void i2cp_cmd_mxfer_reply_data_destroyer(void *data)
{
	/*
	 * We do not have to worry about racing with in-flight I2C messages
	 * because data_destroyer callbacks are guaranteed to never be called
	 * while the I2C adapter device is active.
	 */
	kfree(data);
}

static inline bool
i2cp_mxfer_reply_is_current(struct i2cp_cmd_mxfer_reply_data *cmd_data,
			    struct i2cp_cmd_mxfer_reply *mxfer_reply)
{
	int i;

	i = cmd_data->current_msg_idx;
	return cmd_data->current_id == mxfer_reply->id && i >= 0 &&
	       i < mxfer_reply->num_msgs &&
	       cmd_data->current_addr == mxfer_reply->msgs[i].addr &&
	       cmd_data->current_flags == mxfer_reply->msgs[i].flags;
}

/* cmd_data->reply_queue_lock must be held. */
static inline struct i2cp_cmd_mxfer_reply *
i2cp_mxfer_reply_find_current(struct i2cp_cmd_mxfer_reply_data *cmd_data)
{
	struct list_head *list_ptr;
	struct i2cp_cmd_mxfer_reply *mxfer_reply;

	list_for_each(list_ptr, &cmd_data->reply_queue_head)
	{
		mxfer_reply = list_entry(list_ptr, struct i2cp_cmd_mxfer_reply,
					 reply_queue_item);
		if (i2cp_mxfer_reply_is_current(cmd_data, mxfer_reply))
			return mxfer_reply;
	}
	return NULL;
}

/* cmd_data->reply_queue_lock must NOT already be held. */
static inline void
i2cp_mxfer_reply_update_current(struct i2cp_cmd_mxfer_reply_data *cmd_data)
{
	mutex_lock(&cmd_data->reply_queue_lock);
	cmd_data->reply_queue_current_item =
		i2cp_mxfer_reply_find_current(cmd_data);
	mutex_unlock(&cmd_data->reply_queue_lock);
}

static int i2cp_cmd_mxfer_reply_header_receiver(void *data, char *in,
						size_t in_size,
						bool non_blocking)
{
	int ret, reply_errno = 0;
	struct i2cp_cmd_mxfer_reply_data *cmd_data;

	cmd_data = data;

	switch (cmd_data->state) {
	case I2CP_CMD_MXFER_REPLY_STATE_CMD_NEXT:
		/* Expect the msg/reply ID header field next. */
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_ID_NEXT;
		return 0;
	case I2CP_CMD_MXFER_REPLY_STATE_ID_NEXT:
	case I2CP_CMD_MXFER_REPLY_STATE_INDEX_NEXT:
	case I2CP_CMD_MXFER_REPLY_STATE_ADDR_NEXT:
	case I2CP_CMD_MXFER_REPLY_STATE_FLAGS_NEXT:
	case I2CP_CMD_MXFER_REPLY_STATE_ERRNO_NEXT:
		break;
	default:
		/* Reaching here is a bug. */
		/*
		 * Testing this before checking for null characters ensures the
		 * correct error is indicated.
		 */
		return -EINVAL;
	}

	/*
	 * The command name is logically outside the control of this function,
	 * and may contain null characters, even if that would be nonsensical.
	 * Thus it is handled above, followed by this check, and below here
	 * the rest of the header fields are handled.  Some of them use
	 * functions that could mishandle input which contains nulls.  An actual
	 * error would be okay, however if the input were consumed incorrectly
	 * without an error, that could lead to subtle bugs.
	 */
	if (memchr(in, '\0', in_size))
		return -EPROTO;

	switch (cmd_data->state) {
	case I2CP_CMD_MXFER_REPLY_STATE_ID_NEXT:
		ret = kstrtouint(in, 0, &cmd_data->current_id);
		if (ret < 0)
			return ret;
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_INDEX_NEXT;
		return 0;
	case I2CP_CMD_MXFER_REPLY_STATE_INDEX_NEXT:
		ret = kstrtoint(in, 0, &cmd_data->current_msg_idx);
		if (ret < 0)
			return ret;
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_ADDR_NEXT;
		return 0;
	case I2CP_CMD_MXFER_REPLY_STATE_ADDR_NEXT:
		ret = kstrtou16(in, 0, &cmd_data->current_addr);
		if (ret < 0)
			return ret;
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_FLAGS_NEXT;
		return 0;
	case I2CP_CMD_MXFER_REPLY_STATE_FLAGS_NEXT:
		ret = kstrtou16(in, 0, &cmd_data->current_flags);
		if (ret < 0)
			return ret;
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_ERRNO_NEXT;
		return 0;
	case I2CP_CMD_MXFER_REPLY_STATE_ERRNO_NEXT:
		ret = kstrtoint(in, 0, &reply_errno);
		if (ret < 0)
			return ret;
		break;
	default:
		/* Reaching here is a bug. */
		return -EINVAL;
	}

	/*
	 * Only I2CP_CMD_MXFER_REPLY_STATE_ERRNO_NEXT can reach this point.
	 * Now that we've received all of the headers, find the matching
	 * mxfer_reply.
	 */
	i2cp_mxfer_reply_update_current(cmd_data);

	if (reply_errno || !cmd_data->reply_queue_current_item) {
		/*
		 * reply_errno:
		 * Drop the specific errno for now.  The Linux I2C API
		 * does not provide a way to return an errno for a
		 * specific message within a master_xfer() call.  The
		 * cmd_completer callback will indicate this
		 * controller-reported failure by not incrementing
		 * mxfer_reply->ret for this I2C msg reply.
		 *
		 * cmd_data->reply_queue_current_item == NULL:
		 * No matching mxfer_reply was found.  Discard any
		 * further input in this command.  The cmd_completer
		 * callback will indicate this failure to the
		 * controller.
		 */
		cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_INVALID;
		/*
		 * Ask for data bytes in multiples of 1, i.e. no
		 * boundary requirements, because the we're just going
		 * to discard it.  The next field could even be a header
		 * instead of data, but it doesn't matter, we're going
		 * to continue discarding the write input until the end
		 * of this write command.
		 */
		return 1;
	}

	cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_DATA_NEXT;
	/*
	 * Ask for data bytes in multiples of 3.  Expected format is
	 * hexadecimal NN:NN:... e.g. "3C:05:F1:01" is a possible 4 byte
	 * data value.
	 */
	return 3;
}

static int i2cp_cmd_mxfer_reply_data_receiver(void *data, char *in,
					      size_t in_size, bool non_blocking)
{
	int ret;
	char u8_hex[3] = { 0 };
	struct i2cp_cmd_mxfer_reply_data *cmd_data;
	struct i2cp_cmd_mxfer_reply *mxfer_reply;
	struct i2c_msg *i2c_msg;

	cmd_data = data;

	if (cmd_data->state == I2CP_CMD_MXFER_REPLY_STATE_INVALID)
		return 0;
	if (cmd_data->state != I2CP_CMD_MXFER_REPLY_STATE_DATA_NEXT)
		/* Reaching here is a bug. */
		return -EINVAL;

	mutex_lock(&cmd_data->reply_queue_lock);
	mxfer_reply = cmd_data->reply_queue_current_item;
	if (!mxfer_reply) {
		/* Reaching here is a bug. */
		mutex_unlock(&cmd_data->reply_queue_lock);
		return -EINVAL;
	}
	mutex_lock(&mxfer_reply->lock);
	mutex_unlock(&cmd_data->reply_queue_lock);

	if (cmd_data->current_msg_idx < 0 ||
	    cmd_data->current_msg_idx >= mxfer_reply->num_msgs) {
		/* Reaching here is a bug. */
		ret = -EINVAL;
		goto unlock;
	}

	i2c_msg = &mxfer_reply->msgs[cmd_data->current_msg_idx];

	if (!(i2c_msg->flags & I2C_M_RD)) {
		/* The controller responded to a write with data. */
		ret = -EIO;
		goto unlock;
	}

	if (i2c_msg->flags & I2C_M_RECV_LEN) {
		/*
		 * When I2C_M_RECV_LEN is set, struct i2c_algorithm.master_xfer
		 * is expected to increment struct i2c_msg.len by the actual
		 * amount of bytes read.
		 *
		 * Given the above, an initial struct i2c_msg.len value of 0
		 * would be reasonable, since it will be incremented for each
		 * byte read.
		 *
		 * An initial value of 1 representing the expected size byte
		 * also makes sense, and appears to be common practice.
		 *
		 * We consider a larger initial value to indicate a bug in the
		 * I2C/SMBus client, because it's difficult to reconcile such a
		 * value with the documented requirement that struct i2c_msg.len
		 * be "incremented by the number of block data bytes received."
		 * Besides returning an error, our only options would be to
		 * ignore and blow away a value that was potentially meaningful
		 * to the client (e.g. if it indicates the maximum buffer size),
		 * assume the value is the buffer size or expected read size
		 * (which would conflict with the documentation), or just
		 * blindly increment it, leaving it at a value greater than the
		 * actual number of bytes we wrote to the buffer, and likely
		 * indicating a size larger than the actual buffer allocation.
		 */
		if (cmd_data->current_buf_idx == 0) {
			if (i2c_msg->len > 1) {
				ret = -EPROTO;
				goto unlock;
			}
			/*
			 * Subtract the read size byte because the in_size
			 * increment in the loop below will re-add it.
			 */
			i2c_msg->len = 0;
		}
	}

	while (in_size > 0 && cmd_data->current_buf_idx < i2c_msg->len) {
		if (in_size < 2 ||
		    (in_size > 2 && in[2] != i2cp_ctrlr_data_sep_char) ||
		    memchr(in, '\0', 2)) {
			/*
			 * Reaching here is a bug in the userspace I2C pseudo
			 * adapter controller.  (Or possibly a bug in this
			 * module itself, of course.)
			 */
			ret = -EIO;
			goto unlock;
		}
		/*
		 * When using I2C_M_RECV_LEN, the buffer is required to be able
		 * to hold:
		 *
		 * I2C_SMBUS_BLOCK_MAX
		 * +1 byte for the read size (first byte)
		 * +1 byte for the optional PEC byte (last byte if present).
		 *
		 * If reading the next byte would exceed that, return EPROTO
		 * error per Documentation/i2c/fault-codes .
		 */
		if (i2c_msg->flags & I2C_M_RECV_LEN &&
		    i2c_msg->len >= I2C_SMBUS_BLOCK_MAX + 2) {
			ret = -EPROTO;
			goto unlock;
		}
		/* Use u8_hex to get a terminating null byte for kstrtou8(). */
		memcpy(u8_hex, in, 2);
		/*
		 * TODO: Do we need to do anything different based on the
		 * I2C_M_DMA_SAFE bit? Do we ever need to use copy_to_user()?
		 */
		ret = kstrtou8(u8_hex, 16,
			       &i2c_msg->buf[cmd_data->current_buf_idx]);
		if (ret < 0)
			goto unlock;
		if (i2c_msg->flags & I2C_M_RECV_LEN)
			++i2c_msg->len;
		++cmd_data->current_buf_idx;
		in += min_t(size_t, 3, in_size);
		in_size -= min_t(size_t, 3, in_size);
	}

	/* Quietly ignore any bytes beyond the buffer size. */
	ret = 0;

unlock:
	mutex_unlock(&mxfer_reply->lock);
	return ret;
}

static int i2cp_cmd_mxfer_reply_cmd_completer(void *data,
					      struct i2cp_controller *pdata,
					      int receive_status,
					      bool non_blocking)
{
	int ret;
	struct i2cp_cmd_mxfer_reply_data *cmd_data;
	struct i2cp_cmd_mxfer_reply *mxfer_reply;
	struct i2c_msg *i2c_msg;

	cmd_data = data;
	mutex_lock(&cmd_data->reply_queue_lock);

	mxfer_reply = cmd_data->reply_queue_current_item;
	if (!mxfer_reply) {
		mutex_unlock(&cmd_data->reply_queue_lock);
		ret = -EIO;
		goto reset_cmd_data;
	}

	mutex_lock(&mxfer_reply->lock);

	if (mxfer_reply->completed[cmd_data->current_msg_idx]) {
		/* We already received a reply for this msg. */
		mutex_unlock(&cmd_data->reply_queue_lock);
		mutex_unlock(&mxfer_reply->lock);
		ret = -EIO;
		goto reset_cmd_data;
	}

	mxfer_reply->completed[cmd_data->current_msg_idx] = true;
	if (++mxfer_reply->num_completed_true >= mxfer_reply->num_msgs) {
		list_del_init(&mxfer_reply->reply_queue_item);
		--cmd_data->reply_queue_length;
		cmd_data->reply_queue_current_item = NULL;
		complete_all(&mxfer_reply->data_filled);
	}

	mutex_unlock(&cmd_data->reply_queue_lock);
	i2c_msg = &mxfer_reply->msgs[cmd_data->current_msg_idx];

	if (!receive_status &&
	    cmd_data->state == I2CP_CMD_MXFER_REPLY_STATE_DATA_NEXT &&
	    (!(i2c_msg->flags & I2C_M_RD) ||
	     cmd_data->current_buf_idx >= i2c_msg->len))
		++mxfer_reply->ret;

	mutex_unlock(&mxfer_reply->lock);
	ret = 0;

reset_cmd_data:
	cmd_data->state = I2CP_CMD_MXFER_REPLY_STATE_CMD_NEXT;
	cmd_data->current_id = 0;
	cmd_data->current_addr = 0;
	cmd_data->current_flags = 0;
	cmd_data->current_msg_idx = 0;
	cmd_data->current_buf_idx = 0;
	return ret;
}

static int i2cp_cmd_adap_start_header_receiver(void *data, char *in,
					       size_t in_size,
					       bool non_blocking)
{
	/*
	 * No more header fields or data are expected.  This directs any further
	 * input in this command to the data_receiver, which for this write
	 * command will unconditionally indicate a controller error.
	 */
	return 1;
}

static int i2cp_cmd_adap_start_data_receiver(void *data, char *in,
					     size_t in_size, bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the command
	 * line after the initial command name.  That is unexpected and
	 * indicates a controller bug.
	 */
	return -EPROTO;
}

static int i2cp_cmd_adap_start_cmd_completer(void *data,
					     struct i2cp_controller *pdata,
					     int receive_status,
					     bool non_blocking)
{
	int ret;

	/* Refuse to start if there were errors processing this command. */
	if (receive_status)
		return 0;

	/*
	 * Acquire pdata->startstop_lock manually instead of using
	 * i2cp_adap_get_state() in order to keep the lock while calling
	 * i2c_add_adapter().
	 */
	mutex_lock(&pdata->startstop_lock);

	if (pdata->startstop_state != I2CP_CTRLR_STATE_NEW) {
		ret = -EISCONN;
		goto unlock;
	}

	/* Add the I2C adapter. */
	ret = i2c_add_adapter(&pdata->i2c_adapter);
	if (ret < 0)
		goto unlock;

	pdata->startstop_state = I2CP_CTRLR_STATE_RUNNING;

	/* Add the I2C pseudo controller ID sysfs file. */
	ret = device_create_file(&pdata->i2c_adapter.dev, &i2cp_id_dev_attr);
	if (ret < 0)
		goto unlock;

	ret = 0;

unlock:
	mutex_unlock(&pdata->startstop_lock);
	return ret;
}

static int i2cp_cmd_adap_shutdown_header_receiver(void *data, char *in,
						  size_t in_size,
						  bool non_blocking)
{
	/*
	 * No more header fields or data are expected.  This directs any further
	 * input in this command to the data_receiver, which for this write
	 * command will unconditionally indicate a controller error.
	 */
	return 1;
}

static int i2cp_cmd_adap_shutdown_data_receiver(void *data, char *in,
						size_t in_size,
						bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the command
	 * line after the initial command name.  That is unexpected and
	 * indicates a controller bug.
	 */
	return -EPROTO;
}

static int i2cp_cmd_adap_shutdown_cmd_completer(void *data,
						struct i2cp_controller *pdata,
						int receive_status,
						bool non_blocking)
{
	/* Refuse to shutdown if there were errors processing this command. */
	if (receive_status)
		return 0;

	mutex_lock(&pdata->startstop_lock);
	pdata->startstop_state = I2CP_CTRLR_STATE_SHUTDN_REQ;
	mutex_unlock(&pdata->startstop_lock);

	/* Wake up blocked controller readers. */
	complete_all(&pdata->read_rsp_queued);
	/* Wake up blocked controller pollers. */
	wake_up_interruptible_all(&pdata->poll_wait_queue);
	return 0;
}

static int i2cp_cmd_get_number_header_receiver(void *data, char *in,
					       size_t in_size,
					       bool non_blocking)
{
	/*
	 * No more header fields or data are expected.  This directs any further
	 * input in this command to the data_receiver, which for this write
	 * command will unconditionally indicate a controller error.
	 */
	return 1;
}

static int i2cp_cmd_get_number_data_receiver(void *data, char *in,
					     size_t in_size, bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the command
	 * line after the initial command name.  That is unexpected and
	 * indicates a controller bug.
	 */
	return -EPROTO;
}

static int i2cp_cmd_get_number_cmd_completer(void *data,
					     struct i2cp_controller *pdata,
					     int receive_status,
					     bool non_blocking)
{
	ssize_t ret;
	int i2c_adap_nr;
	struct i2cp_rsp_buffer *rsp_buf;
	struct i2cp_rsp *rsp_wrapper;

	/* Abort if there were errors processing this command. */
	if (receive_status)
		return 0;

	/*
	 * Check the pseudo controller startstop_state.  If it's running, get
	 * the I2C adapter number.
	 *
	 * Acquire pdata->startstop_lock manually instead of using
	 * i2cp_adap_get_state() in order to keep the lock while retrieving the
	 * I2C adapter number.
	 */
	mutex_lock(&pdata->startstop_lock);
	if (pdata->startstop_state != I2CP_CTRLR_STATE_RUNNING) {
		mutex_unlock(&pdata->startstop_lock);
		return -ENOTCONN;
	}
	i2c_adap_nr = pdata->i2c_adapter.nr;
	mutex_unlock(&pdata->startstop_lock);

	rsp_wrapper = kzalloc(sizeof(*rsp_wrapper), GFP_KERNEL);
	if (!rsp_wrapper)
		return -ENOMEM;

	rsp_buf = kzalloc(sizeof(*rsp_buf), GFP_KERNEL);
	if (!rsp_buf) {
		ret = -ENOMEM;
		goto fail_after_rsp_wrapper_alloc;
	}

	ret = anprintf(&rsp_buf->buf, I2CP_MAX_MSG_BUF_SIZE, GFP_KERNEL,
		       "%*s%c%d", (int)strlen(I2CP_NUMBER_REPLY_CMD),
		       I2CP_NUMBER_REPLY_CMD, i2cp_ctrlr_header_sep_char,
		       i2c_adap_nr);
	if (ret < 0) {
		goto fail_after_rsp_buf_alloc;
	} else if (ret == 0) {
		ret = -EINVAL;
		goto fail_after_buf_alloc;
	}
	rsp_buf->size = ret;

	rsp_wrapper->data = rsp_buf;
	rsp_wrapper->formatter = i2cp_rsp_buffer_formatter;

	mutex_lock(&pdata->read_rsp_queue_lock);
	if (pdata->read_rsp_queue_length >= I2CP_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		mutex_unlock(&pdata->read_rsp_queue_lock);
		goto fail_after_buf_alloc;
	}

	list_add_tail(&rsp_wrapper->queue, &pdata->read_rsp_queue_head);
	++pdata->read_rsp_queue_length;
	complete(&pdata->read_rsp_queued);

	mutex_unlock(&pdata->read_rsp_queue_lock);
	return 0;

fail_after_buf_alloc:
	kfree(rsp_buf->buf);
fail_after_rsp_buf_alloc:
	kfree(rsp_buf);
fail_after_rsp_wrapper_alloc:
	kfree(rsp_wrapper);
	return ret;
}

static int i2cp_cmd_get_pseudo_id_header_receiver(void *data, char *in,
						  size_t in_size,
						  bool non_blocking)
{
	/*
	 * No more header fields or data are expected.  This directs any further
	 * input in this command to the data_receiver, which for this write
	 * command will unconditionally indicate a controller error.
	 */
	return 1;
}

static int i2cp_cmd_get_pseudo_id_data_receiver(void *data, char *in,
						size_t in_size,
						bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the command
	 * line after the initial command name.  That is unexpected and
	 * indicates a controller bug.
	 */
	return -EPROTO;
}

static int i2cp_cmd_get_pseudo_id_cmd_completer(void *data,
						struct i2cp_controller *pdata,
						int receive_status,
						bool non_blocking)
{
	ssize_t ret;
	struct i2cp_rsp_buffer *rsp_buf;
	struct i2cp_rsp *rsp_wrapper;

	/* Abort if there were errors processing this command. */
	if (receive_status)
		return 0;

	rsp_wrapper = kzalloc(sizeof(*rsp_wrapper), GFP_KERNEL);
	if (!rsp_wrapper)
		return -ENOMEM;

	rsp_buf = kzalloc(sizeof(*rsp_buf), GFP_KERNEL);
	if (!rsp_buf) {
		ret = -ENOMEM;
		goto fail_after_rsp_wrapper_alloc;
	}

	ret = anprintf(&rsp_buf->buf, I2CP_MAX_MSG_BUF_SIZE, GFP_KERNEL,
		       "%*s%c%u", (int)strlen(I2CP_PSEUDO_ID_REPLY_CMD),
		       I2CP_PSEUDO_ID_REPLY_CMD, i2cp_ctrlr_header_sep_char,
		       pdata->id);
	if (ret < 0) {
		goto fail_after_rsp_buf_alloc;
	} else if (ret == 0) {
		ret = -EINVAL;
		goto fail_after_buf_alloc;
	}
	rsp_buf->size = ret;

	rsp_wrapper->data = rsp_buf;
	rsp_wrapper->formatter = i2cp_rsp_buffer_formatter;

	mutex_lock(&pdata->read_rsp_queue_lock);
	if (pdata->read_rsp_queue_length >= I2CP_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		mutex_unlock(&pdata->read_rsp_queue_lock);
		goto fail_after_buf_alloc;
	}

	list_add_tail(&rsp_wrapper->queue, &pdata->read_rsp_queue_head);
	++pdata->read_rsp_queue_length;
	complete(&pdata->read_rsp_queued);

	mutex_unlock(&pdata->read_rsp_queue_lock);
	return 0;

fail_after_buf_alloc:
	kfree(rsp_buf->buf);
fail_after_rsp_buf_alloc:
	kfree(rsp_buf);
fail_after_rsp_wrapper_alloc:
	kfree(rsp_wrapper);
	return ret;
}

static int i2cp_cmd_set_name_suffix_data_creator(void **data)
{
	struct i2cp_cmd_set_name_suffix_data *cmd_data;

	cmd_data = kzalloc(sizeof(*cmd_data), GFP_KERNEL);
	if (!cmd_data)
		return -ENOMEM;
	*data = cmd_data;
	return 0;
}

static void i2cp_cmd_set_name_suffix_data_destroyer(void *data)
{
	kfree(data);
}

static int i2cp_cmd_set_name_suffix_header_receiver(void *data, char *in,
						    size_t in_size,
						    bool non_blocking)
{
	return 1;
}

static int i2cp_cmd_set_name_suffix_data_receiver(void *data, char *in,
						  size_t in_size,
						  bool non_blocking)
{
	size_t remaining;
	struct i2cp_cmd_set_name_suffix_data *cmd_data;

	cmd_data = data;
	remaining = sizeof(cmd_data->name_suffix) - cmd_data->name_suffix_len;
	/* Quietly truncate the suffix if necessary. */
	/* The suffix may need to be further truncated later. */
	if (in_size > remaining)
		in_size = remaining;
	memcpy(&cmd_data->name_suffix[cmd_data->name_suffix_len], in, in_size);
	cmd_data->name_suffix_len += in_size;
	return 0;
}

static int i2cp_cmd_set_name_suffix_cmd_completer(void *data,
						  struct i2cp_controller *pdata,
						  int receive_status,
						  bool non_blocking)
{
	int ret;
	struct i2cp_cmd_set_name_suffix_data *cmd_data;

	/* Abort if there were errors processing this command. */
	if (receive_status)
		return 0;

	/*
	 * Acquire pdata->startstop_lock manually instead of using
	 * i2cp_adap_get_state() in order to keep the lock while
	 * setting the I2C adapter name.
	 */
	mutex_lock(&pdata->startstop_lock);

	if (pdata->startstop_state != I2CP_CTRLR_STATE_NEW) {
		ret = -EISCONN;
		goto unlock;
	}

	cmd_data = data;
	ret = snprintf(pdata->i2c_adapter.name, sizeof(pdata->i2c_adapter.name),
		       "I2C pseudo ID %u %*s", pdata->id,
		       (int)cmd_data->name_suffix_len, cmd_data->name_suffix);
	if (ret < 0)
		goto unlock;

	ret = 0;

unlock:
	mutex_unlock(&pdata->startstop_lock);
	return ret;
}

static int i2cp_cmd_set_timeout_data_creator(void **data)
{
	struct i2cp_cmd_set_timeout_data *cmd_data;

	cmd_data = kzalloc(sizeof(*cmd_data), GFP_KERNEL);
	if (!cmd_data)
		return -ENOMEM;
	*data = cmd_data;
	return 0;
}

static void i2cp_cmd_set_timeout_data_destroyer(void *data)
{
	kfree(data);
}

static int i2cp_cmd_set_timeout_header_receiver(void *data, char *in,
						size_t in_size,
						bool non_blocking)
{
	int ret;
	struct i2cp_cmd_set_timeout_data *cmd_data;

	cmd_data = data;
	switch (cmd_data->field_pos++) {
	case 0:
		return 0;
	case 1:
		ret = kstrtouint(in, 0, &cmd_data->timeout_ms);
		if (ret < 0)
			return ret;
		return 1;
	}
	/* Reaching here is a bug. */
	return -EINVAL;
}

static int i2cp_cmd_set_timeout_data_receiver(void *data, char *in,
					      size_t in_size, bool non_blocking)
{
	/*
	 * Reaching here means the controller wrote extra data in the command
	 * line.  That is unexpected and indicates a controller bug.
	 */
	return -EPROTO;
}

static int i2cp_cmd_set_timeout_cmd_completer(void *data,
					      struct i2cp_controller *pdata,
					      int receive_status,
					      bool non_blocking)
{
	int ret;
	struct i2cp_cmd_set_timeout_data *cmd_data;

	/* Abort if there were errors processing this command. */
	if (receive_status)
		return 0;

	/*
	 * Acquire pdata->startstop_lock manually instead of using
	 * i2cp_adap_get_state() in order to keep the lock while setting the
	 * I2C adapter name.
	 */
	mutex_lock(&pdata->startstop_lock);

	if (pdata->startstop_state != I2CP_CTRLR_STATE_NEW) {
		ret = -EISCONN;
		goto unlock;
	}

	cmd_data = data;
	if (cmd_data->timeout_ms < I2CP_TIMEOUT_MS_MIN ||
	    cmd_data->timeout_ms > I2CP_TIMEOUT_MS_MAX) {
		ret = -ERANGE;
		goto unlock;
	}

	pdata->i2c_adapter.timeout = msecs_to_jiffies(cmd_data->timeout_ms);
	ret = 0;

unlock:
	mutex_unlock(&pdata->startstop_lock);
	return ret;
}

/* Command names are matched in this order, so sort by expected frequency. */
/* All elements should be initialized in their I2CP_CMD_*_IDX position. */
static const struct i2cp_cmd i2cp_cmds[] = {
	[I2CP_CMD_MXFER_REPLY_IDX] = {
		.cmd_string = I2CP_MXFER_REPLY_CMD,
		.cmd_size = CONST_STRLEN(I2CP_MXFER_REPLY_CMD),
		.data_creator = i2cp_cmd_mxfer_reply_data_creator,
		.data_shutdown = i2cp_cmd_mxfer_reply_data_shutdown,
		.data_destroyer = i2cp_cmd_mxfer_reply_data_destroyer,
		.header_receiver = i2cp_cmd_mxfer_reply_header_receiver,
		.data_receiver = i2cp_cmd_mxfer_reply_data_receiver,
		.cmd_completer = i2cp_cmd_mxfer_reply_cmd_completer,
	},
	[I2CP_CMD_ADAP_START_IDX] = {
		.cmd_string = I2CP_ADAP_START_CMD,
		.cmd_size = CONST_STRLEN(I2CP_ADAP_START_CMD),
		.header_receiver = i2cp_cmd_adap_start_header_receiver,
		.data_receiver = i2cp_cmd_adap_start_data_receiver,
		.cmd_completer = i2cp_cmd_adap_start_cmd_completer,
	},
	[I2CP_CMD_ADAP_SHUTDOWN_IDX] = {
		.cmd_string = I2CP_ADAP_SHUTDOWN_CMD,
		.cmd_size = CONST_STRLEN(I2CP_ADAP_SHUTDOWN_CMD),
		.header_receiver = i2cp_cmd_adap_shutdown_header_receiver,
		.data_receiver = i2cp_cmd_adap_shutdown_data_receiver,
		.cmd_completer = i2cp_cmd_adap_shutdown_cmd_completer,
	},
	[I2CP_CMD_GET_NUMBER_IDX] = {
		.cmd_string = I2CP_GET_NUMBER_CMD,
		.cmd_size = CONST_STRLEN(I2CP_GET_NUMBER_CMD),
		.header_receiver = i2cp_cmd_get_number_header_receiver,
		.data_receiver = i2cp_cmd_get_number_data_receiver,
		.cmd_completer = i2cp_cmd_get_number_cmd_completer,
	},
	[I2CP_CMD_GET_PSEUDO_ID_IDX] = {
		.cmd_string = I2CP_GET_PSEUDO_ID_CMD,
		.cmd_size = CONST_STRLEN(I2CP_GET_PSEUDO_ID_CMD),
		.header_receiver = i2cp_cmd_get_pseudo_id_header_receiver,
		.data_receiver = i2cp_cmd_get_pseudo_id_data_receiver,
		.cmd_completer = i2cp_cmd_get_pseudo_id_cmd_completer,
	},
	[I2CP_CMD_SET_NAME_SUFFIX_IDX] = {
		.cmd_string = I2CP_SET_NAME_SUFFIX_CMD,
		.cmd_size = CONST_STRLEN(I2CP_SET_NAME_SUFFIX_CMD),
		.data_creator = i2cp_cmd_set_name_suffix_data_creator,
		.data_destroyer = i2cp_cmd_set_name_suffix_data_destroyer,
		.header_receiver = i2cp_cmd_set_name_suffix_header_receiver,
		.data_receiver = i2cp_cmd_set_name_suffix_data_receiver,
		.cmd_completer = i2cp_cmd_set_name_suffix_cmd_completer,
	},
	[I2CP_CMD_SET_TIMEOUT_IDX] = {
		.cmd_string = I2CP_SET_TIMEOUT_CMD,
		.cmd_size = CONST_STRLEN(I2CP_SET_TIMEOUT_CMD),
		.data_creator = i2cp_cmd_set_timeout_data_creator,
		.data_destroyer = i2cp_cmd_set_timeout_data_destroyer,
		.header_receiver = i2cp_cmd_set_timeout_header_receiver,
		.data_receiver = i2cp_cmd_set_timeout_data_receiver,
		.cmd_completer = i2cp_cmd_set_timeout_cmd_completer,
	},
};

/* Returns whether or not there is response queue data to read. */
/* Must be called with pdata->rsp_lock held. */
static inline bool i2cp_poll_in(struct i2cp_controller *pdata)
{
	return pdata->rsp_invalidated || pdata->rsp_buf_remaining != 0 ||
	       !list_empty(&pdata->read_rsp_queue_head);
}

static inline int i2cp_fill_rsp_buf(struct i2cp_rsp *rsp_wrapper,
				    struct i2cp_rsp_buffer *rsp_buf,
				    char *contents, size_t size)
{
	rsp_buf->buf = kmemdup(contents, size, GFP_KERNEL);
	if (!rsp_buf->buf)
		return -ENOMEM;
	rsp_buf->size = size;
	rsp_wrapper->data = rsp_buf;
	rsp_wrapper->formatter = i2cp_rsp_buffer_formatter;
	return 0;
}

#define I2CP_FILL_RSP_BUF_WITH_LITERAL(rsp_wrapper, rsp_buf, str_literal) \
	i2cp_fill_rsp_buf(rsp_wrapper, rsp_buf, str_literal,              \
			  strlen(str_literal))

static int i2cp_adapter_master_xfer(struct i2c_adapter *adap,
				    struct i2c_msg *msgs, int num)
{
	int i, ret = 0;
	long wait_ret;
	size_t wrappers_length, wrapper_idx = 0, rsp_bufs_idx = 0;
	struct i2cp_controller *pdata;
	struct i2cp_rsp **rsp_wrappers;
	struct i2cp_rsp_buffer *rsp_bufs[2] = { 0 };
	struct i2cp_rsp_master_xfer *mxfer_rsp;
	struct i2cp_cmd_mxfer_reply_data *cmd_data;
	struct i2cp_cmd_mxfer_reply *mxfer_reply;

	if (num <= 0) {
		if (num < 0)
			return -EINVAL;
		return ret;
	}

	pdata = adap->algo_data;
	cmd_data = pdata->cmd_data[I2CP_CMD_MXFER_REPLY_IDX];

	switch (i2cp_adap_get_state(pdata)) {
	case I2CP_CTRLR_STATE_RUNNING:
		break;
	case I2CP_CTRLR_STATE_SHUTDN_REQ:
		return ret;
	default:
		/* Reaching here is a bug, even with a valid enum value. */
		return -EINVAL;
	}

	wrappers_length = (size_t)num + ARRAY_SIZE(rsp_bufs);
	rsp_wrappers =
		kcalloc(wrappers_length, sizeof(*rsp_wrappers), GFP_KERNEL);
	if (!rsp_wrappers)
		return -ENOMEM;

	mxfer_reply = kzalloc(sizeof(*mxfer_reply), GFP_KERNEL);
	if (!mxfer_reply) {
		ret = -ENOMEM;
		goto return_after_rsp_wrappers_ptrs_alloc;
	}

	mxfer_reply->num_msgs = num;
	init_completion(&mxfer_reply->data_filled);
	mutex_init(&mxfer_reply->lock);

	mxfer_reply->msgs =
		kcalloc(num, sizeof(*mxfer_reply->msgs), GFP_KERNEL);
	if (!mxfer_reply->msgs) {
		ret = -ENOMEM;
		goto return_after_mxfer_reply_alloc;
	}

	mxfer_reply->completed =
		kcalloc(num, sizeof(*mxfer_reply->completed), GFP_KERNEL);
	if (!mxfer_reply->completed) {
		ret = -ENOMEM;
		goto return_after_reply_msgs_alloc;
	}

	for (i = 0; i < num; ++i) {
		mxfer_reply->msgs[i].addr = msgs[i].addr;
		mxfer_reply->msgs[i].flags = msgs[i].flags;
		mxfer_reply->msgs[i].len = msgs[i].len;
		if (msgs[i].flags & I2C_M_RD)
			/* Copy the address, not the data. */
			mxfer_reply->msgs[i].buf = msgs[i].buf;
	}

	for (i = 0; i < ARRAY_SIZE(rsp_bufs); ++i) {
		rsp_bufs[i] = kzalloc(sizeof(*rsp_bufs[i]), GFP_KERNEL);
		if (!rsp_bufs[i]) {
			ret = -ENOMEM;
			goto return_after_reply_completed_alloc;
		}
	}

	mxfer_rsp = kzalloc(sizeof(*mxfer_rsp), GFP_KERNEL);
	if (!mxfer_rsp) {
		ret = -ENOMEM;
		goto fail_after_individual_rsp_bufs_alloc;
	}

	mxfer_rsp->id = cmd_data->next_mxfer_id++;
	mxfer_rsp->num = num;

	mxfer_rsp->msgs = kcalloc(num, sizeof(*mxfer_rsp->msgs), GFP_KERNEL);
	if (!mxfer_rsp->msgs) {
		ret = -ENOMEM;
		goto fail_after_mxfer_rsp_alloc;
	}

	for (i = 0; i < num; ++i) {
		mxfer_rsp->msgs[i].addr = msgs[i].addr;
		mxfer_rsp->msgs[i].flags = msgs[i].flags;
		mxfer_rsp->msgs[i].len = msgs[i].len;
		if (msgs[i].flags & I2C_M_RD)
			continue;
		/* Copy the data, not the address. */
		mxfer_rsp->msgs[i].buf =
			kmemdup(msgs[i].buf, msgs[i].len, GFP_KERNEL);
		if (!mxfer_rsp->msgs[i].buf) {
			ret = -ENOMEM;
			goto fail_after_rsp_msgs_alloc;
		}
	}

	for (i = 0; i < wrappers_length; ++i) {
		rsp_wrappers[i] = kzalloc(sizeof(*rsp_wrappers[i]), GFP_KERNEL);
		if (!rsp_wrappers[i]) {
			ret = -ENOMEM;
			goto fail_after_individual_rsp_wrappers_alloc;
		}
	}

	ret = I2CP_FILL_RSP_BUF_WITH_LITERAL(rsp_wrappers[wrapper_idx++],
					     rsp_bufs[rsp_bufs_idx++],
					     I2CP_BEGIN_MXFER_REQ_CMD);
	if (ret < 0)
		goto fail_after_individual_rsp_wrappers_alloc;

	for (i = 0; i < num; ++i) {
		rsp_wrappers[wrapper_idx]->data = mxfer_rsp;
		rsp_wrappers[wrapper_idx++]->formatter =
			i2cp_rsp_master_xfer_formatter;
	}

	ret = I2CP_FILL_RSP_BUF_WITH_LITERAL(rsp_wrappers[wrapper_idx++],
					     rsp_bufs[rsp_bufs_idx++],
					     I2CP_COMMIT_MXFER_REQ_CMD);
	if (ret < 0)
		goto fail_after_individual_rsp_wrappers_alloc;

	BUILD_BUG_ON(rsp_bufs_idx != ARRAY_SIZE(rsp_bufs));

	mutex_lock(&pdata->read_rsp_queue_lock);
	if (pdata->read_rsp_queue_length >= I2CP_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		goto fail_with_read_rsp_queue_lock;
	}

	mutex_lock(&cmd_data->reply_queue_lock);
	if (cmd_data->reply_queue_length >= I2CP_CTRLR_RSP_QUEUE_LIMIT) {
		ret = -ENOBUFS;
		goto fail_with_reply_queue_lock;
	}

	mxfer_reply->id = mxfer_rsp->id;
	list_add_tail(&mxfer_reply->reply_queue_item,
		      &cmd_data->reply_queue_head);
	++cmd_data->reply_queue_length;

	for (i = 0; i < wrappers_length; ++i) {
		list_add_tail(&rsp_wrappers[i]->queue,
			      &pdata->read_rsp_queue_head);
		complete(&pdata->read_rsp_queued);
	}
	pdata->read_rsp_queue_length += wrappers_length;

	mutex_unlock(&cmd_data->reply_queue_lock);
	mutex_unlock(&pdata->read_rsp_queue_lock);

	/* Wake up the userspace controller if it was polling. */
	wake_up_interruptible(&pdata->poll_wait_queue);
	/* Wait for a response from the userspace controller. */
	wait_ret = wait_for_completion_killable_timeout(
		&mxfer_reply->data_filled, adap->timeout);

	mutex_lock(&cmd_data->reply_queue_lock);
	/*
	 * Ensure mxfer_reply is not in use before dequeuing and freeing it.
	 * This depends on the requirement that mxfer_reply->lock only be
	 * acquired while holding cmd_data->reply_queue_lock.
	 */
	mutex_lock(&mxfer_reply->lock);

	if (wait_ret == -ERESTARTSYS)
		ret = -EINTR;
	else if (wait_ret < 0)
		ret = wait_ret;
	else
		ret = mxfer_reply->ret;

	/*
	 * This depends on other functions that might delete
	 * mxfer_reply->reply_queue_item from cmd_data->reply_queue_head using
	 * list_del_init(), never list_del().
	 */
	if (!list_empty(&mxfer_reply->reply_queue_item)) {
		list_del(&mxfer_reply->reply_queue_item);
		--cmd_data->reply_queue_length;
		if (mxfer_reply == cmd_data->reply_queue_current_item)
			cmd_data->reply_queue_current_item = NULL;
	}

	mutex_unlock(&mxfer_reply->lock);
	mutex_unlock(&cmd_data->reply_queue_lock);
	goto return_after_reply_msgs_alloc;

fail_with_reply_queue_lock:
	mutex_unlock(&cmd_data->reply_queue_lock);
fail_with_read_rsp_queue_lock:
	mutex_unlock(&pdata->read_rsp_queue_lock);
fail_after_individual_rsp_wrappers_alloc:
	for (i = 0; i < wrappers_length; ++i)
		kfree(rsp_wrappers[i]);
fail_after_rsp_msgs_alloc:
	for (i = 0; i < num; ++i)
		kfree(mxfer_rsp->msgs[i].buf);
	kfree(mxfer_rsp->msgs);
fail_after_mxfer_rsp_alloc:
	kfree(mxfer_rsp);
fail_after_individual_rsp_bufs_alloc:
	for (i = 0; i < ARRAY_SIZE(rsp_bufs); ++i) {
		kfree(rsp_bufs[i]->buf);
		kfree(rsp_bufs[i]);
	}
return_after_reply_completed_alloc:
	kfree(mxfer_reply->completed);
return_after_reply_msgs_alloc:
	kfree(mxfer_reply->msgs);
return_after_mxfer_reply_alloc:
	kfree(mxfer_reply);
return_after_rsp_wrappers_ptrs_alloc:
	kfree(rsp_wrappers);
	return ret;
}

/*
 * If more functionality than this needs to be supported, add a write command
 * for the controller to specify its additional functionality prior to
 * ADAPTER_START.  Basic I2C functionality should remain implied and required.
 *
 * These functionalities in particular could be worth supporting:
 * I2C_FUNC_10BIT_ADDR
 * I2C_FUNC_NOSTART
 * I2C_FUNC_PROTOCOL_MANGLING
 */
static u32 i2cp_adapter_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm i2cp_algorithm = {
	.master_xfer = i2cp_adapter_master_xfer,
	.functionality = i2cp_adapter_functionality,
};

/* this_pseudo->counters.lock must _not_ be held when calling this. */
static void i2cp_remove_from_counters(struct i2cp_controller *pdata,
				      struct i2cp_device *this_pseudo)
{
	mutex_lock(&this_pseudo->counters.lock);
	this_pseudo->counters.all_controllers[pdata->index] = NULL;
	--this_pseudo->counters.count;
	mutex_unlock(&this_pseudo->counters.lock);
}

static int i2cp_cdev_open(struct inode *inodep, struct file *filep)
{
	int ret = 0;
	unsigned int i, num_cmd_data_created = 0;
	unsigned int ctrlr_id;
	struct i2cp_controller *pdata;
	struct i2cp_device *this_pseudo;

	/* Is there any way to find this through @inodep? */
	this_pseudo = i2cp_device;

	/*
	 * HAVE_STREAM_OPEN value meanings:
	 *   -1 : stream_open() is not available
	 *    0 : unknown if stream_open() is or is not available
	 *    1 : stream_open() is available
	 */
#if HAVE_STREAM_OPEN >= 0
	/* I2C pseudo adapter controllers are non-seekable pure I/O streams. */
	stream_open(inodep, filep);
#else
	/* I2C pseudo adapter controllers are not seekable. */
	nonseekable_open(inodep, filep);
#endif
	/* Refuse fsnotify events.  Modeled after /dev/ptmx implementation. */
	filep->f_mode |= FMODE_NONOTIFY;

	/* Allocate the I2C adapter. */
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	INIT_LIST_HEAD(&pdata->read_rsp_queue_head);
	init_waitqueue_head(&pdata->poll_wait_queue);
	init_completion(&pdata->read_rsp_queued);
	mutex_init(&pdata->startstop_lock);
	mutex_init(&pdata->cmd_lock);
	mutex_init(&pdata->rsp_lock);
	mutex_init(&pdata->read_rsp_queue_lock);

	for (i = 0; i < ARRAY_SIZE(i2cp_cmds); ++i) {
		if (!i2cp_cmds[i].data_creator)
			continue;
		ret = i2cp_cmds[i].data_creator(&pdata->cmd_data[i]);
		if (ret < 0)
			break;
	}
	num_cmd_data_created = i;
	if (ret < 0)
		goto fail_after_cmd_data_created;

	mutex_lock(&this_pseudo->counters.lock);

	for (i = 0; i < i2cp_limit; ++i)
		if (!this_pseudo->counters.all_controllers[i])
			break;
	if (i >= i2cp_limit) {
		mutex_unlock(&this_pseudo->counters.lock);
		ret = -ENOSPC;
		goto fail_after_cmd_data_created;
	}
	pdata->index = i;

	for (ctrlr_id = this_pseudo->counters.next_ctrlr_id;;) {
		/* Determine whether ctrlr_id is already in use. */
		for (i = 0; i < i2cp_limit; ++i) {
			if (this_pseudo->counters.all_controllers[i] &&
			    (this_pseudo->counters.all_controllers[i]->id ==
			     ctrlr_id))
				break;
		}
		/* If ctrlr_id is available, use it. */
		if (i >= i2cp_limit) {
			pdata->id = ctrlr_id;
			this_pseudo->counters.next_ctrlr_id = ctrlr_id + 1;
			++this_pseudo->counters.count;
			this_pseudo->counters.all_controllers[pdata->index] =
				pdata;
			break;
		}
		/* Increment ctrlr_id, and check for wrapping. */
		if (++ctrlr_id == this_pseudo->counters.next_ctrlr_id) {
			mutex_unlock(&this_pseudo->counters.lock);
			ret = -ENOSPC;
			goto fail_after_cmd_data_created;
		}
	}

	mutex_unlock(&this_pseudo->counters.lock);

	/* Initialize the I2C adapter. */
	pdata->i2c_adapter.owner = THIS_MODULE;
	pdata->i2c_adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	pdata->i2c_adapter.algo = &i2cp_algorithm;
	pdata->i2c_adapter.algo_data = pdata;
	pdata->i2c_adapter.timeout = msecs_to_jiffies(i2cp_default_timeout_ms);
	pdata->i2c_adapter.dev.parent = &this_pseudo->device;
	ret = snprintf(pdata->i2c_adapter.name, sizeof(pdata->i2c_adapter.name),
		       "I2C pseudo ID %u", pdata->id);
	if (ret < 0)
		goto fail_after_counters_update;

	/* Return success. */
	filep->private_data = pdata;
	return 0;

fail_after_counters_update:
	i2cp_remove_from_counters(pdata, this_pseudo);
fail_after_cmd_data_created:
	for (i = 0; i < num_cmd_data_created; ++i)
		if (i2cp_cmds[i].data_destroyer)
			i2cp_cmds[i].data_destroyer(pdata->cmd_data[i]);
	kfree(pdata);
	return ret;
}

static int i2cp_cdev_release(struct inode *inodep, struct file *filep)
{
	int i;
	bool adapter_was_added = false;
	struct i2cp_controller *pdata;
	struct i2cp_device *this_pseudo;

	pdata = filep->private_data;
	this_pseudo = container_of(pdata->i2c_adapter.dev.parent,
				   struct i2cp_device, device);

	/*
	 * The select(2) man page makes it clear that the behavior of pending
	 * select()/poll()/epoll_wait() on a fd that gets closed while waiting
	 * is undefined and should never be relied on.  However since we are
	 * about to free pdata and therefore free pdata->poll_wait_queue, safest
	 * to wake up anyone waiting on it in an attempt to not leave them in a
	 * completely undefined state.
	 */
	wake_up_interruptible_all(&pdata->poll_wait_queue);
	/*
	 * Linux guarantees there are no outstanding reads or writes when a
	 * struct file is released, so no further synchronization with the other
	 * struct file_operations callbacks should be needed.
	 */
	filep->private_data = NULL;

	mutex_lock(&pdata->startstop_lock);
	if (pdata->startstop_state != I2CP_CTRLR_STATE_NEW) {
		/*
		 * Defer deleting the adapter until after releasing
		 * pdata->startstop_state.  This avoids deadlocking with any
		 * overlapping i2cp_adapter_master_xfer() calls, which also
		 * acquire the lock in order to check the state.
		 */
		adapter_was_added = true;
		/*
		 * Instruct any overlapping i2cp_adapter_master_xfer() calls to
		 * return immediately.
		 */
		pdata->startstop_state = I2CP_CTRLR_STATE_SHUTDN_REQ;
	}
	mutex_unlock(&pdata->startstop_lock);

	/*
	 * Wake up blocked I2C requests.  This is an optimization so that they
	 * don't need to wait for the I2C adapter timeout, since there is no
	 * possibility of any further I2C replies.
	 */
	for (i = 0; i < ARRAY_SIZE(i2cp_cmds); ++i)
		if (i2cp_cmds[i].data_shutdown)
			i2cp_cmds[i].data_shutdown(pdata->cmd_data[i]);

	if (adapter_was_added)
		i2c_del_adapter(&pdata->i2c_adapter);

	for (i = 0; i < ARRAY_SIZE(i2cp_cmds); ++i) {
		if (i2cp_cmds[i].data_destroyer)
			i2cp_cmds[i].data_destroyer(pdata->cmd_data[i]);
		pdata->cmd_data[i] = NULL;
	}

	i2cp_remove_from_counters(pdata, this_pseudo);
	kfree(pdata);
	return 0;
}

/* The caller must hold pdata->rsp_lock. */
/* Return value is whether or not to continue in calling loop. */
static bool i2cp_cdev_read_iteration(char __user **buf, size_t *count,
				     ssize_t *ret, bool non_blocking,
				     struct i2cp_controller *pdata)
{
	long wait_ret;
	ssize_t copy_size;
	unsigned long copy_ret;
	struct i2cp_rsp *rsp_wrapper = NULL;

	/*
	 * If a previous read response buffer has been exhausted, free
	 * it.
	 *
	 * This is done at the beginning of the while(count>0) loop
	 * because...?
	 */
	if (pdata->rsp_buf_start && !pdata->rsp_buf_remaining) {
		kfree(pdata->rsp_buf_start);
		pdata->rsp_buf_start = NULL;
		pdata->rsp_buf_pos = NULL;
	}

	/*
	 * If we have no formatter callback output queued (neither
	 * successful output nor error), go through the FIFO queue of
	 * read responses until a formatter returns non-zero (successful
	 * output or failure).
	 */
	while (pdata->rsp_buf_remaining == 0) {
		/*
		 * If pdata->rsp_invalidated is true, it means the
		 * previous read() returned an error.  Now that the
		 * error has already been propagated to userspace, we
		 * can write the end character for the invalidated read
		 * response.
		 */
		if (pdata->rsp_invalidated) {
			pdata->rsp_invalidated = false;
			goto write_end_char;
		}

		/* If we have already read some bytes successfully, even
		 * if less than requested, we should return as much as
		 * we can without blocking further.  Same if we have an
		 * error to return.
		 */
		if (non_blocking || *ret != 0) {
			if (!try_wait_for_completion(&pdata->read_rsp_queued)) {
				if (*ret == 0)
					*ret = -EAGAIN;
				/*
				 * If we are out of read responses,
				 * return whatever we have written to
				 * the userspace buffer so far, even if
				 * it's nothing.
				 */
				return false;
			}
		} else {
			wait_ret = wait_for_completion_killable(
				&pdata->read_rsp_queued);
			if (wait_ret == -ERESTARTSYS) {
				if (*ret == 0)
					*ret = -EINTR;
				return false;
			} else if (wait_ret < 0) {
				if (*ret == 0)
					*ret = wait_ret;
				return false;
			}
		}

		mutex_lock(&pdata->read_rsp_queue_lock);
		if (!list_empty(&pdata->read_rsp_queue_head))
			rsp_wrapper =
				list_first_entry(&pdata->read_rsp_queue_head,
						 struct i2cp_rsp, queue);
		/*
		 * Avoid holding pdata->read_rsp_queue_lock while
		 * executing a formatter, allocating memory, or doing
		 * anything else that might block or take non-trivial
		 * time.  This avoids blocking the enqueuing of new read
		 * responses for any significant time, even during large
		 * controller reads.
		 */
		mutex_unlock(&pdata->read_rsp_queue_lock);

		if (!rsp_wrapper) {
			/* This should only happen if shutdown was requested. */
			if (i2cp_adap_get_state(pdata) !=
			    I2CP_CTRLR_STATE_SHUTDN_REQ)
				*ret = -EINVAL;
			return false;
		}

		pdata->rsp_buf_remaining = rsp_wrapper->formatter(
			rsp_wrapper->data, &pdata->rsp_buf_start);

		if (pdata->rsp_buf_remaining > 0) {
			pdata->rsp_buf_pos = pdata->rsp_buf_start;
			/*
			 * We consumed a completion for this rsp_wrapper
			 * but we are leaving it in
			 * pdata->read_rsp_queue_head.  Re-add a
			 * completion for it.
			 *
			 * Since overlapping reads are effectively
			 * serialized via use of pdata->rsp_lock, we
			 * could take shortcuts in how
			 * pdata->read_rsp_queued is used to avoid the
			 * need for re-incrementing it here.  However by
			 * maintaining the invariant of consuming a
			 * completion each time an item from
			 * pdata->read_rsp_queue_head is consumed
			 * (whether or not it ends up being removed from
			 * the queue in that iteration), the completion
			 * logic is simpler to follow, and more easily
			 * lends itself to a future refactor of this
			 * read operation to not hold pdata->rsp_lock
			 * continuously.
			 */
			complete(&pdata->read_rsp_queued);
			break;
		}

		/*
		 * The formatter should not mutate pdata->rsp_buf_start
		 * if it returned non-positive.  Just in case, we handle
		 * such a bug gracefully here.
		 */
		kfree(pdata->rsp_buf_start);
		pdata->rsp_buf_start = NULL;

		mutex_lock(&pdata->read_rsp_queue_lock);
		list_del(&rsp_wrapper->queue);
		--pdata->read_rsp_queue_length;
		mutex_unlock(&pdata->read_rsp_queue_lock);

		kfree(rsp_wrapper);
		rsp_wrapper = NULL;

		/* Check if the formatter callback returned an error.
		 *
		 * If we have _not_ written any bytes to the userspace
		 * buffer yet, return now with the error code from the
		 * formatter.
		 *
		 * If we _have_ written bytes already, return now with
		 * the number of bytes written, and leave the error code
		 * from the formatter in pdata->rsp_buf_remaining so it
		 * can be returned on the next read, before any bytes
		 * are written.
		 *
		 * In either case, we deliberately return the error
		 * before writing the end character for the invalidated
		 * read response, so that the userspace controller knows
		 * to discard the response.
		 */
		if (pdata->rsp_buf_remaining < 0) {
			if (*ret == 0) {
				*ret = pdata->rsp_buf_remaining;
				pdata->rsp_buf_remaining = 0;
			}
			pdata->rsp_invalidated = true;
			return false;
		}

	write_end_char:
		copy_size = sizeof(i2cp_ctrlr_end_char);
		/*
		 * This assertion is just in case someone changes
		 * i2cp_ctrlr_end_char to a string.  Such a change would require
		 * handling it like a read response buffer, including ensuring
		 * that we not write more than *count.  So long as it's a single
		 * character, we can avoid an extra check of *count in this code
		 * block, we already know it's greater than zero.
		 */
		BUILD_BUG_ON(copy_size != 1);
		copy_ret = copy_to_user(*buf, &i2cp_ctrlr_end_char, copy_size);
		copy_size -= copy_ret;
		/*
		 * After writing to the userspace buffer, we need to
		 * update various counters including the return value,
		 * then continue from the start of the outer while loop
		 * because it's possible *count has reached zero.
		 *
		 * Those exact same steps must be done after copying
		 * from a read response buffer to the userspace buffer,
		 * so jump to that code instead of duplicating it.
		 */
		goto after_copy_to_user;
	}

	copy_size = max_t(ssize_t, 0,
			  min_t(ssize_t, *count, pdata->rsp_buf_remaining));
	copy_ret = copy_to_user(*buf, pdata->rsp_buf_pos, copy_size);
	copy_size -= copy_ret;
	pdata->rsp_buf_remaining -= copy_size;

	if (pdata->rsp_buf_remaining > 0) {
		pdata->rsp_buf_pos += copy_size;
	} else {
		kfree(pdata->rsp_buf_start);
		pdata->rsp_buf_start = NULL;
		pdata->rsp_buf_pos = NULL;
	}

/*
 * When jumping here, the following variables should be set:
 *   copy_ret: Return value from copy_to_user() (bytes not copied).
 *   copy_size: The number of bytes successfully copied by copy_to_user().  In
 *       other words, this should be the size arg to copy_to_user() minus its
 *       return value (bytes not copied).
 */
after_copy_to_user:
	*ret += copy_size;
	*count -= copy_size;
	*buf += copy_size;

	return !copy_ret;
}

static ssize_t i2cp_cdev_read(struct file *filep, char __user *buf,
			      size_t count, loff_t *f_ps)
{
	ssize_t ret = 0;
	bool non_blocking;
	struct i2cp_controller *pdata;

	/*
	 * Just in case this could change out from under us, best to keep a
	 * consistent view for the duration of this syscall.
	 */
	non_blocking = !!(filep->f_flags & O_NONBLOCK);
	pdata = filep->private_data;

	if (count > (size_t)I2CP_RW_SIZE_LIMIT)
		count = I2CP_RW_SIZE_LIMIT;

	/*
	 * Since read() calls are effectively serialized by way of
	 * pdata->rsp_lock, we MUST NOT block on obtaining that lock if in
	 * non-blocking mode, because it might be held by a blocking read().
	 */
	if (!non_blocking)
		mutex_lock(&pdata->rsp_lock);
	else if (!mutex_trylock(&pdata->rsp_lock))
		return -EAGAIN;

	/*
	 * Check if a formatter callback returned an error that hasn't yet been
	 * returned to the controller.  Do this before the while(count>0) loop
	 * because read(2) with zero count is allowed to report errors.
	 */
	if (pdata->rsp_buf_remaining < 0) {
		BUILD_BUG_ON(ret != 0);
		ret = pdata->rsp_buf_remaining;
		pdata->rsp_buf_remaining = 0;
		goto unlock;
	}

	while (count > 0 && i2cp_cdev_read_iteration(&buf, &count, &ret,
						     non_blocking, pdata))
		;

unlock:
	mutex_unlock(&pdata->rsp_lock);
	return ret;
}

/* Must be called with pdata->cmd_lock held. */
/* Must never consume past first i2cp_ctrlr_end_char in @start. */
static ssize_t i2cp_receive_ctrlr_cmd_header(struct i2cp_controller *pdata,
					     char *start, size_t remaining,
					     bool non_blocking)
{
	int found_deliminator_char = 0;
	int i, cmd_idx;
	ssize_t copy_size, ret = 0, stop, buf_remaining;

	buf_remaining = I2CP_CTRLR_CMD_LIMIT - pdata->cmd_size;
	stop = min_t(ssize_t, remaining, buf_remaining + 1);

	for (i = 0; i < stop; ++i)
		if (start[i] == i2cp_ctrlr_end_char ||
		    start[i] == i2cp_ctrlr_header_sep_char) {
			found_deliminator_char = 1;
			break;
		}

	if (i <= buf_remaining) {
		copy_size = i;
	} else {
		copy_size = buf_remaining;
		if (!pdata->cmd_receive_status)
			/*
			 * Exceeded max size of I2C pseudo controller command
			 * buffer.  The command currently being written will be
			 * ignored.
			 *
			 * Positive error number is deliberate here.
			 */
			pdata->cmd_receive_status = ENOBUFS;
	}

	memcpy(&pdata->cmd_buf[pdata->cmd_size], start, copy_size);
	pdata->cmd_size += copy_size;

	if (!found_deliminator_char || pdata->cmd_size <= 0)
		return copy_size + found_deliminator_char;

	/* This may be negative. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;

	if (cmd_idx < 0) {
		for (i = 0; i < ARRAY_SIZE(i2cp_cmds); ++i)
			if (i2cp_cmds[i].cmd_size == pdata->cmd_size &&
			    !memcmp(i2cp_cmds[i].cmd_string, pdata->cmd_buf,
				    pdata->cmd_size))
				break;
		if (i >= ARRAY_SIZE(i2cp_cmds)) {
			/* unrecognized command */
			ret = -EIO;
			goto clear_buffer;
		}
		cmd_idx = i;
		pdata->cmd_idx_plus_one = cmd_idx + 1;
	}

	/*
	 * If we have write bytes queued and we encountered i2cp_ctrlr_end_char
	 * or i2cp_ctrlr_header_sep_char, invoke the header_receiver callback.
	 */
	if (!pdata->cmd_receive_status) {
		ret = i2cp_cmds[cmd_idx].header_receiver(
			pdata->cmd_data[cmd_idx], pdata->cmd_buf,
			pdata->cmd_size, non_blocking);
		if (ret > 0) {
			if (ret > I2CP_CTRLR_CMD_LIMIT) {
				ret = -EINVAL;
				goto clear_buffer;
			}
			pdata->cmd_data_increment = ret;
		} else if (ret < 0) {
			pdata->cmd_receive_status = ret;
		}
	}

clear_buffer:
	pdata->cmd_size = 0;
	/*
	 * Ensure a trailing null character for the next header_receiver() or
	 * data_receiver() invocation.
	 */
	memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));

	if (ret < 0) {
		if (pdata->cmd_idx_plus_one >= 1 && !pdata->cmd_receive_status)
			/* Negate to get a positive error number. */
			pdata->cmd_receive_status = -ret;
		return ret;
	}
	return copy_size + found_deliminator_char;
}

/* Must be called with pdata->cmd_lock held. */
/* Must never consume past first i2cp_ctrlr_end_char in @start. */
static ssize_t i2cp_receive_ctrlr_cmd_data(struct i2cp_controller *pdata,
					   char *start, size_t remaining,
					   bool non_blocking)
{
	ssize_t i, ret, size_holder;
	int cmd_idx;

	/* If cmd_idx ends up negative here, it is a bug. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;
	if (cmd_idx < 0)
		return -EINVAL;

	size_holder = min_t(
		size_t,
		(I2CP_CTRLR_CMD_LIMIT -
		 (I2CP_CTRLR_CMD_LIMIT % pdata->cmd_data_increment)) -
			pdata->cmd_size,
		(((pdata->cmd_size + remaining) / pdata->cmd_data_increment) *
		 pdata->cmd_data_increment) -
			pdata->cmd_size);

	/* Size of current buffer plus all remaining write bytes. */
	size_holder = pdata->cmd_size + remaining;
	/*
	 * Avoid rounding down to zero.  If there are insufficient write
	 * bytes remaining to grow the buffer to 1x of the requested
	 * data byte increment, we'll copy what is available to the
	 * buffer, and just leave it queued without any further command
	 * handler invocations in this write() (unless i2cp_ctrlr_end_char is
	 * found, in which case we will always invoke the data_receiver for any
	 * remaining data bytes, and will always invoke the cmd_completer).
	 */
	if (size_holder > pdata->cmd_data_increment)
		/*
		 * Round down to the nearest multiple of the requested
		 * data byte increment.
		 */
		size_holder -= size_holder % pdata->cmd_data_increment;
	/*
	 * Take the smaller of:
	 *
	 * [A] 2nd min_t() arg: The number of bytes that we would want the
	 * buffer to end up with if it had unlimited space (computed
	 * above).
	 *
	 * [B] 3rd min_t() arg: The number of bytes that we would want the
	 * buffer to end up with if there were unlimited write bytes
	 * remaining (computed in-line below).
	 */
	size_holder =
		min_t(ssize_t, size_holder,
		      (I2CP_CTRLR_CMD_LIMIT -
		       (I2CP_CTRLR_CMD_LIMIT % pdata->cmd_data_increment)));
	/*
	 * Subtract the existing buffer size to get the number of bytes we
	 * actually want to copy from the remaining write bytes in this loop
	 * iteration, assuming no i2cp_ctrlr_end_char.
	 */
	size_holder -= pdata->cmd_size;

	/*
	 * Look for i2cp_ctrlr_end_char.  If we find it, we will copy up to but
	 * *not* including its position.
	 */
	for (i = 0; i < size_holder; ++i)
		if (start[i] == i2cp_ctrlr_end_char)
			break;

	/* Copy from the remaining write bytes to the command buffer. */
	memcpy(&pdata->cmd_buf[pdata->cmd_size], start, i);
	pdata->cmd_size += i;

	/*
	 * If we have write bytes queued and *either* we encountered
	 * i2cp_ctrlr_end_char *or* we have a multiple of
	 * pdata->cmd_data_increment, invoke the data_receiver callback.
	 */
	if (pdata->cmd_size > 0 &&
	    (i < size_holder ||
	     pdata->cmd_size % pdata->cmd_data_increment == 0)) {
		if (!pdata->cmd_receive_status) {
			ret = i2cp_cmds[cmd_idx].data_receiver(
				pdata->cmd_data[cmd_idx], pdata->cmd_buf,
				pdata->cmd_size, non_blocking);
			if (ret < 0)
				pdata->cmd_receive_status = ret;
		}
		pdata->cmd_size = 0;
		/*
		 * Ensure a trailing null character for the next
		 * header_receiver() or data_receiver() invocation.
		 */
		memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));
	}

	/* If i2cp_ctrlr_end_char was found, skip past it. */
	if (i < size_holder)
		++i;
	return i;
}

/* Must be called with pdata->cmd_lock held. */
static int i2cp_receive_ctrlr_cmd_complete(struct i2cp_controller *pdata,
					   bool non_blocking)
{
	int ret = 0, cmd_idx;

	/* This may be negative. */
	cmd_idx = pdata->cmd_idx_plus_one - 1;

	if (cmd_idx >= 0 && i2cp_cmds[cmd_idx].cmd_completer) {
		ret = i2cp_cmds[cmd_idx].cmd_completer(
			pdata->cmd_data[cmd_idx], pdata,
			pdata->cmd_receive_status, non_blocking);
		if (ret > 0)
			ret = 0;
	}

	pdata->cmd_idx_plus_one = 0;
	pdata->cmd_receive_status = 0;
	pdata->cmd_data_increment = 0;

	pdata->cmd_size = 0;
	/*
	 * Ensure a trailing null character for the next header_receiver() or
	 * data_receiver() invocation.
	 */
	memset(pdata->cmd_buf, 0, sizeof(pdata->cmd_buf));

	return ret;
}

static ssize_t i2cp_cdev_write(struct file *filep, const char __user *buf,
			       size_t count, loff_t *f_ps)
{
	ssize_t ret = 0;
	bool non_blocking;
	size_t remaining;
	char *kbuf, *start;
	struct i2cp_controller *pdata;

	/*
	 * Just in case this could change out from under us, best to keep a
	 * consistent view for the duration of this syscall.
	 *
	 * Write command implementations, i.e. struct i2cp_cmd implementations,
	 * do NOT have to support blocking writes.  For example, if a write of
	 * an I2C message reply is received for a message that the pseudo
	 * adapter never requested or expected, it makes more sense to indicate
	 * an error than to block until possibly receiving a master_xfer request
	 * for that I2C message, even if blocking is permitted.
	 *
	 * Furthermore, controller writes MUST NEVER block indefinitely, even
	 * when non_blocking is false.  E.g. while non_blocking may be used to
	 * select between mutex_trylock and mutex_lock*, even in the
	 * latter case the lock should never be blocked on I/O, on userspace, or
	 * on anything else outside the control of this driver.  It IS
	 * permissable for the lock to be blocked on processing of previous or
	 * concurrent write input, so long as that processing does not violate
	 * these rules.
	 */
	non_blocking = !!(filep->f_flags & O_NONBLOCK);
	pdata = filep->private_data;

	if (count > (size_t)I2CP_RW_SIZE_LIMIT)
		count = I2CP_RW_SIZE_LIMIT;

	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto free_kbuf;
	}
	if (copy_from_user(kbuf, buf, count)) {
		ret = -EFAULT;
		goto free_kbuf;
	}

	start = kbuf;
	remaining = count;

	/*
	 * Since write() calls are effectively serialized by way of
	 * pdata->cmd_lock, we MUST NOT block on obtaining that lock if in
	 * non-blocking mode, because it might be held by a blocking write().
	 */
	if (!non_blocking) {
		mutex_lock(&pdata->cmd_lock);
	} else if (!mutex_trylock(&pdata->cmd_lock)) {
		ret = -EAGAIN;
		goto free_kbuf;
	}

	while (remaining) {
		if (pdata->cmd_data_increment <= 0)
			ret = i2cp_receive_ctrlr_cmd_header(
				pdata, start, remaining, non_blocking);
		else
			ret = i2cp_receive_ctrlr_cmd_data(
				pdata, start, remaining, non_blocking);
		if (ret < 0)
			break;
		if (ret == 0 || ret > remaining) {
			ret = -EINVAL;
			break;
		}

		remaining -= ret;
		start += ret;

		if (ret > 0 && start[-1] == i2cp_ctrlr_end_char) {
			ret = i2cp_receive_ctrlr_cmd_complete(pdata,
							      non_blocking);
			if (ret < 0)
				break;
		}
	}

	mutex_unlock(&pdata->cmd_lock);
	wake_up_interruptible_sync(&pdata->poll_wait_queue);

	if (ret >= 0)
		/* If successful the whole write is always consumed. */
		ret = count;

free_kbuf:
	kfree(kbuf);
	return ret;
}

/*
 * The select/poll/epoll implementation in this module is designed around these
 * controller behavior assumptions:
 *
 * - If any reader of a given controller makes use of polling, all will.
 *
 * - Upon notification of available data to read, a reader will fully consume it
 *   in a read() loop until receiving EAGAIN, EWOULDBLOCK, or EOF.
 *
 * - Only one reader need be woken upon newly available data, however it is okay
 *   if more than one are sometimes woken.
 *
 * - If more than one reader is woken, or otherwise acts in parallel, it is the
 *   responsibility of the readers to either ensure that only one at a time
 *   consumes all input until EAGAIN/EWOULDBLOCK, or that they properly
 *   recombine any data that was split among them.
 *
 * - All of the above applies to writers as well.
 *
 * Notes:
 *
 * - If a reader does not read all available data until EAGAIN/EWOULDBLOCK after
 *   being woken from poll, there may be no wake event for the remaining
 *   available data, causing it to remain unread until further data becomes
 *   available and triggers another wake event.  The same applies to writers -
 *   they are only guaranteed to be woken /once/ per blocked->unblocked
 *   transition, so after being woken they should continue writing until either
 *   the controller is out of data or EAGAIN/EWOULDBLOCK is encountered.
 *
 * - It is strongly suggested that controller implementations have only one
 *   reader (thread) and one writer (thread), which may or may not be the same
 *   thread.  After all only one message can be active on an I2C bus at a time,
 *   and this driver implementation reflects that.  Avoiding multiple readers
 *   and multiple writers greatly simplifies controller implementation, and
 *   there is likely nothing to be gained from performing any of their work in
 *   parallel.
 *
 * - Implementation detail: Reads are effectively serialized by a per controller
 *   read lock.  From the perspective of other readers, the controller device
 *   will appear blocked, with appropriate behavior based on the O_NONBLOCK bit.
 *   THIS IS SUBJECT TO CHANGE!
 *
 * - Implementation detail: Writes are effectively serialized by a per
 *   controller write lock.  From the perspective of other writers, the
 *   controller device will appear blocked, with appropriate behavior based on
 *   the O_NONBLOCK bit.  THIS IS SUBJECT TO CHANGE!
 *
 * - Implementation detail: In the initial implementation, the only scenario
 *   where a controller will appear blocked for writes is if another write is in
 *   progress.  Thus, a single writer should never see the device blocked.  THIS
 *   IS SUBJECT TO CHANGE!  When using O_NONBLOCK, a controller should correctly
 *   handle EAGAIN/EWOULDBLOCK even if it has only one writer.
 */
static __poll_t i2cp_cdev_poll(struct file *filep, poll_table *ptp)
{
	__poll_t poll_ret = 0;
	struct i2cp_controller *pdata;

	pdata = filep->private_data;

	poll_wait(filep, &pdata->poll_wait_queue, ptp);

	if (mutex_trylock(&pdata->rsp_lock)) {
		if (i2cp_poll_in(pdata))
			poll_ret |= POLLIN | POLLRDNORM;
		mutex_unlock(&pdata->rsp_lock);
	}

	if (!mutex_is_locked(&pdata->cmd_lock))
		poll_ret |= POLLOUT | POLLWRNORM;

	if (i2cp_adap_get_state(pdata) == I2CP_CTRLR_STATE_SHUTDN_REQ)
		poll_ret |= POLLHUP;

	return poll_ret;
}

static const struct file_operations i2cp_fileops = {
	.owner = THIS_MODULE,
	.open = i2cp_cdev_open,
	.release = i2cp_cdev_release,
	.read = i2cp_cdev_read,
	.write = i2cp_cdev_write,
	.poll = i2cp_cdev_poll,
	.llseek = no_llseek,
};

static ssize_t i2cp_limit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%u\n", i2cp_limit);
	if (ret >= PAGE_SIZE)
		return -ERANGE;
	return ret;
}

static struct device_attribute i2cp_limit_dev_attr = {
	.attr = {
		.name = "limit",
		.mode = 0444,
	},
	.show = i2cp_limit_show,
};

static ssize_t i2cp_count_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int count, ret;
	struct i2cp_device *this_pseudo;

	this_pseudo = container_of(dev, struct i2cp_device, device);

	mutex_lock(&this_pseudo->counters.lock);
	count = this_pseudo->counters.count;
	mutex_unlock(&this_pseudo->counters.lock);

	ret = snprintf(buf, PAGE_SIZE, "%u\n", count);
	if (ret >= PAGE_SIZE)
		return -ERANGE;
	return ret;
}

static struct device_attribute i2cp_count_dev_attr = {
	.attr = {
		.name = "count",
		.mode = 0444,
	},
	.show = i2cp_count_show,
};

static struct attribute *i2cp_device_sysfs_attrs[] = {
	&i2cp_limit_dev_attr.attr,
	&i2cp_count_dev_attr.attr,
	NULL,
};

static const struct attribute_group i2cp_device_sysfs_group = {
	.attrs = i2cp_device_sysfs_attrs,
};

static const struct attribute_group *i2cp_device_sysfs_groups[] = {
	&i2cp_device_sysfs_group,
	NULL,
};

static void i2c_p_device_release(struct device *dev)
{
	struct i2cp_device *this_pseudo;

	this_pseudo = container_of(dev, struct i2cp_device, device);
	kfree(this_pseudo->counters.all_controllers);
	kfree(this_pseudo);
}

static inline void i2c_p_class_destroy(void)
{
	struct class *class;

	class = i2cp_class;
	i2cp_class = NULL;
	class_destroy(class);
}

static int __init i2cp_init(void)
{
	int ret = -1;

	if (i2cp_limit < I2CP_ADAPTERS_MIN || i2cp_limit > I2CP_ADAPTERS_MAX) {
		pr_err("%s: i2cp_limit=%u, must be in range [" STR(
			       I2CP_ADAPTERS_MIN) ", " STR(I2CP_ADAPTERS_MAX) "]\n",
		       __func__, i2cp_limit);
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	i2cp_class = class_create(I2CP_CLASS_NAME);
#else
	i2cp_class = class_create(THIS_MODULE, I2CP_CLASS_NAME);
#endif
	if (IS_ERR(i2cp_class))
		return PTR_ERR(i2cp_class);

	i2cp_class->dev_groups = i2cp_device_sysfs_groups;

	ret = alloc_chrdev_region(&i2cp_dev_num, I2CP_CDEV_BASEMINOR,
				  I2CP_CDEV_COUNT, I2CP_CHRDEV_NAME);
	if (ret < 0)
		goto fail_after_class_create;

	i2cp_device = kzalloc(sizeof(*i2cp_device), GFP_KERNEL);
	if (!i2cp_device) {
		ret = -ENOMEM;
		goto fail_after_chrdev_register;
	}

	i2cp_device->device.devt = i2cp_dev_num;
	i2cp_device->device.class = i2cp_class;
	i2cp_device->device.release = i2c_p_device_release;
	device_initialize(&i2cp_device->device);

	ret = dev_set_name(&i2cp_device->device, "%s", I2CP_DEVICE_NAME);
	if (ret < 0)
		goto fail_after_device_init;

	mutex_init(&i2cp_device->counters.lock);
	i2cp_device->counters.all_controllers = kcalloc(
		i2cp_limit, sizeof(*i2cp_device->counters.all_controllers),
		GFP_KERNEL);
	if (!i2cp_device->counters.all_controllers) {
		ret = -ENOMEM;
		goto fail_after_device_init;
	}

	cdev_init(&i2cp_device->cdev, &i2cp_fileops);
	i2cp_device->cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&i2cp_device->cdev, &i2cp_device->device);
	if (ret < 0)
		goto fail_after_device_init;

	return 0;

fail_after_device_init:
	put_device(&i2cp_device->device);
fail_after_chrdev_register:
	unregister_chrdev_region(i2cp_dev_num, I2CP_CDEV_COUNT);
fail_after_class_create:
	i2c_p_class_destroy();
	return ret;
}

static void __exit i2cp_exit(void)
{
	cdev_device_del(&i2cp_device->cdev, &i2cp_device->device);
	put_device(&i2cp_device->device);
	unregister_chrdev_region(i2cp_dev_num, I2CP_CDEV_COUNT);
	i2c_p_class_destroy();
}

MODULE_AUTHOR("Matthew Blecker <matthewb@ihavethememo.net");
MODULE_DESCRIPTION("Driver for userspace I2C adapter implementations.");
MODULE_LICENSE("GPL");
/* Keep dkms.conf PACKAGE_VERSION in sync with this. */
MODULE_VERSION("1.1");

module_init(i2cp_init);
module_exit(i2cp_exit);
