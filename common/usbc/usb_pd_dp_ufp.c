/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Functions required for UFP_D operation
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_dp_ufp.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

enum hpd_state {
	LOW_WAIT,
	HIGH_CHECK,
	HIGH_WAIT,
	LOW_CHECK,
	IRQ_CHECK,
};

#define EDGE_QUEUE_DEPTH BIT(3)
#define EDGE_QUEUE_MASK (EDGE_QUEUE_DEPTH - 1)
#define HPD_QUEUE_DEPTH BIT(2)
#define HPD_QUEUE_MASK (HPD_QUEUE_DEPTH - 1)
#define HPD_T_IRQ_MIN_PULSE 250
#define HPD_T_IRQ_MAX_PULSE (2 * MSEC)
#define HPD_T_MIN_DP_ATTEN (10 * MSEC)

struct hpd_mark {
	int level;
	uint64_t ts;
};

struct hpd_edge {
	int overflow;
	uint32_t head;
	uint32_t tail;
	struct hpd_mark buffer[EDGE_QUEUE_DEPTH];
};

struct hpd_info {
	enum hpd_state state;
	int count;
	int send_enable;
	uint64_t timer;
	uint64_t last_send_ts;
	enum hpd_event queue[HPD_QUEUE_DEPTH];
	struct hpd_edge edges;
};

static struct hpd_info hpd;
static struct mutex hpd_mutex;

static int alt_dp_mode_opos[CONFIG_USB_PD_PORT_MAX_COUNT];

void pd_ufp_set_dp_opos(int port, int opos)
{
	alt_dp_mode_opos[port] = opos;
}

int pd_ufp_get_dp_opos(int port)
{
	return alt_dp_mode_opos[port];
}

void pd_ufp_enable_hpd_send(int port)
{
	/*
	 * This control is used ensure that a DP_ATTENTION message is not sent
	 * to the DFP-D before a DP_CONFIG messaage has been received. This
	 * control is not strictly required by the spec, but some port partners
	 * will get confused if DP_ATTENTION is sent prior to DP_CONFIG.
	 */
	hpd.send_enable = 1;
}

static void hpd_to_dp_attention(void)
{
	int port = hpd_config.port;
	int evt_index = hpd.count - 1;
	uint32_t vdm[2];
	uint32_t svdm_header;
	enum hpd_event evt;
	int opos = pd_ufp_get_dp_opos(port);

	if (!opos)
		return;

	/* Get the next hpd event from the queue */
	evt = hpd.queue[evt_index];
	/* Save timestamp of when most recent DP attention message was sent */
	hpd.last_send_ts = get_time().val;

	/*
	 * Construct DP Attention message. This consists of the VDM header and
	 * the DP_STATUS VDO.
	 */
	svdm_header = VDO_SVDM_VERS_MAJOR(pd_get_vdo_ver(port, TCPCI_MSG_SOP)) |
		      VDO_OPOS(opos) | CMD_ATTENTION;
	vdm[0] = VDO(USB_SID_DISPLAYPORT, 1, svdm_header);

	vdm[1] = VDO_DP_STATUS((evt == hpd_irq), /* IRQ_HPD */
			       (evt != hpd_low), /* HPD_HI|LOW */
			       0, /* request exit DP */
			       0, /* request exit USB */
			       dock_get_mf_preference(), /* MF pref */
			       1, /* enabled */
			       0, /* power low */
			       0x2);

	/* Send request to DPM to send an attention VDM */
	pd_request_vdm(port, vdm, ARRAY_SIZE(vdm), TCPCI_MSG_SOP);

	/* If there are still events, need to shift the buffer */
	if (--hpd.count) {
		int i;

		for (i = 0; i < hpd.count; i++)
			hpd.queue[i] = hpd.queue[i + 1];
	}
}

static void hpd_queue_event(enum hpd_event evt)
{
	/*
	 * HPD events are put into a queue. However, this queue is not a typical
	 * FIFO queue. Instead there are special rules based on which type of
	 * event is being added.
	 *     HPD_LOW -> always resets the queue and must be in slot 0
	 *     HPD_HIGH -> must follow a HPD_LOW, so can only be in slot 0 or
	 *                 slot 1.
	 *     HPD_IRQ  -> There shall never be more than 2 HPD_IRQ events
	 *                 stored in the queue and HPD_IRQ must follow HPD_HIGH
	 *
	 * Worst case for queueing HPD events is 4 events in the queue:
	 *    0 - HPD_LOW
	 *    1 - HPD_HIGH
	 *    2 - HPD_IRQ
	 *    3 - HPD_IRQ
	 *
	 * The above rules mean that HPD_LOW and HPD_HIGH events can always be
	 * added to the queue since high must follow low and a low event resets
	 * the queue. HPD_IRQ events are checked to make sure that they don't
	 * overflow the queue and to ensure that no more than 2 hpd_irq events
	 * are kept in the queue.
	 */
	if (evt == hpd_irq) {
		if ((hpd.count >= HPD_QUEUE_DEPTH) ||
		    ((hpd.count >= 2) &&
		     (hpd.queue[hpd.count - 2] == hpd_irq))) {
			CPRINTS("hpd: discard hpd: count - %d", hpd.count);
			return;
		}
	}

	if (evt == hpd_low) {
		hpd.count = 0;
	}

	/* Add event to the queue */
	hpd.queue[hpd.count++] = evt;
}

