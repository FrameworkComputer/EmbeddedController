/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "debug.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

/* ------------------------- Power supply control ------------------------ */

/* GPIO level setting helpers through BSRR register */
#define GPIO_SET(n)   (1 << (n))
#define GPIO_RESET(n) (1 << ((n) + 16))

/* Output voltage selection */
enum volt {
	VO_5V  = GPIO_RESET(13) | GPIO_RESET(14),
	VO_12V = GPIO_SET(13)   | GPIO_RESET(14),
	VO_13V = GPIO_RESET(13) | GPIO_SET(14),
	VO_20V = GPIO_SET(13)   | GPIO_SET(14),
};

static inline void set_output_voltage(enum volt v)
{
	/* set voltage_select on PA13/PA14 */
	STM32_GPIO_BSRR(GPIO_A) = v;
}

static inline void output_enable(void)
{
	/* GPF0 (enable OR'ing FETs) = 1 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_SET(0);
}

static inline void output_disable(void)
{
	/* GPF0 (disable OR'ing FETs) = 0 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_RESET(0);
}

static inline int output_is_enabled(void)
{
	/* GPF0 = enable output FET */
	return STM32_GPIO_IDR(GPIO_F) & 1;
}

/* ----- fault conditions ----- */

enum faults {
	FAULT_OK = 0,
	FAULT_OCP, /* Over-Current Protection */
	FAULT_FAST_OCP, /* Over-Current Protection for interrupt context */
	FAULT_OVP, /* Under or Over-Voltage Protection */
	FAULT_DISCHARGE, /* Discharge was ineffective */
};

/* current fault condition */
static enum faults fault;
/* expiration date of the last fault condition */
static timestamp_t fault_deadline;

/* ADC in 12-bit mode */
#define ADC_SCALE (1 << 12)
/* ADC power supply : VDDA = 3.3V */
#define VDDA_MV   3300
/* Current sense resistor : 5 milliOhm */
#define R_SENSE   5
/* VBUS voltage is measured through 10k / 100k voltage divider = /11 */
#define VOLT_DIV  ((10+100)/10)
/* The current sensing op-amp has a x100 gain */
#define CURR_GAIN 100
/* convert VBUS voltage in raw ADC value */
#define VBUS_MV(mv) ((mv)*ADC_SCALE/VOLT_DIV/VDDA_MV)
/* convert VBUS current in raw ADC value */
#define VBUS_MA(ma) ((ma)*ADC_SCALE*R_SENSE/1000*CURR_GAIN/VDDA_MV)
/* convert raw ADC value to mA */
#define ADC_TO_CURR_MA(vbus) ((vbus)*1000/(ADC_SCALE*R_SENSE)*VDDA_MV/CURR_GAIN)
/* convert raw ADC value to mV */
#define ADC_TO_VOLT_MV(vbus) ((vbus)*VOLT_DIV*VDDA_MV/ADC_SCALE)

/* Max current */
#if defined(BOARD_ZINGER)
#define RATED_CURRENT 3000
#elif defined(BOARD_MINIMUFFIN)
#define RATED_CURRENT 2250
#endif

/* Max current : 20% over rated current */
#define MAX_CURRENT VBUS_MA(RATED_CURRENT * 6/5)
/* Fast short circuit protection : 50% over rated current */
#define MAX_CURRENT_FAST VBUS_MA(RATED_CURRENT * 3/2)
/* reset over-current after 1 second */
#define OCP_TIMEOUT SECOND

/* Threshold below which we stop fast OCP to save power */
#define SINK_IDLE_CURRENT VBUS_MA(500 /* mA */)

/* Under-voltage limit is 0.8x Vnom */
#define UVP_MV(mv)  VBUS_MV((mv) * 8 / 10)
/* Over-voltage limit is 1.2x Vnom */
#define OVP_MV(mv)  VBUS_MV((mv) * 12 / 10)
/* Over-voltage recovery threshold is 1.1x Vnom */
#define OVP_REC_MV(mv)  VBUS_MV((mv) * 11 / 10)

