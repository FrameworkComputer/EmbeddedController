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
#include "sha1.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "version.h"

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
	adc_enable_watchdog(ADC_CH_V_SENSE, 0xFFF, target_volt);
}

/* ----------------------- USB Power delivery policy ---------------------- */

/* Power Delivery Objects */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_EXTERNAL),
		PDO_FIXED(5000,  RATED_CURRENT, 0),
		PDO_FIXED(12000, RATED_CURRENT, 0),
		PDO_FIXED(20000, RATED_CURRENT, 0),
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
	{VO_5V,  UVP_MV(5000),  OVP_MV(5000), OVP_REC_MV(5000)},
	{VO_12V, UVP_MV(12000), OVP_MV(12000), OVP_REC_MV(12000)},
	{VO_20V, UVP_MV(20000), OVP_MV(20000), OVP_REC_MV(20000)},
};

/* current and previous selected PDO entry */
static int volt_idx;
static int last_volt_idx;

/* output current measurement */
int vbus_amp;

int pd_request_voltage(uint32_t rdo)
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

	debug_printf("Switch to %d V %d mA (for %d/%d mA)\n",
		     ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		     ((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

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

	return EC_SUCCESS;
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
	int need_discharge = (volt_idx > 1) || discharge_is_enabled();

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

int pd_board_checks(void)
{
	static timestamp_t hib_to;
	static int hib_to_ready;
	int vbus_volt;
	int ovp_idx;

	/* Reload the watchdog */
	STM32_IWDG_KR = STM32_IWDG_KR_RELOAD;

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
}
DECLARE_IRQ(STM32_IRQ_ADC_COMP, pd_adc_interrupt, 1);

/* ----------------- Vendor Defined Messages ------------------ */
int pd_custom_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	static int flash_offset;
	void *hash;
	int cmd = PD_VDO_CMD(payload[0]);
	int rsize = 1;
	debug_printf("%T] VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);

	*rpayload = payload;
	switch (cmd) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &version_data.version, 24);
		rsize = 7;
		break;
	case VDO_CMD_REBOOT:
		/* ensure the power supply is in a safe state */
		pd_power_supply_reset(0);
		cpu_reset();
		break;
	case VDO_CMD_READ_INFO:
		hash = flash_hash_rw();
		/* copy the 20 first bytes of the hash into response */
		memcpy(payload + 1, hash, 5 * sizeof(uint32_t));
		/* copy other info into response */
		payload[6] = VDO_INFO(USB_PD_HARDWARE_DEVICE_ID,
					ver_get_numcommits(),
					!is_ro_mode());
		rsize = 7;
		break;
	case VDO_CMD_FLASH_ERASE:
		/* do not kill the code under our feet */
		if (!is_ro_mode())
			break;
		flash_offset = 0;
		flash_erase_rw();
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if (!is_ro_mode())
			break;
		flash_write_rw(flash_offset, 4*(cnt - 1),
			       (const char *)(payload+1));
		flash_offset += 4*(cnt - 1);
		break;
	case VDO_CMD_FLASH_HASH:
		/* this is not touching the code area */
		flash_write_rw(CONFIG_FW_RW_SIZE - 32, 4*cnt,
			       (const char *)(payload+1));
		break;
	case VDO_CMD_PING_ENABLE:
		pd_ping_enable(0, payload[1]);
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
	debug_printf("%T] DONE\n");
	/* respond (positively) to the request */
	payload[0] |= VDO_SRC_RESPONDER;

	return rsize;
}
