// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-pseudo.c - userspace I2C adapters
 *
 * Copyright 2023 Google LLC
 *
 * This allows for userspace implementations of functionality such as tunneling
 * I2C through another communication channel, or mocking of real I2C devices for
 * driver tests.
 */

#include <linux/build_bug.h>
#include <linux/cdev.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <uapi/linux/i2c-pseudo.h>

/* Use u32_max for params that limit or correspond to u32 UAPI fields. */
static int param_set_u32_max(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp, 0, U32_MAX);
}
static const struct kernel_param_ops param_ops_u32_max = {
	.set = param_set_u32_max,
	.get = param_get_uint,
};
#define param_check_u32_max(name, p) __param_check(name, p, unsigned int)

static unsigned int max_adapters = 1 << 7;
module_param(max_adapters, uint, 0444);
MODULE_PARM_DESC(max_adapters,
		 "Maximum number of concurrent userspace I2C adapters");

static unsigned int max_msgs_per_xfer = 1 << 7;
module_param(max_msgs_per_xfer, u32_max, 0444);
MODULE_PARM_DESC(max_msgs_per_xfer,
		 "Maximum number of I2C messages per master_xfer transaction");

static unsigned int max_total_data_per_xfer = 1 << 15;
module_param(max_total_data_per_xfer, u32_max, 0444);
MODULE_PARM_DESC(
	max_total_data_per_xfer,
	"Maximum total size of all buffers per master_xfer transaction");

static unsigned int default_timeout_ms = 3 * MSEC_PER_SEC;
module_param(default_timeout_ms, u32_max, 0444);
MODULE_PARM_DESC(
	default_timeout_ms,
	"Default I2C transaction timeout, in milliseconds. 0 for subsystem default");

static unsigned int max_timeout_ms = 10 * MSEC_PER_SEC;
module_param(max_timeout_ms, u32_max, 0444);
MODULE_PARM_DESC(max_timeout_ms,
		 "Maximum I2C transaction timeout, in milliseconds");

static struct class *i2cp_class;
static dev_t i2cp_cdev_num;
static const unsigned int i2cp_cdev_count = 1;

struct i2cp_device {
	struct cdev cdev;
	struct device device;

	/* must hold to access count_* fields */
	struct mutex count_lock;
	unsigned int count_open;
};

static struct i2cp_device *i2cp_device;

/*
 * All values must be >= 0. This should not contain any error values.
 *
 * The state for a new controller must have a zero value, so that
 * zero-initialized memory results in the correct default value.
 */
enum i2cp_state {
	I2CP_STATE_NEW = 0,
	I2CP_STATE_WAIT_FOR_XFER,
	I2CP_STATE_WAIT_FOR_REQ,
	I2CP_STATE_WAIT_FOR_REPLY,
	I2CP_STATE_XFER_RETURN,
	I2CP_STATE_RETURN_THEN_SHUTDOWN,
	I2CP_STATE_SHUTDOWN,
};

struct i2cp_controller {
	u32 functionality;
	struct i2c_adapter i2c_adapter;
	/* wake for any change to xfer_state */
	wait_queue_head_t state_wait_queue;
	/* wake for any change to I/O readiness */
	wait_queue_head_t poll_wait_queue;

	/* must hold to access xfer_* fields, except READ_ONCE(xfer_state) */
	struct mutex xfer_lock;
	/*
	 * must hold xfer_lock while writing AND use WRITE_ONCE()
	 * must hold xfer_lock while reading OR use READ_ONCE()
	 */
	enum i2cp_state xfer_state;
	struct i2cp_ioctl_xfer_counters xfer_counters;
	u64 xfer_id;
	struct i2c_msg *xfer_msgs;
	u32 xfer_num_msgs;
	int xfer_ret;
};

static bool i2cp_sum_buf_lens(const struct i2c_msg *msgs, size_t num_msgs,
			      size_t *ret)
{
	for (size_t i = 0; i < num_msgs; ++i)
		if (check_add_overflow(msgs[i].len, *ret, ret))
			return false;
	return (*ret <= max_total_data_per_xfer);
}