static void hpd_to_pd_converter(int level, uint64_t ts)
{
	/*
	 * HPD edges are marked in the irq routine. The converter state machine
	 * runs in the hooks task and so there will be some delay between when
	 * the edge was captured and when that edge is processed here in the
	 * state machine. This means that the delitch timer (250 uSec) may have
	 * already expired or is about to expire.
	 *
	 * If transitioning to timing dependent state, need to ensure the state
	 * machine is executed again. All timers are relative to the ts value
	 * passed into this routine. The timestamps passed into this routine
	 * are either the values latched in the irq routine, or the current
	 * time latched by the calling function. From the perspective of the
	 * state machine, ts represents the current time.
	 *
	 * Note that all hpd queue events are contingent on detecting edges
	 * on the incoming hpd gpio signal. The hpd->dp attention converter is
	 * enabled/disabled as part of the svdm dp enter/exit response handler
	 * functions. When the converter is disabled, gpio interrupts for the
	 * hpd gpio signal are disabled so it will never execute, unless the
	 * converter is enabled, and the converter is only enabled when the
	 * UFP-D is actively in ALT-DP mode.
	 */
	switch (hpd.state) {
	case LOW_WAIT:
		/*
		 * In this state only expected event is a level change from low
		 * to high.
		 */
		if (level) {
			hpd.state = HIGH_CHECK;
			hpd.timer = ts + HPD_T_IRQ_MIN_PULSE;
		}
		break;
	case HIGH_CHECK:
		/*
		 * In this state if level is high and deglitch timer is
		 * exceeded, then state advances to HIGH_WAIT, otherwise return
		 * to LOW_WAIT state.
		 */
		if (!level || (ts <= hpd.timer)) {
			hpd.state = LOW_WAIT;
		} else {
			hpd.state = HIGH_WAIT;
			hpd_queue_event(hpd_high);
		}
		break;
	case HIGH_WAIT:
		/*
		 * In this state, only expected event is a level change from
		 * high to low. If current level is low, then advance to
		 * LOW_CHECK for deglitch checking.
		 */
		if (!level) {
			hpd.state = LOW_CHECK;
			hpd.timer = ts + HPD_T_IRQ_MIN_PULSE;
		}
		break;
	case LOW_CHECK:
		/*
		 * This state is used to deglitch high->low level
		 * change. However, due to processing latency, it's possible to
		 * detect hpd_irq event if level is high and low pulse width was
		 * valid.
		 */
		if (!level) {
			/* Still low, now wait for IRQ or LOW determination */
			hpd.timer = ts +
				    (HPD_T_IRQ_MAX_PULSE - HPD_T_IRQ_MIN_PULSE);
			hpd.state = IRQ_CHECK;

		} else {
			uint64_t irq_ts = hpd.timer + HPD_T_IRQ_MAX_PULSE -
					  HPD_T_IRQ_MIN_PULSE;
			/*
			 * If hpd is high now, this must have been an edge
			 * event, but still need to determine if the pulse width
			 * is longer than hpd_irq min pulse width. State will
			 * advance to HIGH_WAIT, but if pulse width is < 2 msec,
			 * must send hpd_irq event.
			 */
			if ((ts >= hpd.timer) && (ts <= irq_ts)) {
				/* hpd irq detected */
				hpd_queue_event(hpd_irq);
			}
			hpd.state = HIGH_WAIT;
		}
		break;
	case IRQ_CHECK:
		/*
		 * In this state deglitch time has already passed. If current
		 * level is low and hpd_irq timer has expired, then go to
		 * LOW_WAIT as hpd_low event has been detected. If level is high
		 * and low pulse is < hpd_irq, hpd_irq event has been detected.
		 */
		if (level) {
			hpd.state = HIGH_WAIT;
			if (ts <= hpd.timer) {
				hpd_queue_event(hpd_irq);
			}
		} else if (ts > hpd.timer) {
			hpd.state = LOW_WAIT;
			hpd_queue_event(hpd_low);
		}
		break;
	}
}

static void manage_hpd(void);
DECLARE_DEFERRED(manage_hpd);

