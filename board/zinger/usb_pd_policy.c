/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "debug_printf.h"
#include "ec_commands.h"
#include "hooks.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"

/* ------------------------- Power supply control ------------------------ */

/* GPIO level setting helpers through BSRR register */
#define GPIO_SET(n) (1 << (n))
#define GPIO_RESET(n) (1 << ((n) + 16))

/* Output voltage selection */
enum volt {
	VO_5V = GPIO_RESET(13) | GPIO_RESET(14),
	VO_12V = GPIO_SET(13) | GPIO_RESET(14),
	VO_13V = GPIO_RESET(13) | GPIO_SET(14),
	VO_20V = GPIO_SET(13) | GPIO_SET(14),
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
	return STM32_GPIO_ODR(GPIO_F) & 1;
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
#define ADC_SCALE BIT(12)
/* ADC power supply : VDDA = 3.3V */
#define VDDA_MV 3300
/* Current sense resistor : 5 milliOhm */
#define R_SENSE 5
/* VBUS voltage is measured through 10k / 100k voltage divider = /11 */
#define VOLT_DIV ((10 + 100) / 10)
/* The current sensing op-amp has a x100 gain */
#define CURR_GAIN 100
/* convert VBUS voltage in raw ADC value */
#define VBUS_MV(mv) ((mv)*ADC_SCALE / VOLT_DIV / VDDA_MV)
/* convert VBUS current in raw ADC value */
#define VBUS_MA(ma) ((ma)*ADC_SCALE * R_SENSE / 1000 * CURR_GAIN / VDDA_MV)
/* convert raw ADC value to mA */
#define ADC_TO_CURR_MA(vbus) \
	((vbus)*1000 / (ADC_SCALE * R_SENSE) * VDDA_MV / CURR_GAIN)
/* convert raw ADC value to mV */
#define ADC_TO_VOLT_MV(vbus) ((vbus)*VOLT_DIV * VDDA_MV / ADC_SCALE)

/* Max current : 20% over rated current */
#define MAX_CURRENT VBUS_MA(RATED_CURRENT * 6 / 5)
/* Fast short circuit protection : 50% over rated current */
#define MAX_CURRENT_FAST VBUS_MA(RATED_CURRENT * 3 / 2)
/* reset over-current after 1 second */
#define OCP_TIMEOUT SECOND

/* Threshold below which we stop fast OCP to save power */
#define SINK_IDLE_CURRENT VBUS_MA(500 /* mA */)

/* Under-voltage limit is 0.8x Vnom */
#define UVP_MV(mv) VBUS_MV((mv)*8 / 10)
/* Over-voltage limit is 1.2x Vnom */
#define OVP_MV(mv) VBUS_MV((mv)*12 / 10)
/* Over-voltage recovery threshold is 1.1x Vnom */
#define OVP_REC_MV(mv) VBUS_MV((mv)*11 / 10)

/* Maximum discharging delay */
#define DISCHARGE_TIMEOUT (275 * MSEC)
/* Voltage overshoot below the OVP threshold for discharging to avoid OVP */
#define DISCHARGE_OVERSHOOT_MV VBUS_MV(200)

/* Time to wait after last RX edge interrupt before allowing deep sleep */
#define PD_RX_SLEEP_TIMEOUT (100 * MSEC)

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
	return STM32_GPIO_ODR(GPIO_F) & 2;
}

static void discharge_voltage(int target_volt)
{
	discharge_enable();
	discharge_deadline.val = get_time().val + DISCHARGE_TIMEOUT;
	/* Monitor VBUS voltage */
	target_volt -= DISCHARGE_OVERSHOOT_MV;
	disable_sleep(SLEEP_MASK_USB_PWR);
	adc_enable_watchdog(ADC_CH_V_SENSE, 0xFFF, target_volt);
}

/* ----------------------- USB Power delivery policy ---------------------- */

/* PDO voltages (should match the table above) */
static const struct {
	enum volt select; /* GPIO configuration to select the voltage */
	int uvp; /* under-voltage limit in mV */
	int ovp; /* over-voltage limit in mV */
	int ovp_rec; /* over-voltage recovery threshold in mV */
} voltages[ARRAY_SIZE(pd_src_pdo)] = {
	[PDO_IDX_5V] = { VO_5V, UVP_MV(5000), OVP_MV(5000), OVP_REC_MV(5000) },
	[PDO_IDX_12V] = { VO_12V, UVP_MV(12000), OVP_MV(12000),
			  OVP_REC_MV(12000) },
	[PDO_IDX_20V] = { VO_20V, UVP_MV(20000), OVP_MV(20000),
			  OVP_REC_MV(20000) },
};

/* current and previous selected PDO entry */
static int volt_idx;
static int last_volt_idx;
/* target voltage at the end of discharge */
static int discharge_volt_idx;

/* output current measurement */
int vbus_amp;

__override int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	/* fault condition or output disabled: reject transitions */
	if (fault != FAULT_OK || !output_is_enabled())
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