static inline bool i2cp_check_buf_lens(const struct i2c_msg *msgs,
				       size_t num_msgs)
{
	size_t total_buf_len;

	return i2cp_sum_buf_lens(msgs, num_msgs, &total_buf_len);
}

static inline bool i2cp_master_xfer_wait_cond(enum i2cp_state xfer_state)
{
	return xfer_state != I2CP_STATE_WAIT_FOR_REQ &&
	       xfer_state != I2CP_STATE_WAIT_FOR_REPLY;
}

static int i2cp_adapter_master_xfer(struct i2c_adapter *adap,
				    struct i2c_msg *msgs, int num)
{
	int ret = -ENOTRECOVERABLE;
	long time_left;
	struct i2cp_controller *pdata;

	pdata = adap->algo_data;
	mutex_lock(&pdata->xfer_lock);
	if (unlikely(num < 0)) {
		pdata->xfer_counters.unknown_failure++;
		ret = -EINVAL;
		goto unlock;
	}
	if ((unsigned int)num > max_msgs_per_xfer) {
		pdata->xfer_counters.too_many_msgs++;
		ret = -EMSGSIZE;
		goto unlock;
	}

	switch (pdata->xfer_state) {
	case I2CP_STATE_WAIT_FOR_XFER:
		break;
	case I2CP_STATE_SHUTDOWN:
		pdata->xfer_counters.after_shutdown++;
		ret = -ESHUTDOWN;
		goto unlock;
	default:
		pdata->xfer_counters.unknown_failure++;
		goto unlock;
	}

	if (!i2cp_check_buf_lens(msgs, num)) {
		pdata->xfer_counters.too_much_data++;
		ret = -ENOBUFS;
		goto unlock;
	}

	pdata->xfer_id++;
	pdata->xfer_msgs = msgs;
	pdata->xfer_num_msgs = num;
	pdata->xfer_ret = 0;
	WRITE_ONCE(pdata->xfer_state, I2CP_STATE_WAIT_FOR_REQ);
	mutex_unlock(&pdata->xfer_lock);

	wake_up_interruptible_sync(&pdata->state_wait_queue);
	wake_up_interruptible_sync_poll(&pdata->poll_wait_queue, EPOLLIN);
	time_left = wait_event_interruptible_timeout(
		pdata->state_wait_queue,
		i2cp_master_xfer_wait_cond(READ_ONCE(pdata->xfer_state)),
		adap->timeout);

	mutex_lock(&pdata->xfer_lock);
	switch (pdata->xfer_state) {
	case I2CP_STATE_XFER_RETURN:
		WRITE_ONCE(pdata->xfer_state, I2CP_STATE_WAIT_FOR_XFER);
		pdata->xfer_counters.controller_replied++;
		ret = pdata->xfer_ret;
		goto unlock;
	case I2CP_STATE_RETURN_THEN_SHUTDOWN:
		WRITE_ONCE(pdata->xfer_state, I2CP_STATE_SHUTDOWN);
		pdata->xfer_counters.controller_replied++;
		ret = pdata->xfer_ret;
		goto unlock;
	case I2CP_STATE_WAIT_FOR_REQ:
		if (time_left == 0)
			pdata->xfer_counters.timed_out_before_req++;
		else
			pdata->xfer_counters.interrupted_before_req++;
		break;
	case I2CP_STATE_WAIT_FOR_REPLY:
		if (time_left == 0)
			pdata->xfer_counters.timed_out_before_reply++;
		else
			pdata->xfer_counters.interrupted_before_reply++;
		break;
	case I2CP_STATE_SHUTDOWN:
		pdata->xfer_counters.after_shutdown++;
		ret = -ESHUTDOWN;
		goto unlock;
	default:
		pdata->xfer_counters.unknown_failure++;
		goto unlock;
	}