static void manage_hpd(void)
{
	int level;
	uint64_t ts = get_time().val;
	uint32_t num_hpd_events = (hpd.edges.head - hpd.edges.tail) &
				  EDGE_QUEUE_MASK;

	/*
	 * HPD edges are detected via GPIO interrupts. The ISR routine adds edge
	 * info to a queue and scheudles this routine. If this routine is called
	 * without a new edge detected, then it is being called due to a timer
	 * event.
	 */

	/* First check to see overflow condition has occurred */
	if (hpd.edges.overflow) {
		/* Disable hpd interrupts */
		usb_pd_hpd_converter_enable(0);
		/* Re-enable hpd converter */
		usb_pd_hpd_converter_enable(1);
	}

	if (num_hpd_events) {
		while (num_hpd_events-- > 0) {
			int idx = hpd.edges.tail;

			level = hpd.edges.buffer[idx].level;
			ts = hpd.edges.buffer[idx].ts;

			hpd_to_pd_converter(level, ts);
			hpd.edges.tail = (hpd.edges.tail + 1) & EDGE_QUEUE_MASK;
		}
	} else {
		/* no new edge event, so get current time and level */
		level = gpio_get_level(hpd_config.signal);
		ts = get_time().val;
		hpd_to_pd_converter(level, ts);
	}

	/*
	 * If min time spacing requirement is exceeded and a hpd_event is
	 * queued, then send DP_ATTENTION message.
	 */
	if (hpd.count > 0) {
		/*
		 * If at least one hpd event is pending in the queue, send
		 * a DP_ATTENTION message if a DP_CONFIG message has been
		 * received and have passed the minimum spacing interval.
		 */
		if (hpd.send_enable && ((get_time().val - hpd.last_send_ts) >
					HPD_T_MIN_DP_ATTEN)) {
			/* Generate DP_ATTENTION event pending in queue */
			hpd_to_dp_attention();
		} else {
			uint32_t callback_us;

			/*
			 * Need to wait until until min spacing requirement of
			 * DP attention messages. Set callback time to the min
			 * value required. This callback time could be changed
			 * based on hpd interrupts.
			 *
			 * This wait is also used to prevent a DP_ATTENTION
			 * message from being sent before at least one DP_CONFIG
			 * message has been received. If DP_ATTENTION messages
			 * need to be delayed for this reason, then just wait
			 * the minimum time spacing.
			 */
			callback_us = HPD_T_MIN_DP_ATTEN -
				      (get_time().val - hpd.last_send_ts);
			if (callback_us <= 0 ||
			    callback_us > HPD_T_MIN_DP_ATTEN)
				callback_us = HPD_T_MIN_DP_ATTEN;
			hook_call_deferred(&manage_hpd_data, callback_us);
		}
	}

	/*
	 * Because of the delay between gpio edge irq, and when those edge
	 * events are processed here, all timers must be done relative to the
	 * timing marker stored in the hpd edge queue. If the state machine
	 * required a new timer, then hpd.timer will be advanced relative to the
	 * ts that was passed into the state machine.
	 *
	 * If the deglitch timer is active, then it can likely already have been
	 * expired when the edge gets processed. So if the timer is active the
	 * deferred callback must be requested.
	 *.
	 */
	if (hpd.timer > ts) {
		uint64_t callback_us = 0;
		uint64_t now = get_time().val;

		/* If timer is in the future, adjust the callback timer */
		if (now < hpd.timer)
			callback_us = (hpd.timer - now) & 0xffffffff;

		hook_call_deferred(&manage_hpd_data, callback_us);
	}
}

void usb_pd_hpd_converter_enable(int enable)
{
	/*
	 * The hpd converter should be enabled as part of the UFP-D enter mode
	 * response function. Likewise, the converter should be disabled by the
	 * exit mode function. In addition, the coverter may get disabled so
	 * that it can be reset in the case that the input gpio edges queue
	 * overflows. A muxtex must be used here since this function may be
	 * called from the PD task (enter/exit response mode functions) or from
	 * the hpd event handler state machine (hook task).
	 */
	mutex_lock(&hpd_mutex);

	if (enable) {
		gpio_disable_interrupt(hpd_config.signal);
		/* Reset HPD event queue */
		hpd.state = LOW_WAIT;
		hpd.count = 0;
		hpd.timer = 0;
		hpd.last_send_ts = 0;
		hpd.send_enable = 0;

		/* Reset hpd signal edges queue */
		hpd.edges.head = 0;
		hpd.edges.tail = 0;
		hpd.edges.overflow = 0;

		/* If signal is high, need to ensure state machine executes */
		if (gpio_get_level(hpd_config.signal))
			hook_call_deferred(&manage_hpd_data, 0);

		/* Enable hpd edge detection */
		gpio_enable_interrupt(hpd_config.signal);
	} else {
		gpio_disable_interrupt(hpd_config.signal);
		hook_call_deferred(&manage_hpd_data, -1);
	}

	mutex_unlock(&hpd_mutex);
}

void usb_pd_hpd_edge_event(int signal)
{
	int next_head = (hpd.edges.head + 1) & EDGE_QUEUE_MASK;
	struct hpd_mark mark;

	/* Get current timestamp and level */
	mark.ts = get_time().val;
	mark.level = gpio_get_level(hpd_config.signal);

	/* Add this edge to the buffer if there is space */
	if (next_head != hpd.edges.tail) {
		hpd.edges.buffer[hpd.edges.head].ts = mark.ts;
		hpd.edges.buffer[hpd.edges.head].level = mark.level;
		hpd.edges.head = next_head;
	} else {
		/* Edge queue is overflowing, need to reset the converter */
		hpd.edges.overflow = 1;
	}
	/* Schedule HPD state machine to run ASAP */
	hook_call_deferred(&manage_hpd_data, 0);
}