/* Maximum discharging delay */
#define DISCHARGE_TIMEOUT (90*MSEC)
/* Voltage overshoot below the OVP threshold for discharging to avoid OVP */
#define DISCHARGE_OVERSHOOT_MV VBUS_MV(200)

/* ----- output voltage discharging ----- */

/* expiration date of the discharge */
static timestamp_t discharge_deadline;

static inline void discharge_enable(void)
{
	STM32_GPIO_BSRR(GPIO_F) = GPIO_SET(1);
}

static inline void discharge_disable(void)
{
	STM32_GPIO_BSRR(GPIO_F) = GPIO_RESET(1);
	adc_disable_watchdog();
}

static inline int discharge_is_enabled(void)
{
	/* GPF1 = enable discharge FET */
	return STM32_GPIO_IDR(GPIO_F) & 2;
}

static void discharge_voltage(int target_volt)
{
	discharge_enable();
	discharge_deadline.val = get_time().val + DISCHARGE_TIMEOUT;
	/* Monitor VBUS voltage */
	target_volt -= DISCHARGE_OVERSHOOT_MV;
	disable_sleep(SLEEP_MASK_USB_PD);
	adc_enable_watchdog(ADC_CH_V_SENSE, 0xFFF, target_volt);
}

/* ----------------------- USB Power delivery policy ---------------------- */

#define PDO_FIXED_FLAGS (PDO_FIXED_EXTERNAL | PDO_FIXED_DATA_SWAP)

/* Power Delivery Objects */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  RATED_CURRENT, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, RATED_CURRENT, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, RATED_CURRENT, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* PDO voltages (should match the table above) */
static const struct {
	enum volt select; /* GPIO configuration to select the voltage */
	int       uvp;    /* under-voltage limit in mV */
	int       ovp;    /* over-voltage limit in mV */
	int       ovp_rec;/* over-voltage recovery threshold in mV */
} voltages[ARRAY_SIZE(pd_src_pdo)] = {
	{VO_5V,  UVP_MV(5000),  OVP_MV(5000), OVP_REC_MV(5000)},
	{VO_12V, UVP_MV(12000), OVP_MV(12000), OVP_REC_MV(12000)},
	{VO_20V, UVP_MV(20000), OVP_MV(20000), OVP_REC_MV(20000)},
};

/* current and previous selected PDO entry */
static int volt_idx;
static int last_volt_idx;

/* output current measurement */
int vbus_amp;