	WRITE_ONCE(pdata->xfer_state, I2CP_STATE_WAIT_FOR_XFER);
	if (time_left == 0)
		ret = -ETIMEDOUT;
	else if (time_left == -ERESTARTSYS)
		ret = -EINTR;

unlock:
	mutex_unlock(&pdata->xfer_lock);
	return ret;
}

static u32 i2cp_adapter_functionality(struct i2c_adapter *adap)
{
	return container_of(adap, struct i2cp_controller, i2c_adapter)
		->functionality;
}

static const struct i2c_algorithm i2cp_algorithm = {
	.master_xfer = i2cp_adapter_master_xfer,
	.functionality = i2cp_adapter_functionality,
};

static int i2cp_cdev_open(struct inode *inodep, struct file *filep)
{
	struct i2cp_controller *pdata;
	struct i2cp_device *i2cp_dev;

	i2cp_dev = container_of(inodep->i_cdev, struct i2cp_device, cdev);

	mutex_lock(&i2cp_dev->count_lock);
	if (i2cp_dev->count_open >= max_adapters) {
		mutex_unlock(&i2cp_dev->count_lock);
		return -ENOSPC;
	}
	/* I2C pseudo adapter controllers are not seekable. */
	stream_open(inodep, filep);
	/* Refuse fsnotify events. Modeled after /dev/ptmx implementation. */
	filep->f_mode |= FMODE_NONOTIFY;
	/* Allocate the I2C adapter. */
	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		mutex_unlock(&i2cp_dev->count_lock);
		return -ENOMEM;
	}
	i2cp_dev->count_open++;
	mutex_unlock(&i2cp_dev->count_lock);

	init_waitqueue_head(&pdata->state_wait_queue);
	init_waitqueue_head(&pdata->poll_wait_queue);
	mutex_init(&pdata->xfer_lock);

	/* Initialize the I2C adapter. */
	pdata->i2c_adapter.owner = THIS_MODULE;
	pdata->i2c_adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	pdata->i2c_adapter.algo = &i2cp_algorithm;
	pdata->i2c_adapter.algo_data = pdata;
	pdata->i2c_adapter.dev.parent = &i2cp_dev->device;
	filep->private_data = pdata;
	return 0;
}

static int i2cp_cdev_release(struct inode *inodep, struct file *filep)
{
	bool adapter_was_added = false;
	struct i2cp_controller *pdata;
	struct i2cp_device *i2cp_dev;

	pdata = filep->private_data;
	i2cp_dev = container_of(inodep->i_cdev, struct i2cp_device, cdev);

	mutex_lock(&pdata->xfer_lock);
	filep->private_data = NULL;
	if (pdata->xfer_state != I2CP_STATE_NEW) {
		/*
		 * Defer deleting the adapter until after releasing
		 * pdata->xfer_state. This avoids deadlocking with any
		 * overlapping i2cp_adapter_master_xfer() calls, which also
		 * acquire the lock in order to check the state.
		 */
		adapter_was_added = true;
		WRITE_ONCE(pdata->xfer_state,
			   (pdata->xfer_state == I2CP_STATE_XFER_RETURN) ?
				   I2CP_STATE_RETURN_THEN_SHUTDOWN :
				   I2CP_STATE_SHUTDOWN);
	}
	mutex_unlock(&pdata->xfer_lock);

	wake_up_all(&pdata->state_wait_queue);
	wake_up_all(&pdata->poll_wait_queue);

	if (adapter_was_added)
		i2c_del_adapter(&pdata->i2c_adapter);

	mutex_lock(&i2cp_dev->count_lock);
	i2cp_dev->count_open--;
	mutex_unlock(&i2cp_dev->count_lock);

	kfree(pdata);
	return 0;
}

static inline long i2cp_set_functionality(u32 functionality,
					  struct i2cp_controller *pdata)
{
	if ((functionality & I2C_FUNC_I2C) != I2C_FUNC_I2C ||
	    (functionality &
	     ~(I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING |
	       I2C_FUNC_SMBUS_EMUL)))
		return -EINVAL;
	pdata->functionality = functionality;
	return 0;
}

