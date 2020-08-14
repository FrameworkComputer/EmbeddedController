/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* for asprintf */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <dirent.h>
#include <limits.h>

#include "config.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "charge_manager.h"
#include "system.h"

/*
 * Vendor Info File Generator
 * Revision 1.40, Version 1.0
 */

#define GENVIF_TITLE  "EC GENVIF, Version 1.40"
#define VIF_SPEC      "Revision 1.40, Version 1.0"
#define VENDOR_NAME   "Google"

enum spec_rev {
	PD_REV_2_0 = 1,
	PD_REV_3_0
};

enum field {
	INTRO = 0,
	PRODUCT,
	GENERAL,
	USB,
	DEVICE,
	SOURCE,
	SINK,
	DUAL_ROLE,
	SOP,
	BC12,
};

enum dtype {
	SRC = 0,
	SNK,
	DRP
};

enum vif_product_type {
	PORT = 0,
	CABLE,
	RE_TIMER
};

enum conn_type {
	TYPE_A = 0,
	TYPE_B,
	TYPE_C,
	MICRO_AB
};

enum port_type {
	PT_CONSUMER = 0,
	PT_CONSUMER_PROVIDER,
	PT_PROVIDER_CONSUMER,
	PT_PROVIDER,
	PT_DRP,
	PT_EMARKER
};

enum bc_1_2_support {
	BC_NONE = 0,
	BC_PORTABLE_DEVICE,
	BC_CHARGING_PORT,
	BC_BOTH
};

enum power_source {
	PS_EXT_POWERED = 0,
	PS_UFP_POWERED,
	PS_BOTH
};

enum usb_speed {
	USB_2,
	USB_GEN11,
	USB_GEN21,
	USB_GEN12,
	USB_GEN22
};

const uint32_t *src_pdo;
uint32_t src_pdo_cnt;

char *yes_no(int val)
{
	return val ? "YES" : "NO";
}

enum ec_image system_get_image_copy(void)
{
	return EC_IMAGE_RW;
}

static void write_title(FILE *vif)
{
	fprintf(vif, ";\r\n");
	fprintf(vif, "; %s \r\n", GENVIF_TITLE);
	fprintf(vif, ";\r\n");
}

static void write_field(FILE *vif, enum field t)
{
	if (!vif)
		return;

	fprintf(vif, "\r\n%s\r\n",
	";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;");
	switch (t) {
	case INTRO:
		fprintf(vif, ";   Intro Fields\r\n");
		break;
	case PRODUCT:
		fprintf(vif, ";   VIF Product Fields\r\n");
		break;
	case GENERAL:
		fprintf(vif, ";   General PD Fields\r\n");
		break;
	case USB:
		fprintf(vif, ";   USB Type-C Fields\r\n");
		break;
	case DEVICE:
		fprintf(vif, ";   USB Device Fields\r\n");
		break;
	case SOURCE:
		fprintf(vif, ";   PD Source Fields\r\n");
		break;
	case SINK:
		fprintf(vif, ";   PD Sink Fields\r\n");
		break;
	case DUAL_ROLE:
		fprintf(vif, ";   PD Dual Role Fields\r\n");
		break;
	case SOP:
		fprintf(vif, ";   SOP Discovery Fields\r\n");
		break;
	case BC12:
		fprintf(vif, ":   Battery Charging 1.2 Fields\r\n");
		break;

	}
	fprintf(vif, "%s\r\n",
	";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;");
}

static void init_src_pdos(void)
{
#ifdef CONFIG_USB_PD_DYNAMIC_SRC_CAP
	src_pdo_cnt = charge_manager_get_source_pdo(&src_pdo, 0);
#else
	src_pdo_cnt = pd_src_pdo_cnt;
	src_pdo = pd_src_pdo;
#endif
}

static int is_src(void)
{
	return src_pdo_cnt;
}

static int is_snk(void)
{
#ifdef CONFIG_USB_PD_DUAL_ROLE
	return pd_snk_pdo_cnt;
#else
	return 0;
#endif
}

static int is_drp(void)
{
	if (is_src())
		return !!(src_pdo[0] & PDO_FIXED_DUAL_ROLE);
	else
		return 0;
}