void pd_transition_voltage(int idx)
{
	last_volt_idx = volt_idx;
	volt_idx = idx - 1;
	if (volt_idx < last_volt_idx) { /* down voltage transition */
		/* Stop OCP monitoring */
		adc_disable_watchdog();

		discharge_volt_idx = volt_idx;
		/* from 20V : do an intermediate step at 12V */
		if (volt_idx == PDO_IDX_5V && last_volt_idx == PDO_IDX_20V)
			volt_idx = PDO_IDX_12V;
		discharge_voltage(voltages[volt_idx].ovp);
	} else if (volt_idx > last_volt_idx) { /* up voltage transition */
		if (discharge_is_enabled()) {
			/* Make sure discharging is disabled */
			discharge_disable();
			/* Enable over-current monitoring */
			adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST,
					    0);
		}
	}
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
	last_volt_idx = volt_idx;
	/* from 20V : do an intermediate step at 12V */
	volt_idx = volt_idx == PDO_IDX_20V ? PDO_IDX_12V : PDO_IDX_5V;
	set_output_voltage(voltages[volt_idx].select);
	/* TODO transition delay */

	/* Stop OCP monitoring to save power */
	adc_disable_watchdog();

	/* discharge voltage to 5V ? */
	if (need_discharge) {
		/* final target : 5V  */
		discharge_volt_idx = PDO_IDX_5V;
		discharge_voltage(voltages[volt_idx].ovp);
	}
}

int pd_check_data_swap(int port, enum pd_data_role data_role)
{
	/* Allow data swap if we are a DFP, otherwise don't allow */
	return (data_role == PD_ROLE_DFP) ? 1 : 0;
}

void pd_execute_data_swap(int port, enum pd_data_role data_role)
{
	/* Do nothing */
}

void pd_check_pr_role(int port, enum pd_power_role pr_role, int flags)
{
}

void pd_check_dr_role(int port, enum pd_data_role dr_role, int flags)
{
	/* If DFP, try to switch to UFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
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
			debug_printf("hib\n");
			__enter_hibernate(0, 0);
		}
	} else {
		hib_to.val = get_time().val + 60 * SECOND;
		hib_to_ready = 1;
	}
#endif

	/* if it's been a while since last RX edge, then allow deep sleep */
	if (get_time_since_last_edge(0) > PD_RX_SLEEP_TIMEOUT)
		enable_sleep(SLEEP_MASK_USB_PD);

	vbus_volt = adc_read_channel(ADC_CH_V_SENSE);
	vbus_amp = adc_read_channel(ADC_CH_A_SENSE);

	if (fault == FAULT_FAST_OCP) {
		debug_printf("Fast OCP\n");
		pd_log_event(PD_EVENT_PS_FAULT, 0, PS_FAULT_FAST_OCP, NULL);
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
			debug_printf("OCP %d mA\n",
				     vbus_amp * VDDA_MV / CURR_GAIN * 1000 /
					     R_SENSE / ADC_SCALE);
			pd_log_event(PD_EVENT_PS_FAULT, 0, PS_FAULT_OCP, NULL);
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
		enable_sleep(SLEEP_MASK_USB_PWR);
	else if (vbus_amp > SINK_IDLE_CURRENT)
		disable_sleep(SLEEP_MASK_USB_PWR);

	/*
	 * Set the voltage index to use for checking OVP. During a down step
	 * transition, use the previous voltage index to check for OVP.
	 */
	ovp_idx = discharge_is_enabled() ? last_volt_idx : volt_idx;

	if ((output_is_enabled() && (vbus_volt > voltages[ovp_idx].ovp)) ||
	    (fault && (vbus_volt > voltages[ovp_idx].ovp_rec))) {
		if (!fault) {
			debug_printf("OVP %d mV\n", ADC_TO_VOLT_MV(vbus_volt));
			pd_log_event(PD_EVENT_PS_FAULT, 0, PS_FAULT_OVP, NULL);
		}
		fault = FAULT_OVP;
		/* no timeout */
		fault_deadline.val = get_time().val;
		return EC_ERROR_INVAL;
	}

	/* the discharge did not work properly */
	if (discharge_is_enabled() &&
	    (get_time().val > discharge_deadline.val)) {
		/* ensure we always finish a 2-step discharge */
		volt_idx = discharge_volt_idx;
		set_output_voltage(voltages[volt_idx].select);
		/* stop it */
		discharge_disable();
		/* enable over-current monitoring */
		adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST, 0);
		debug_printf("Disch FAIL %d mV\n", ADC_TO_VOLT_MV(vbus_volt));
		pd_log_event(PD_EVENT_PS_FAULT, 0, PS_FAULT_DISCH, NULL);
		fault = FAULT_DISCHARGE;
		/* reset it after 1 second */
		fault_deadline.val = get_time().val + OCP_TIMEOUT;
		return EC_ERROR_INVAL;
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

static void pd_adc_interrupt(void)
{
	/* Clear flags */
	STM32_ADC_ISR = 0x8e;

	if (discharge_is_enabled()) {
		if (discharge_volt_idx != volt_idx) {
			/* first step of the discharge completed: now 12V->5V */
			volt_idx = PDO_IDX_5V;
			set_output_voltage(VO_5V);
			discharge_voltage(voltages[PDO_IDX_5V].ovp);
		} else { /* discharge complete */
			discharge_disable();
			/* enable over-current monitoring */
			adc_enable_watchdog(ADC_CH_A_SENSE, MAX_CURRENT_FAST,
					    0);
		}
	} else { /* Over-current detection */
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

const uint32_t vdo_dp_mode[MODE_CNT] = { VDO_MODE_GOOGLE(MODE_GOOGLE_FU) };

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

__override int pd_custom_vdm(int port, int cnt, uint32_t *payload,
			     uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize;
	char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];

	if (PD_VDO_VID(payload[0]) != USB_VID_GOOGLE || !gfu_mode)
		return 0;

	snprintf_timestamp_now(ts_str, sizeof(ts_str));
	debug_printf("%s] VDM/%d [%d] %08x\n", ts_str, cnt, cmd, payload[0]);
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
		case VDO_CMD_GET_LOG:
			rsize = pd_vdm_get_log_entry(payload);
			break;
		default:
			/* Unknown : do not answer */
			return 0;
		}
	}

	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}