static inline long i2cp_set_timeout(u32 timeout_ms,
				    struct i2cp_controller *pdata)
{
	if (!timeout_ms)
		timeout_ms = default_timeout_ms;
	if (timeout_ms > max_timeout_ms)
		return -EINVAL;
	pdata->i2c_adapter.timeout = msecs_to_jiffies(timeout_ms);
	return 0;
}

static inline long i2cp_set_name(const char __user *name,
				 struct i2cp_controller *pdata)
{
	long ret;

	if (!name)
		return -EINVAL;
	ret = strncpy_from_user(pdata->i2c_adapter.name, name,
				sizeof(pdata->i2c_adapter.name) - 1);
	pdata->i2c_adapter.name[sizeof(pdata->i2c_adapter.name) - 1] = '\0';
	return ret;
}

static long i2cp_cdev_ioctl_start(struct file *filep, unsigned long arg)
{
	long ret;
	struct i2cp_ioctl_start_arg __user *user_arg;
	struct i2cp_ioctl_start_arg arg_copy;
	struct i2cp_controller *pdata;

	pdata = filep->private_data;
	mutex_lock(&pdata->xfer_lock);
	if (pdata->xfer_state != I2CP_STATE_NEW) {
		ret = -EINVAL;
		goto unlock;
	}

	user_arg = (void __user *)arg;
	if (copy_from_user(&arg_copy, user_arg, sizeof(arg_copy))) {
		ret = -EFAULT;
		goto unlock;
	}

	ret = i2cp_set_functionality(arg_copy.functionality, pdata);
	if (ret < 0)
		goto unlock;
	ret = i2cp_set_timeout(arg_copy.timeout_ms, pdata);
	if (ret < 0)
		goto unlock;
	ret = i2cp_set_name(arg_copy.name, pdata);
	if (ret < 0)
		goto unlock;
	arg_copy.output.name_len = ret;

	ret = i2c_add_adapter(&pdata->i2c_adapter);
	if (ret < 0)
		goto unlock;
	BUILD_BUG_ON(INT_MAX > U64_MAX);
	arg_copy.output.adapter_num = pdata->i2c_adapter.nr;

	BUILD_BUG_ON((void *)&arg_copy.output != (void *)&arg_copy);
	if (copy_to_user(user_arg, &arg_copy.output, sizeof(arg_copy.output))) {
		ret = -EFAULT;
		i2c_del_adapter(&pdata->i2c_adapter);
		goto unlock;
	}

	ret = 0;
	WRITE_ONCE(pdata->xfer_state, I2CP_STATE_WAIT_FOR_XFER);

unlock:
	mutex_unlock(&pdata->xfer_lock);
	return ret;
}

static void i2cp_null_bufs(struct i2c_msg *msgs, u32 num_msgs)
{
	for (u32 i = 0; i < num_msgs; ++i)
		msgs[i].buf = NULL;
}

static void i2cp_fill_bufs(struct i2c_msg *msgs, u32 num_msgs,
			   u8 *data_buf_copy, u8 __user *user_data_buf)
{
	u32 i;
	size_t pos;

	for (i = 0, pos = 0; i < num_msgs; pos += msgs[i++].len) {
		/*
		 * The data buffer is always copied, even for reads, to
		 * faithfully pass on to userspace exactly what this I2C adapter
		 * received from the I2C subsystem.
		 */
		memcpy(&data_buf_copy[pos], msgs[i].buf,
		       sizeof(*msgs[i].buf) * msgs[i].len);
		/* Set buf address for userspace. */
		msgs[i].buf = &user_data_buf[pos];
	}
}