/* Application exits on failure */
__attribute__((__format__(__printf__, 2, 3)))
static void append(char **buf, const char *fmt, ...)
{
	va_list ap1, ap2;
	int n;
	static int offset;

	va_start(ap1, fmt);
	va_copy(ap2, ap1);

	n = vsnprintf(NULL, 0, fmt, ap1) + 1;

	if (*buf == NULL)
		offset = 0;

	*buf = (char *)realloc(*buf, offset + n);

	if (*buf) {
		vsnprintf(*buf + offset, n, fmt, ap2);
	} else {
		fprintf(stderr, "ERROR: Out of memory.\n");
		exit(EXIT_FAILURE);
	}

	/* Overwrite NULL terminator the next time through. */
	offset += (n-1);

	va_end(ap1);
	va_end(ap2);
}

static int32_t write_pdo_to_buf(char **buf, uint32_t pdo,
				enum dtype type, uint32_t pnum)
{
	int32_t power;

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t voltage = (pdo >> 10) & 0x3ff;

		power = ((current * 10) * (voltage * 50)) / 1000;

		append(buf, "\t%s_PDO_Supply_Type%d: 0\r\n",
			(type == SRC) ? "Src" : "Snk", pnum);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Peak_Current%d: 0\r\n", pnum);

		append(buf, "\t%s_PDO_Voltage%d: %d\r\n",
				(type == SRC) ? "Src" : "Snk", pnum, voltage);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Max_Current%d: %d\r\n", pnum,
				current);
		else
			append(buf, "\tSnk_PDO_Op_Current%d: %d\r\n", pnum,
				current);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;

		power = pdo & 0x3ff;

		append(buf, "\t%s_PDO_Supply_Type%d: 1\r\n",
			(type == SRC) ? "Src" : "Snk", pnum);

		append(buf, "\t%s_PDO_Min_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, min_voltage);

		append(buf, "\t%s_PDO_Max_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, max_voltage);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Max_Power%d: %d\r\n", pnum,
				power);
		else
			append(buf, "\tSnk_PDO_Op_Power%d: %d\r\n", pnum,
				power);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t current = pdo & 0x3ff;

		power = ((current * 10) * (max_voltage * 50)) / 1000;

		append(buf, "\t%s_PDO_Supply_Type%d: 2\r\n",
			(type == SRC) ? "Src" : "Snk", pnum);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Peak_Current%d: 0\r\n", pnum);

		append(buf, "\t%s_PDO_Min_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, min_voltage);

		append(buf, "\t%s_PDO_Max_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, max_voltage);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Max_Current%d: %d\r\n", pnum,
				current);
		else
			append(buf, "\tSnk_PDO_Op_Current%d: %d\r\n", pnum,
				current);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		uint32_t pps = (pdo >> 28) & 3;
		uint32_t pps_max_voltage = (pdo >> 17) & 0xff;
		uint32_t pps_min_voltage = (pdo >> 8) & 0xff;
		uint32_t pps_current = pdo & 0x7f;

		if (pps) {
			fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
			return -1;
		}

		append(buf, "\t%s_PDO_Supply_Type%d: 3\r\n",
			(type == SRC) ? "Src" : "Snk", pnum);

		if (type == SRC)
			append(buf, "\tSrc_PDO_Max_Current%d: %d\r\n", pnum,
							pps_current);
		else
			append(buf, "\tSnk_PDO_Op_Current%d: %d\r\n", pnum,
							pps_current);

		append(buf, "\t%s_PDO_Min_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, pps_min_voltage);

		append(buf, "\t%s_PDO_Max_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, pps_max_voltage);
	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	append(buf, "\r\n");

	return power;
}

/*
 * Intro Fields
 */

static void write_vif_specification(FILE *vif)
{
	fprintf(vif, "$VIF_Specification: \"%s\"\r\n", VIF_SPEC);
}

static void write_vif_producer(FILE *vif, const char *vp)
{
	fprintf(vif, "$VIF_Producer: \"%s\"\r\n", vp);
}

static void write_vendor_name(FILE *vif)
{
	fprintf(vif, "$Vendor_Name: \"%s\"\r\n", VENDOR_NAME);
}

static void write_model_part_number(FILE *vif)
{
#ifdef CONFIG_USB_PD_MODEL_PART_NUMBER
	fprintf(vif, "$Model_Part_Number: \"%s\"\r\n",
					CONFIG_USB_PD_MODEL_PART_NUMBER);
#endif
}

static void write_product_revision(FILE *vif)
{
#ifdef CONFIG_USB_PD_PRODUCT_REVISION
	fprintf(vif, "$Model_Part_Number: \"%s\"\r\n",
					CONFIG_USB_PD_PRODUCT_REVISION);
#endif
}

static void write_tid(FILE *vif)
{
#ifdef CONFIG_USB_PD_TID
	fprintf(vif, "$TID: \"%s\"\r\n", CONFIG_USB_PD_TID);
#endif
}

/*
 * VIF Product Fields
 */

static void write_vif_product_type(FILE *vif)
{
	fprintf(vif, "VIF_Product_Type: %d\r\n", PORT);
}

static void write_port_label(FILE *vif)
{
#ifdef CONFIG_USB_PD_PORT_LABEL
	fprintf(vif, "$Port_Label: %s\r\n", CONFIG_USB_PD_PORT_LABEL);
#endif
}

static void write_connector_type(FILE *vif)
{
	fprintf(vif, "Connector_Type: %d\r\n", TYPE_C);
}

static void write_usb_pd_support(FILE *vif)
{
	char *yn = "NO";

#if defined(CONFIG_USB_PRL_SM) || defined(CONFIG_USB_POWER_DELIVERY)
	yn = "YES";
#endif

	fprintf(vif, "USB_PD_Support: %s\n", yn);
}

static void write_pd_port_type(FILE *vif, enum dtype type)

{
	enum port_type pt;

	switch (type) {
	case SNK:
		pt = PT_CONSUMER;
		break;
	case SRC:
		pt = PT_PROVIDER;
		break;
	case DRP:
		pt = PT_DRP;
		break;
	}

	fprintf(vif, "PD_Port_Type: %d\r\n", pt);
}

static void write_type_c_state_machine(FILE *vif, enum dtype type)
{
	fprintf(vif, "Type_C_State_Machine: %d\r\n", type);
}

static void write_captive_cable(FILE *vif)
{
	fprintf(vif, "Captive_Cable: NO\r\n");
}

static void write_port_battery_powered(FILE *vif)
{
#ifdef CONFIG_BATTERY
	fprintf(vif, "Port_Battery_Powered: YES\r\n");
#else
	fprintf(vif, "Port_Battery_Powered: NO\r\n");
#endif
}

static void write_bc_1_2_support(FILE *vif, enum dtype type)
{
	enum bc_1_2_support bc = BC_NONE;

	fprintf(vif, "BC_1_2_Support: %d\r\n", bc);
}

/*
 * General PD Fields
 */

static void write_pd_spec_rev(FILE *vif)
{
	enum spec_rev rev;

#if defined(CONFIG_USB_PD_REV30) || defined(CONFIG_USB_PRL_SM)
	rev = PD_REV_3_0;
#else
	rev = PD_REV_2_0;
#endif
	fprintf(vif, "PD_Specification_Revision: %d\r\n", rev);
}

static void write_usb_comms_capable(FILE *vif)
{
	char *yn = "YES";

#if defined(CONFIG_USB_VPD) || defined(CONFIG_USB_CTVPD)
	yn = "NO";
#endif
	fprintf(vif, "USB_Comms_Capable: %s\r\n", yn);
}

static void write_dr_swap_to_dfp_supported(FILE *vif)
{
	char *yn = "NO";

	if (is_src() && (src_pdo[0] & PDO_FIXED_DATA_SWAP))
		yn = yes_no(pd_check_data_swap(0, PD_ROLE_DFP));

	fprintf(vif, "DR_Swap_To_DFP_Supported: %s\r\n", yn);
}

static void write_dr_swap_to_ufp_supported(FILE *vif)
{
	char *yn = "NO";

	if (is_src() && (src_pdo[0] & PDO_FIXED_DATA_SWAP))
		yn = yes_no(pd_check_data_swap(0, PD_ROLE_UFP));


	fprintf(vif, "DR_Swap_To_UFP_Supported: %s\r\n", yn);
}

static void write_unconstrained_power(FILE *vif)
{
	int yn = 0;

	if (is_src())
		yn = !!(src_pdo[0] & PDO_FIXED_UNCONSTRAINED);

	fprintf(vif, "Unconstrained_Power: %s\r\n", yes_no(yn));
}

static void write_vconn_swap_to_on_supported(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USBC_VCONN_SWAP
	yn = "YES";
#endif

	fprintf(vif, "VCONN_Swap_To_On_Supported: %s\r\n", yn);
}

static void write_vconn_swap_to_off_supported(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USBC_VCONN_SWAP
	yn = "YES";
#endif

	fprintf(vif, "VCONN_Swap_To_Off_Supported: %s\r\n", yn);
}

static void write_responds_to_discov_sop_ufp(FILE *vif)
{
	fprintf(vif, "Responds_To_Discov_SOP_UFP: NO\r\n");
}

static void write_responds_to_discov_sop_dfp(FILE *vif)
{
	fprintf(vif, "Responds_To_Discov_SOP_DFP: NO\r\n");
}

static void write_attempts_discov_sop(FILE *vif, enum dtype type)
{
	char *yn = "YES";

#ifdef CONFIG_USB_PD_SIMPLE_DFP
	if (type == SRC)
		yn = "NO";
	else
		yn = "YES";
#endif

	fprintf(vif, "Attempts_Discov_SOP: %s\r\n", yn);
}

static void write_chunking_implemented_sop(FILE *vif)
{
	char *yn = "NO";

#if defined(CONFIG_USB_PD_REV30) && defined(CONFIG_USB_PRL_SM)
	yn = "YES";
#endif

	fprintf(vif, "Chunking_Implemented_SOP: %s\r\n", yn);
}

static void write_unchunked_extended_messages_supported(FILE *vif)
{
	fprintf(vif, "Unchunked_Extended_Messages_Supported: NO\r\n");
}

static void write_manufacturer_info_supported_port(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USB_PD_MANUFACTURER_INFO
	yn = "YES";
#endif

	fprintf(vif, "Manufacturer_Info_Supported_Port: %s\r\n", yn);
}

static void write_manufacturer_info_pid_port(FILE *vif)
{
#ifdef USB_PID_GOOGLE
	fprintf(vif, "Manufacturer_Info_PID_Port: 0x%04x\r\n", USB_PID_GOOGLE);
#endif
}

static void write_security_msgs_supported_sop(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USB_PD_SECURITY_MSGS
	yn = "YES";
#endif

	fprintf(vif, "Security_Msgs_Supported_SOP: %s\r\n", yn);
}

static void write_num_fixed_batteries(FILE *vif)
{
	int num = 1;

#ifdef CONFIG_NUM_FIXED_BATTERIES
	num = CONFIG_NUM_FIXED_BATTERIES;
#else
#if defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
	num = 0;
#endif
#endif

	fprintf(vif, "Num_Fixed_Batteries: %d\r\n", num);
}

static void write_num_swappable_battery_slots(FILE *vif)
{
	fprintf(vif, "Num_Swappable_Battery_Slots: 0\r\n");
}

static void write_sop_capable(FILE *vif)
{
	char *yn = "YES";

#if defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
	yn = "NO";
#endif

	fprintf(vif, "SOP_Capable: %s\r\n", yn);
}

static void write_sop_p_capable(FILE *vif)
{
	char *yn = "NO";

#if defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
	yn = "YES";
#endif

	fprintf(vif, "SOP_P_Capable: %s\r\n", yn);
}

static void write_sop_pp_capable(FILE *vif)
{
	fprintf(vif, "SOP_PP_Capable: NO\r\n");
}

static void write_sop_p_debug_capable(FILE *vif)
{
	fprintf(vif, "SOP_P_Debug_Capable: NO\r\n");
}

static void write_sop_pp_debug_capable(FILE *vif)
{
	fprintf(vif, "SOP_PP_Debug_Capable: NO\r\n");
}

/*
 * USB Type-C Fields
 */

static void write_type_c_implements_try_src(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USB_PD_TRY_SRC
	yn = "YES";
#endif

	fprintf(vif, "Type_C_Implements_Try_SRC: %s\r\n", yn);
}

static void write_type_c_implements_try_snk(FILE *vif)
{
	fprintf(vif, "Type_C_Implements_Try_SNK: NO\r\n");
}

static void write_rp_value(FILE *vif)
{
	/*
	 * 0 - Default
	 * 1 - 1.5A
	 * 2 - 3A
	 */
	int rp = CONFIG_USB_PD_PULLUP;

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	rp = CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
#endif

	fprintf(vif, "Rp_Value: %d\r\n", rp);
}

static void write_type_c_supports_vconn_powered_accessory(FILE *vif)
{
	fprintf(vif, "Type_C_Supports_VCONN_Powered_Accessory: NO\r\n");
}

static void write_type_c_is_debug_target_src(FILE *vif)
{
	fprintf(vif, "Type_C_Is_Debug_Target_SRC: YES\r\n");
}

static void write_type_c_is_debug_target_snk(FILE *vif)
{
	fprintf(vif, "Type_C_Is_Debug_Target_SNK: YES\r\n");
}

static void write_type_c_can_act_as_host(FILE *vif)
{
	char *yn = "YES";

	#if defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
		yn = "NO";
	#endif

	fprintf(vif, "Type_C_Can_Act_As_Host: %s\r\n", yn);
}

static void write_type_c_is_alt_mode_controller(FILE *vif)
{
	fprintf(vif, "Type_C_Is_Alt_Mode_Controller: NO\r\n");
}

static void write_type_c_can_act_as_device(FILE *vif)
{
	char *yn = "NO";

#if defined(USB_DEV_CLASS) && defined(USB_CLASS_BILLBOARD)
	if (USB_DEV_CLASS == USB_CLASS_BILLBOARD)
		yn = "YES";
#endif

	fprintf(vif, "Type_C_Can_Act_As_Device: %s\r\n", yn);
}

static void write_type_c_is_alt_mode_adapter(FILE *vif)
{
	char *yn = "NO";

	if (IS_ENABLED(CONFIG_USB_ALT_MODE_ADAPTER))
		yn = "YES";

	fprintf(vif, "Type_C_Is_Alt_Mode_Adapter: %s\r\n", yn);
}

static void write_type_c_power_source(FILE *vif)
{
	/*
	 * 0 - Externally Powered
	 * 1 - USB-powered
	 * 2 - Both
	 */
	int ps = 1;

	if (CONFIG_DEDICATED_CHARGE_PORT_COUNT == 1)
		ps = 0;

	fprintf(vif, "Type_C_Power_Source: %d\r\n", ps);
}

static void write_type_c_port_on_hub(FILE *vif)
{
	fprintf(vif, "Type_C_Port_On_Hub: NO\r\n");
}

static void write_type_c_supports_audio_accessory(FILE *vif)
{
	fprintf(vif, "Type_C_Supports_Audio_Accessory: NO\r\n");
}

static void write_type_c_sources_vconn(FILE *vif)
{
	char *yn = "NO";

#ifdef CONFIG_USBC_VCONN
	yn = "YES";
#endif

	fprintf(vif, "Type_C_Source_Vconn: %s\r\n", yn);
}

/*
 * USB Device Fields
 */

static void write_device_speed(FILE *vif)
{
	/*
	 * TODO(shurst): USB_2 might not be true for all boards and will need
	 * changing for future board that implement USB_GENx.
	 */
	enum usb_speed speed = USB_2;

	fprintf(vif, "Device_Speed: %d\r\n", speed);
}

/*
 * PD Source Fields
 */
static int write_pd_source_fields(FILE *vif, enum dtype type)
{
	uint32_t max_power = 0;
	char *pdo_buf = NULL;
	int i;
	int32_t pwr;

	if (type == DRP || type == SRC) {
		/* Source PDOs */
		for (i = 0; i < src_pdo_cnt; i++) {
			pwr = write_pdo_to_buf(&pdo_buf, src_pdo[i], SRC, i+1);
			if (pwr < 0) {
				fprintf(stderr, "ERROR: Out of memory.\n");
				fclose(vif);
				return 1;
			}

			if (pwr > max_power)
				max_power = pwr;
		}

		/* Source Fields */
		fprintf(vif, "PD_Power_as_Source: %d\r\n", max_power);
		fprintf(vif, "USB_Suspend_May_Be_Cleared: YES\r\n");
		fprintf(vif, "Sends_Pings: NO\r\n");
		fprintf(vif, "Num_Src_PDOs: %d\r\n", src_pdo_cnt);

		if (IS_ENABLED(CONFIG_USBC_PPC)) {
			/*
			 * 0 – Over-Current Response
			 * 1 – Under-Voltage Response
			 * 2 – Both
			 */
			int resp = 0;

			fprintf(vif, "PD_OC_Protection: YES\r\n");
			fprintf(vif, "PD_OCP_Method: %d\r\n", resp);
		} else {
			fprintf(vif, "PD_OC_Protection: NO\r\n");
		}

		fprintf(vif, "\r\n%s\r\n", pdo_buf);
		free(pdo_buf);
	}

	return 0;
}

/*
 * PD Sink Fields
 */
static int write_pd_sink_fields(FILE *vif, enum dtype type)
{
#ifdef CONFIG_USB_PD_DUAL_ROLE
	uint32_t max_power = 0;
	char *pdo_buf = NULL;
	int32_t pwr;
	int i;
	char *giveback = "NO";

#ifdef CONFIG_USB_PD_GIVE_BACK
	giveback = "YES";
#endif

	if (type == DRP || type == SNK) {
		/* Sink PDOs */
		for (i = 0; i < pd_snk_pdo_cnt; i++) {
			pwr = write_pdo_to_buf(&pdo_buf,
						pd_snk_pdo[i], SNK, i+1);

			if (pwr < 0) {
				fprintf(stderr, "ERROR: Out of memory.\n");
				fclose(vif);
				return 1;
			}

			if (pwr > max_power)
				max_power = pwr;
		}

		/* Sink Fields */
		fprintf(vif, "PD_Power_as_Sink: %d\r\n", max_power);
		fprintf(vif, "No_USB_Suspend_May_Be_Set: YES\r\n");
		fprintf(vif, "GiveBack_May_Be_Set: %s\r\n", giveback);
		fprintf(vif, "Higher_Capability_Set: NO\r\n");
		fprintf(vif, "Num_Snk_PDOs: %d\r\n", pd_snk_pdo_cnt);
		fprintf(vif, "\r\n%s\r\n", pdo_buf);
		free(pdo_buf);
	}
#endif
	return 0;
}

/*
 * PD Dual Role Fields
 */
static void write_pd_drp_fields(FILE *vif, enum dtype type)
{
#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (type == DRP) {
		/* Dual Role Fields */
		fprintf(vif, "Accepts_PR_Swap_As_Src: YES\r\n");
		fprintf(vif, "Accepts_PR_Swap_As_Snk: YES\r\n");
		fprintf(vif, "Requests_PR_Swap_As_Src: YES\r\n");
		fprintf(vif, "FR_Swap_Supported_As_Initial_Sink: NO\r\n");
	}
#endif
}

/**
 * Carriage and line feed, '\r\n', is needed because the file is processed
 * on a Windows machine.
 */
static int gen_vif(const char *name, const char *board,
					const char *vif_producer)
{
	FILE *vif;
	enum dtype type;

	if (is_drp())
		type = DRP;
	else if (is_src() && is_snk())
		/* No DRP with SRC and SNK PDOs detected. So ignore. */
		/* ie. Twinkie or Plankton */
		return 0;
	else if (is_src())
		type = SRC;
	else if (is_snk())
		type = SNK;
	else
		return 1;

	/*
	 * Create VIF
	 */
	vif = fopen(name, "w+");
	if (vif == NULL)
		return 1;

	/*
	 * Write VIF Title
	 */
	write_title(vif);

	/*
	 * Write Intro Fields comment
	 */
	write_field(vif, INTRO);

	write_vif_specification(vif);
	write_vif_producer(vif, vif_producer);
	write_vendor_name(vif);
	write_model_part_number(vif);
	write_product_revision(vif);
	write_tid(vif);

	/*
	 * Write VIF Product Fields
	 */
	write_field(vif, PRODUCT);

	write_vif_product_type(vif);
	write_port_label(vif);
	write_connector_type(vif);
	write_usb_pd_support(vif);
	write_pd_port_type(vif, type);
	write_type_c_state_machine(vif, type);
	write_captive_cable(vif);
	write_port_battery_powered(vif);
	write_bc_1_2_support(vif, type);


	/*
	 * Write General PD Fields
	 */
	write_field(vif, GENERAL);

	write_pd_spec_rev(vif);
	write_usb_comms_capable(vif);
	write_dr_swap_to_dfp_supported(vif);
	write_dr_swap_to_ufp_supported(vif);
	write_unconstrained_power(vif);
	write_vconn_swap_to_on_supported(vif);
	write_vconn_swap_to_off_supported(vif);
	write_responds_to_discov_sop_ufp(vif);
	write_responds_to_discov_sop_dfp(vif);
	write_attempts_discov_sop(vif, type);
	write_chunking_implemented_sop(vif);
	write_unchunked_extended_messages_supported(vif);
	write_manufacturer_info_supported_port(vif);
	write_manufacturer_info_pid_port(vif);
	write_security_msgs_supported_sop(vif);
	write_num_fixed_batteries(vif);
	write_num_swappable_battery_slots(vif);
	write_sop_capable(vif);
	write_sop_p_capable(vif);
	write_sop_pp_capable(vif);
	write_sop_p_debug_capable(vif);
	write_sop_pp_debug_capable(vif);

	/*
	 * Write USB Type-C Fields
	 */
	write_field(vif, USB);

	write_type_c_implements_try_src(vif);
	write_type_c_implements_try_snk(vif);
	write_rp_value(vif);
	write_type_c_supports_vconn_powered_accessory(vif);
	write_type_c_is_debug_target_src(vif);
	write_type_c_is_debug_target_snk(vif);
	write_type_c_can_act_as_host(vif);
	write_type_c_is_alt_mode_controller(vif);
	write_type_c_can_act_as_device(vif);
	write_type_c_is_alt_mode_adapter(vif);
	write_type_c_power_source(vif);
	write_type_c_port_on_hub(vif);
	write_type_c_supports_audio_accessory(vif);
	write_type_c_sources_vconn(vif);


	/*
	 * Write USB Device Fields
	 */
	write_field(vif, DEVICE);

	write_device_speed(vif);

	/*
	 * Write PD Source Fields
	 */
	write_field(vif, SOURCE);

	write_pd_source_fields(vif, type);

	/*
	 * Write PD Sink Fields
	 */
	write_field(vif, SINK);

	write_pd_sink_fields(vif, type);


	/*
	 * Write DRP Fields
	 */
	write_field(vif, DUAL_ROLE);

	write_pd_drp_fields(vif, type);

	/*
	 * Battery Charging 1.2 Fields
	 */
	write_field(vif, BC12);

	fclose(vif);
	return 0;
}

int main(int argc, char **argv)
{
	int nopt;
	int ret;
	const char *out = NULL;
	const char *board = NULL;
	const char *vif_producer;
	DIR *vifdir;
	char *name;
	int name_size;
	const char * const short_opt = "hb:o:";
	const struct option long_opts[] = {
		{ "help", 0, NULL, 'h' },
		{ "board", 1, NULL, 'b' },
		{ "out", 1, NULL, 'o' },
		{ NULL }
	};

	vif_producer = argv[0];

	do {
		nopt = getopt_long(argc, argv, short_opt, long_opts, NULL);
		switch (nopt) {
		case 'h': /* -h or --help */
			printf("USAGE: %s -b <board name> -o <out directory>\n",
					vif_producer);
			return 1;

		case 'b': /* -b or --board */
			board = optarg;
			break;

		case 'o': /* -o or --out */
			out = optarg;
			break;
		case -1:
			break;

		default:
			abort();
		}
	} while (nopt != -1);

	if (out == NULL || board == NULL)
		return 1;

	/* Make sure VIF directory exists */
	vifdir = opendir(out);
	if (vifdir == NULL) {
		fprintf(stderr, "ERROR: %s directory does not exist.\n", out);
		return 1;
	}
	closedir(vifdir);

	init_src_pdos();

	name_size = asprintf(&name, "%s/%s_vif.txt", out, board);
	if (name_size < 0) {
		fprintf(stderr, "ERROR: Out of memory.\n");
		return 1;
	}

	ret = gen_vif(name, board, vif_producer);

	free(name);

	return ret;
}