int pd_check_requested_voltage(uint32_t rdo)
{
	int op_ma = rdo & 0x3FF;
	int max_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	/* fault condition or output disabled: reject transitions */
	if (fault != FAULT_OK || !output_is_enabled())
		return EC_ERROR_INVAL;

	if (!idx || idx > pd_src_pdo_cnt)
		return EC_ERROR_INVAL; /* Invalid index */

	/* check current ... */
	pdo = pd_src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);
	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */
	if (max_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much max current */

	debug_printf("Requested %d V %d mA (for %d/%d mA)\n",
		     ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		     ((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);
	/* Accept the requested voltage */
	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	if (idx - 1 < volt_idx) { /* down voltage transition */
		/* Stop OCP monitoring */
		adc_disable_watchdog();

		discharge_voltage(voltages[idx - 1].ovp);
	} else if (idx - 1 > volt_idx) { /* up voltage transition */
		if (discharge_is_enabled()) {
			/* Make sure discharging is disabled */
			discharge_disable();
			/* Enable over-current monitoring */
			adc_enable_watchdog(ADC_CH_A_SENSE,
					    MAX_CURRENT_FAST, 0);
		}
	}
	last_volt_idx = volt_idx;
	volt_idx = idx - 1;
	set_output_voltage(voltages[volt_idx].select);
}

int pd_set_power_supply_ready(int port)
{
	/* fault condition not cleared : do not turn on power */
	if ((fault != FAULT_OK) || discharge_is_enabled())
		return EC_ERROR_INVAL;

	output_enable();
	/* Over-current monitoring */
	adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST, 0);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	int need_discharge = (volt_idx > 0) || discharge_is_enabled();

	output_disable();
	volt_idx = 0;
	set_output_voltage(VO_5V);
	/* TODO transition delay */

	/* Stop OCP monitoring to save power */
	adc_disable_watchdog();

	/* discharge voltage to 5V ? */
	if (need_discharge)
		discharge_voltage(voltages[0].ovp);
}

int pd_check_data_swap(int port, int data_role)
{
	/* Allow data swap if we are a DFP, otherwise don't allow */
	return (data_role == PD_ROLE_DFP) ? 1 : 0;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

void pd_new_contract(int port, int pr_role, int dr_role,
		     int partner_pr_swap, int partner_dr_swap)
{
	/* If DFP, try to switch to UFP */
	if (partner_dr_swap && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
}

int pd_board_checks(void)
{
#ifdef CONFIG_HIBERNATE
	static timestamp_t hib_to;
	static int hib_to_ready;
#endif
	int vbus_volt;
	int ovp_idx;

	/* Reload the watchdog */
	STM32_IWDG_KR = STM32_IWDG_KR_RELOAD;

#ifdef CONFIG_HIBERNATE
	/* If output is disabled for long enough, then hibernate */
	if (!pd_is_connected(0) && hib_to_ready) {
		if (get_time().val >= hib_to.val) {
			debug_printf("hibernate\n");
			__enter_hibernate(0, 0);
		}
	} else {
		hib_to.val = get_time().val + 60*SECOND;
		hib_to_ready = 1;
	}
#endif

	vbus_volt = adc_read_channel(ADC_CH_V_SENSE);
	vbus_amp = adc_read_channel(ADC_CH_A_SENSE);

	if (fault == FAULT_FAST_OCP) {
		debug_printf("Fast OverCurrent\n");
		fault = FAULT_OCP;
		/* reset over-current after 1 second */
		fault_deadline.val = get_time().val + OCP_TIMEOUT;
		return EC_ERROR_INVAL;
	}

	if (vbus_amp > MAX_CURRENT) {
		/* 3 more samples to check whether this is just a transient */
		int count;
		for (count = 0; count < 3; count++)
			if (adc_read_channel(ADC_CH_A_SENSE) < MAX_CURRENT)
				break;
		/* trigger the slow OCP iff all 4 samples are above the max */
		if (count == 3) {
			debug_printf("OverCurrent : %d mA\n",
			  vbus_amp * VDDA_MV / CURR_GAIN * 1000
				   / R_SENSE / ADC_SCALE);
			fault = FAULT_OCP;
			/* reset over-current after 1 second */
			fault_deadline.val = get_time().val + OCP_TIMEOUT;
			return EC_ERROR_INVAL;
		}
	}
	/*
	 * Optimize power consumption when the sink is idle :
	 * Enable STOP mode while we are connected,
	 * this kills fast OCP as the actual ADC conversion for the analog
	 * watchdog will happen on the next wake-up (x0 ms latency).
	 */
	if (vbus_amp < SINK_IDLE_CURRENT && !discharge_is_enabled())
		/* override the PD state machine sleep mask */
		enable_sleep(SLEEP_MASK_USB_PD);
	else if (vbus_amp > SINK_IDLE_CURRENT)
		disable_sleep(SLEEP_MASK_USB_PD);

	/*
	 * Set the voltage index to use for checking OVP. During a down step
	 * transition, use the previous voltage index to check for OVP.
	 */
	ovp_idx = discharge_is_enabled() ? last_volt_idx : volt_idx;

	if ((output_is_enabled() && (vbus_volt > voltages[ovp_idx].ovp)) ||
	    (fault && (vbus_volt > voltages[ovp_idx].ovp_rec))) {
		if (!fault)
			debug_printf("OverVoltage : %d mV\n",
				     ADC_TO_VOLT_MV(vbus_volt));
		fault = FAULT_OVP;
		/* no timeout */
		fault_deadline.val = get_time().val;
		return EC_ERROR_INVAL;
	}

	/* the discharge did not work properly */
	if (discharge_is_enabled() &&
		(get_time().val > discharge_deadline.val)) {
		/* stop it */
		discharge_disable();
		/* enable over-current monitoring */
		adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST, 0);
		debug_printf("Discharge failure : %d mV\n",
			     ADC_TO_VOLT_MV(vbus_volt));
		fault = FAULT_DISCHARGE;
		/* reset it after 1 second */
		fault_deadline.val = get_time().val + OCP_TIMEOUT;
	}

	/* everything is good *and* the error condition has expired */
	if ((fault != FAULT_OK) && (get_time().val > fault_deadline.val)) {
		fault = FAULT_OK;
		debug_printf("Reset fault\n");
		/*
		 * Reset the PD state and communication on both side,
		 * so we can now re-negociate a voltage.
		 */
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;

}

void pd_adc_interrupt(void)
{
	/* Clear flags */
	STM32_ADC_ISR = 0x8e;

	if (discharge_is_enabled()) { /* discharge completed */
		discharge_disable();
		/* enable over-current monitoring */
		adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST, 0);
	} else {/* Over-current detection */
		/* cut the power output */
		pd_power_supply_reset(0);
		/* record a special fault */
		fault = FAULT_FAST_OCP;
		/* pd_board_checks() will record the timeout later */
	}

	/* clear ADC irq so we don't get a second interrupt */
	task_clear_pending_irq(STM32_IRQ_ADC_COMP);
}
DECLARE_IRQ(STM32_IRQ_ADC_COMP, pd_adc_interrupt, 1);

/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(0, /* data caps as USB host */
				 0, /* data caps as USB device */
				 IDH_PTYPE_UNDEF, /* Undefined */
				 1, /* supports alt modes */
				 USB_VID_GOOGLE);

const uint32_t vdo_product = VDO_PRODUCT(CONFIG_USB_PID, CONFIG_USB_BCD_DEV);

/* When set true, we are in GFU mode */
static int gfu_mode;

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;
	return VDO_I(PRODUCT) + 1;
}

static int svdm_response_svids(int port, uint32_t *payload)
{
	payload[1] = VDO_SVID(USB_VID_GOOGLE, 0);
	return 2;
}

/* Will only ever be a single mode for this device */
#define MODE_CNT 1
#define OPOS 1

const uint32_t vdo_dp_mode[MODE_CNT] =  {
	VDO_MODE_GOOGLE(MODE_GOOGLE_FU)
};

static int svdm_response_modes(int port, uint32_t *payload)
{
	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE)
		return 0; /* nak */

	memcpy(payload + 1, vdo_dp_mode, sizeof(vdo_dp_mode));
	return MODE_CNT + 1;
}