/* Returns true if *msgs_copy should be copied to user, false if not. */
/* When false is returned, *ret _will_ be set to a negative errno value. */
/* When true is returned, *ret _may_ be set to a negative errno value. */
static bool i2cp_xfer_req_copy_data(struct i2c_msg *msgs_copy, u32 num_msgs,
				    u8 __user *user_data_buf, u32 data_buf_len,
				    long *ret)
{
	bool copy_msgs = true;
	size_t total_buf_len;
	u8 *data_buf_copy;

	if (!i2cp_sum_buf_lens(msgs_copy, num_msgs, &total_buf_len) ||
	    total_buf_len > data_buf_len) {
		i2cp_null_bufs(msgs_copy, num_msgs);
		*ret = -ENOBUFS;
		return true;
	}

	data_buf_copy =
		kzalloc(sizeof(*data_buf_copy) * total_buf_len, GFP_KERNEL);
	if (!data_buf_copy) {
		*ret = -ENOMEM;
		return false;
	}

	i2cp_fill_bufs(msgs_copy, num_msgs, data_buf_copy, user_data_buf);
	if (copy_to_user(user_data_buf, data_buf_copy,
			 sizeof(*data_buf_copy) * total_buf_len)) {
		*ret = -EFAULT;
		copy_msgs = false;
	}
	kfree(data_buf_copy);
	return copy_msgs;
}

static long i2cp_xfer_req_copy_msgs(struct i2c_msg *xfer_msgs, u32 num_msgs,
				    struct i2cp_ioctl_xfer_req_arg *arg_copy)
{
	long ret = 0;
	struct i2c_msg *msgs_copy;

	msgs_copy =
		kmemdup(xfer_msgs, sizeof(*xfer_msgs) * num_msgs, GFP_KERNEL);
	if (!msgs_copy)
		return -ENOMEM;
	if (i2cp_xfer_req_copy_data(msgs_copy, num_msgs, arg_copy->data_buf,
				    arg_copy->data_buf_len, &ret) &&
	    copy_to_user(arg_copy->msgs, msgs_copy,
			 sizeof(*msgs_copy) * num_msgs))
		ret = -EFAULT;
	kfree(msgs_copy);
	return ret;
}

static inline bool i2cp_xfer_req_wait_cond(enum i2cp_state xfer_state)
{
	return xfer_state != I2CP_STATE_WAIT_FOR_XFER &&
	       xfer_state != I2CP_STATE_WAIT_FOR_REPLY &&
	       xfer_state != I2CP_STATE_XFER_RETURN;
}

static long i2cp_cdev_ioctl_xfer_req(struct file *filep, unsigned long arg)
{
	long ret = 0;
	struct i2cp_ioctl_xfer_req_arg arg_copy;
	struct i2cp_ioctl_xfer_req_arg __user *user_arg;
	struct i2cp_controller *pdata;

	pdata = filep->private_data;
	user_arg = (void __user *)arg;
	if (copy_from_user(&arg_copy, user_arg, sizeof(arg_copy)))
		return -EFAULT;

check_xfer_state:
	mutex_lock(&pdata->xfer_lock);
	switch (pdata->xfer_state) {
	case I2CP_STATE_WAIT_FOR_REQ:
		break;
	case I2CP_STATE_NEW:
	case I2CP_STATE_WAIT_FOR_XFER:
	case I2CP_STATE_WAIT_FOR_REPLY:
	case I2CP_STATE_XFER_RETURN:
		mutex_unlock(&pdata->xfer_lock);
		if (filep->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(
			pdata->state_wait_queue,
			i2cp_xfer_req_wait_cond(READ_ONCE(pdata->xfer_state)));
		if (ret == -ERESTARTSYS)
			return ret;
		if (ret != 0)
			return -ENOTRECOVERABLE;
		/* ret == 0 */
		goto check_xfer_state;
	case I2CP_STATE_RETURN_THEN_SHUTDOWN:
	case I2CP_STATE_SHUTDOWN:
		ret = -ESHUTDOWN;
		goto unlock;
	default:
		ret = -ENOTRECOVERABLE;
		goto unlock;
	}

	arg_copy.output.xfer_id = pdata->xfer_id;
	arg_copy.output.num_msgs = pdata->xfer_num_msgs;

	BUILD_BUG_ON((void *)&arg_copy.output != (void *)&arg_copy);
	if (copy_to_user(user_arg, &arg_copy.output, sizeof(arg_copy.output))) {
		ret = -EFAULT;
		goto unlock;
	}
	if (arg_copy.msgs_len < pdata->xfer_num_msgs) {
		ret = -EMSGSIZE;
		goto unlock;
	}

	ret = i2cp_xfer_req_copy_msgs(pdata->xfer_msgs, pdata->xfer_num_msgs,
				      &arg_copy);
	if (ret >= 0) {
		WRITE_ONCE(pdata->xfer_state, I2CP_STATE_WAIT_FOR_REPLY);
		wake_up_interruptible_sync(&pdata->state_wait_queue);
		wake_up_interruptible_sync_poll(&pdata->poll_wait_queue,
						EPOLLOUT);
	}

unlock:
	mutex_unlock(&pdata->xfer_lock);
	return ret;
}

static long i2cp_cdev_ioctl_xfer_reply(struct file *filep, unsigned long arg)
{
	long ret = 0;
	struct i2c_msg *msgs_copy;
	struct i2cp_ioctl_xfer_reply_arg arg_copy;
	struct i2cp_ioctl_xfer_reply_arg __user *user_arg;
	struct i2cp_controller *pdata;

	user_arg = (void __user *)arg;
	if (copy_from_user(&arg_copy, user_arg, sizeof(arg_copy)))
		return -EFAULT;
	pdata = filep->private_data;

	mutex_lock(&pdata->xfer_lock);
	switch (pdata->xfer_state) {
	case I2CP_STATE_WAIT_FOR_REPLY:
	case I2CP_STATE_WAIT_FOR_REQ:
		break;
	case I2CP_STATE_WAIT_FOR_XFER:
		/* master_xfer already returned for pdata->xfer_id */
		ret = arg_copy.xfer_id <= pdata->xfer_id ? -ETIME : -EINVAL;
		goto unlock;
	case I2CP_STATE_XFER_RETURN:
		/* master_xfer has not yet returned for pdata->xfer_id */
		ret = arg_copy.xfer_id < pdata->xfer_id ? -ETIME : -EINVAL;
		goto unlock;
	case I2CP_STATE_RETURN_THEN_SHUTDOWN:
	case I2CP_STATE_SHUTDOWN:
		ret = -ESHUTDOWN;
		goto unlock;
	case I2CP_STATE_NEW:
		ret = -EINVAL;
		goto unlock;
	default:
		ret = -ENOTRECOVERABLE;
		goto unlock;
	}

	if (arg_copy.xfer_id != pdata->xfer_id) {
		ret = arg_copy.xfer_id < pdata->xfer_id ? -ETIME : -EINVAL;
		goto unlock;
	}
	if (arg_copy.num_msgs > pdata->xfer_num_msgs) {
		ret = -EINVAL;
		goto unlock;
	}

	msgs_copy = kcalloc(arg_copy.num_msgs, sizeof(*msgs_copy), GFP_KERNEL);
	if (!msgs_copy) {
		ret = -ENOMEM;
		goto unlock;
	}
	if (copy_from_user(msgs_copy, arg_copy.msgs,
			   sizeof(*msgs_copy) * arg_copy.num_msgs)) {
		ret = -EFAULT;
		goto unlock;
	}
	for (u32 i = 0; i < arg_copy.num_msgs; ++i) {
		if ((msgs_copy[i].flags & I2C_M_RD) &&
		    copy_from_user(pdata->xfer_msgs[i].buf, msgs_copy[i].buf,
				   pdata->xfer_msgs[i].len *
					   sizeof(*(msgs_copy[i].buf)))) {
			ret = -EFAULT;
			goto unlock;
		}
	}

	if (arg_copy.error > 0)
		pdata->xfer_ret = min(-1, -(int)arg_copy.error);
	else
		pdata->xfer_ret = max(0, (int)arg_copy.num_msgs);

	WRITE_ONCE(pdata->xfer_state, I2CP_STATE_XFER_RETURN);
	wake_up_interruptible_sync(&pdata->state_wait_queue);

unlock:
	mutex_unlock(&pdata->xfer_lock);
	return ret;
}

static long i2cp_cdev_ioctl_get_counters(struct file *filep, unsigned long arg)
{
	long ret;
	struct i2cp_ioctl_xfer_counters __user *user_arg;
	struct i2cp_controller *pdata;

	pdata = filep->private_data;
	user_arg = (void __user *)arg;

	mutex_lock(&pdata->xfer_lock);
	ret = copy_to_user(user_arg, &pdata->xfer_counters,
			   sizeof(pdata->xfer_counters)) ?
		      -EFAULT :
		      0;
	mutex_unlock(&pdata->xfer_lock);
	return ret;
}

static long i2cp_cdev_ioctl_shutdown(struct file *filep, unsigned long arg)
{
	struct i2cp_controller *pdata;

	if (arg)
		return -EINVAL;
	pdata = filep->private_data;

	mutex_lock(&pdata->xfer_lock);
	WRITE_ONCE(pdata->xfer_state,
		   (pdata->xfer_state == I2CP_STATE_XFER_RETURN) ?
			   I2CP_STATE_RETURN_THEN_SHUTDOWN :
			   I2CP_STATE_SHUTDOWN);
	mutex_unlock(&pdata->xfer_lock);

	wake_up_interruptible_all(&pdata->state_wait_queue);
	wake_up_interruptible_all(&pdata->poll_wait_queue);
	return 0;
}

static long i2cp_cdev_unlocked_ioctl(struct file *filep, unsigned int cmd,
				     unsigned long arg)
{
	switch (cmd) {
	case I2CP_IOCTL_XFER_REQ:
		return i2cp_cdev_ioctl_xfer_req(filep, arg);
	case I2CP_IOCTL_XFER_REPLY:
		return i2cp_cdev_ioctl_xfer_reply(filep, arg);
	case I2CP_IOCTL_GET_COUNTERS:
		return i2cp_cdev_ioctl_get_counters(filep, arg);
	case I2CP_IOCTL_START:
		return i2cp_cdev_ioctl_start(filep, arg);
	case I2CP_IOCTL_SHUTDOWN:
		return i2cp_cdev_ioctl_shutdown(filep, arg);
	}
	return -ENOIOCTLCMD;
}

/*
 * EPOLLIN indicates xfer request waiting for I2CP_IOCTL_XFER_REQ. This is what
 * pollers will normally wait for in conjunction with O_NONBLOCK.
 *
 * EPOLLOUT indicates xfer request waiting for I2CP_IOCTL_XFER_REPLY. This is
 * always the case immediately after successful I2CP_IOCTL_XFER_REQ, so polling
 * for this is unnecessary, it is safe and recommended to call
 * I2CP_IOCTL_XFER_REPLY as soon as a reply is ready.
 *
 * EPOLLHUP indicates I2CP_IOCTL_SHUTDOWN was called.
 */
static __poll_t i2cp_cdev_poll(struct file *filep, poll_table *ptp)
{
	__poll_t poll_ret;
	struct i2cp_controller *pdata;

	pdata = filep->private_data;
	poll_wait(filep, &pdata->poll_wait_queue, ptp);

	mutex_lock(&pdata->xfer_lock);
	switch (pdata->xfer_state) {
	case I2CP_STATE_WAIT_FOR_REQ:
		poll_ret = EPOLLIN;
		break;
	case I2CP_STATE_WAIT_FOR_REPLY:
		poll_ret = EPOLLOUT;
		break;
	case I2CP_STATE_RETURN_THEN_SHUTDOWN:
	case I2CP_STATE_SHUTDOWN:
		poll_ret = EPOLLHUP;
		break;
	default:
		poll_ret = 0;
	}
	mutex_unlock(&pdata->xfer_lock);

	return poll_ret;
}

static const struct file_operations i2cp_fileops = {
	.owner = THIS_MODULE,
	.open = i2cp_cdev_open,
	.release = i2cp_cdev_release,
	.unlocked_ioctl = i2cp_cdev_unlocked_ioctl,
	.poll = i2cp_cdev_poll,
	.llseek = no_llseek,
};

static ssize_t i2cp_open_count_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned int count;
	struct i2cp_device *i2cp_dev;

	i2cp_dev = container_of(dev, struct i2cp_device, device);

	mutex_lock(&i2cp_dev->count_lock);
	count = i2cp_dev->count_open;
	mutex_unlock(&i2cp_dev->count_lock);

	ret = snprintf(buf, PAGE_SIZE, "%u\n", count);
	if (ret >= PAGE_SIZE)
		return -ERANGE;
	return ret;
}