static int svdm_enter_mode(int port, uint32_t *payload)
{
	/* SID & mode request is valid */
	if ((PD_VDO_VID(payload[0]) != USB_VID_GOOGLE) ||
	    (PD_VDO_OPOS(payload[0]) != OPOS))
		return 0; /* will generate NAK */

	gfu_mode = 1;
	debug_printf("GFU\n");
	return 1;
}

static int svdm_exit_mode(int port, uint32_t *payload)
{
	gfu_mode = 0;
	return 1; /* Must return ACK */
}

static struct amode_fx dp_fx = {
	.status = NULL,
	.config = NULL,
};

const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = &svdm_response_svids,
	.modes = &svdm_response_modes,
	.enter_mode = &svdm_enter_mode,
	.amode = &dp_fx,
	.exit_mode = &svdm_exit_mode,
};

static int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			 uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize;

	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE || !gfu_mode)
		return 0;

	debug_printf("%T] VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);
	*rpayload = payload;

	rsize = pd_custom_flash_vdm(port, cnt, payload);
	if (!rsize) {
		switch (cmd) {
		case VDO_CMD_PING_ENABLE:
			pd_ping_enable(0, payload[1]);
			rsize = 1;
			break;
		case VDO_CMD_CURRENT:
			/* return last measured current */
			payload[1] = ADC_TO_CURR_MA(vbus_amp);
			rsize = 2;
			break;
		default:
			/* Unknown : do not answer */
			return 0;
		}
	}

	debug_printf("%T] DONE\n");
	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}

int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	if (PD_VDO_SVDM(payload[0]))
		return pd_svdm(port, cnt, payload, rpayload);
	else
		return pd_custom_vdm(port, cnt, payload, rpayload);
}