static struct device_attribute i2cp_open_count_dev_attr = {
	.attr = {
		.name = "open_count",
		.mode = 0444,
	},
	.show = i2cp_open_count_show,
};

static struct attribute *i2cp_device_sysfs_attrs[] = {
	&i2cp_open_count_dev_attr.attr,
	NULL,
};

static const struct attribute_group i2cp_device_sysfs_group = {
	.attrs = i2cp_device_sysfs_attrs,
};

static const struct attribute_group *i2cp_device_sysfs_groups[] = {
	&i2cp_device_sysfs_group,
	NULL,
};

static inline void i2cp_device_release(struct device *dev)
{
	struct i2cp_device *i2cp_dev;

	i2cp_dev = container_of(dev, struct i2cp_device, device);
	kfree(i2cp_dev);
}

static int __init i2cp_init(void)
{
	int ret;

/* TODO: upstream patch: remove #if LINUX_VERSION_CODE */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	i2cp_class = class_create(THIS_MODULE, "i2c-pseudo");
#else
	i2cp_class = class_create("i2c-pseudo");
#endif
	if (IS_ERR(i2cp_class))
		return PTR_ERR(i2cp_class);
	i2cp_class->dev_groups = i2cp_device_sysfs_groups;

	ret = alloc_chrdev_region(&i2cp_cdev_num, 0, i2cp_cdev_count,
				  "i2c_pseudo");
	if (ret < 0)
		goto fail_after_class_create;

	i2cp_device = kzalloc(sizeof(*i2cp_device), GFP_KERNEL);
	if (!i2cp_device) {
		ret = -ENOMEM;
		goto fail_after_chrdev_register;
	}

	i2cp_device->device.devt = i2cp_cdev_num;
	i2cp_device->device.class = i2cp_class;
	i2cp_device->device.release = i2cp_device_release;
	device_initialize(&i2cp_device->device);
	ret = dev_set_name(&i2cp_device->device, "i2c-pseudo");
	if (ret < 0)
		goto fail_after_device_init;

	mutex_init(&i2cp_device->count_lock);
	cdev_init(&i2cp_device->cdev, &i2cp_fileops);
	i2cp_device->cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&i2cp_device->cdev, &i2cp_device->device);
	if (ret >= 0)
		return 0;

fail_after_device_init:
	put_device(&i2cp_device->device);
fail_after_chrdev_register:
	unregister_chrdev_region(i2cp_cdev_num, i2cp_cdev_count);
fail_after_class_create:
	class_destroy(i2cp_class);
	return ret;
}

static void __exit i2cp_exit(void)
{
	cdev_device_del(&i2cp_device->cdev, &i2cp_device->device);
	put_device(&i2cp_device->device);
	unregister_chrdev_region(i2cp_cdev_num, i2cp_cdev_count);
	class_destroy(i2cp_class);
}

MODULE_AUTHOR("Matthew Blecker <matthewb@chromium.org");
MODULE_DESCRIPTION("Driver for userspace I2C adapters");
MODULE_LICENSE("GPL");
/* TODO: upstream patch: remove comment about dkms.conf */
/* Keep dkms.conf PACKAGE_VERSION in sync with this. */
MODULE_VERSION("2.4");

module_init(i2cp_init);
module_exit(i2cp_exit);
