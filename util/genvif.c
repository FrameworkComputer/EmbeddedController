/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* for asprintf */

#include <errno.h>
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

#define PD_REV_2_0 1
#define PD_REV_3_0 2

#define VIF_SPEC "Revision 1.11, Version 1.0"
#define VENDOR_NAME "Google"
#define PD_SPEC_REV PD_REV_2_0

enum dtype {SNK = 0, SRC = 3, DRP = 4};

const uint32_t vdo_idh __attribute__((weak)) = 0;

const uint32_t *src_pdo;
uint32_t src_pdo_cnt;

char *yes_no(int val)
{
	return val ? "YES" : "NO";
}

enum system_image_copy_t system_get_image_copy(void)
{
	return SYSTEM_IMAGE_RW;
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

static int is_extpwr(void)
{
	if (is_src())
		return !!(src_pdo[0] & PDO_FIXED_EXTERNAL);
	else
		return 0;
}

static int is_drp(void)
{
	if (is_src())
		return !!(src_pdo[0] & PDO_FIXED_DUAL_ROLE);
	else
		return 0;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
static char *giveback(void)
{
#ifdef CONFIG_USB_PD_GIVE_BACK
	return "YES";
#else
	return "NO";
#endif
}
#endif

static char *is_comms_cap(void)
{
	if (is_src())
		return yes_no(src_pdo[0] & PDO_FIXED_COMM_CAP);
	else
		return "NO";
}

static char *dr_swap_to_ufp_supported(void)
{
	if (src_pdo[0] & PDO_FIXED_DATA_SWAP)
		return yes_no(pd_check_data_swap(0, PD_ROLE_DFP));

	return "NO";
}

static char *dr_swap_to_dfp_supported(void)
{
	if (src_pdo[0] & PDO_FIXED_DATA_SWAP)
		return yes_no(pd_check_data_swap(0, PD_ROLE_UFP));

	return "NO";
}

static char *vconn_swap(void)
{
#ifdef CONFIG_USBC_VCONN_SWAP
	return "YES";
#else
	return "NO";
#endif
}

static char *try_src(void)
{
#ifdef CONFIG_USB_PD_TRY_SRC
	return "YES";
#else
	return "NO";
#endif
}

static char *can_act_as_host(void)
{
#ifdef CONFIG_VIF_TYPE_C_CAN_ACT_AS_HOST
	return "YES";
#else
	return "NO";
#endif
}

static char *can_act_as_device(void)
{
#ifdef CONFIG_USB
	return "YES";
#else
	return "NO";
#endif
}

static char *captive_cable(void)
{
#ifdef CONFIG_VIF_CAPTIVE_CABLE
	return "YES";
#else
	return "NO";
#endif
}

static char *sources_vconn(void)
{
#ifdef CONFIG_USBC_VCONN
	return "YES";
#else
	return "NO";
#endif
}

static char *battery_powered(void)
{
#ifdef CONFIG_BATTERY
	return "YES";
#else
	return "NO";
#endif
}

static uint32_t product_type(void)
{
	return PD_IDH_PTYPE(vdo_idh);
}

static uint32_t pid_sop(void)
{
#ifdef CONFIG_USB_PID
	return CONFIG_USB_PID;
#else
	return 0;
#endif
}

static uint32_t rp_value(void)
{
#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	return CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
#else
	return 0;
#endif
}

static char *attempts_discov_sop(enum dtype type)
{
#ifdef CONFIG_USB_PD_SIMPLE_DFP
	if (type == SRC)
		return "NO";
	else
		return "YES";
#else
	return "YES";
#endif
}

static uint32_t bcddevice_sop(void)
{
#ifdef CONFIG_USB_BCD_DEV
	return CONFIG_USB_BCD_DEV;
#else
	return 0;
#endif
}

static uint32_t write_pdo_to_vif(FILE *vif, uint32_t pdo,
				enum dtype type, uint32_t pnum)
{
	uint32_t power = 0;

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t voltage = (pdo >> 10) & 0x3ff;

		power = ((current * 10) * (voltage * 50)) / 1000;

		fprintf(vif, "%s_PDO_Supply_Type%d: 0\r\n",
					(type == SRC) ? "Src" : "Snk", pnum);
		if (type == SRC)
			fprintf(vif, "Src_PDO_Peak_Current%d: 0\r\n", pnum);
		fprintf(vif, "%s_PDO_Voltage%d: %d\r\n",
				(type == SRC) ? "Src" : "Snk", pnum, voltage);
		if (type == SRC)
			fprintf(vif, "Src_PDO_Max_Current%d: %d\r\n",
					pnum, current);
		else
			fprintf(vif, "Snk_PDO_Op_Current%d: %d\r\n",
					pnum, current);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;

		power = pdo & 0x3ff;

		fprintf(vif, "%s_PDO_Supply_Type%d: 1\r\n",
				(type == SRC) ? "Src" : "Snk", pnum);
		fprintf(vif, "%s_PDO_Min_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, min_voltage);
		fprintf(vif, "%s_PDO_Max_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, max_voltage);
		if (type == SRC)
			fprintf(vif, "Src_PDO_Max_Power%d: %d\r\n",
						pnum, power);
		else
			fprintf(vif, "Snk_PDO_Op_Power%d: %d\r\n",
						pnum, power);
	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t current = pdo & 0x3ff;

		power = ((current * 10) * (max_voltage * 50)) / 1000;

		fprintf(vif, "%s_PDO_Supply_Type%d: 2\r\n",
				(type == SRC) ? "Src" : "Snk", pnum);
		if (type == SRC)
			fprintf(vif, "Src_PDO_Peak_Current%d: 0\r\n", pnum);
		fprintf(vif, "%s_PDO_Min_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, min_voltage);
		fprintf(vif, "%s_PDO_Max_Voltage%d: %d\r\n",
			(type == SRC) ? "Src" : "Snk", pnum, max_voltage);
		if (type == SRC)
			fprintf(vif, "Src_PDO_Max_Current%d: %d\r\n",
						pnum, current);
		else
			fprintf(vif, "Snk_PDO_Op_Current%d: %d\r\n",
						pnum, current);
	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
	}

	return power;
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
		/* ie. Twinki or Plankton */
		return 0;
	else if (is_src())
		type = SRC;
	else if (is_snk())
		type = SNK;
	else
		return 1;

	/* Create VIF */
	vif = fopen(name, "w+");
	if (vif == NULL)
		return 1;

	/* Write VIF Header */
	fprintf(vif, "$VIF_Specification: \"%s\"\r\n", VIF_SPEC);
	fprintf(vif, "$VIF_Producer: \"%s\"\r\n", vif_producer);
	fprintf(vif, "$Vendor_Name: \"%s\"\r\n", VENDOR_NAME);
	fprintf(vif, "$Product_Name: \"%s\"\r\n", board);

	fprintf(vif, "PD_Specification_Revision: %d\r\n", PD_SPEC_REV);
	fprintf(vif, "UUT_Device_Type: %d\r\n", type);
	fprintf(vif, "USB_Comms_Capable: %s\r\n", is_comms_cap());
	fprintf(vif, "DR_Swap_To_DFP_Supported: %s\r\n",
				dr_swap_to_dfp_supported());
	fprintf(vif, "DR_Swap_To_UFP_Supported: %s\r\n",
				dr_swap_to_ufp_supported());
	fprintf(vif, "Externally_Powered: %s\r\n", yes_no(is_extpwr()));
	fprintf(vif, "VCONN_Swap_To_On_Supported: %s\r\n", vconn_swap());
	fprintf(vif, "VCONN_Swap_To_Off_Supported: %s\r\n", vconn_swap());
	fprintf(vif, "Responds_To_Discov_SOP: YES\r\n");
	fprintf(vif, "Attempts_Discov_SOP: %s\r\n", attempts_discov_sop(type));
	fprintf(vif, "SOP_Capable: YES\r\n");
	fprintf(vif, "SOP_P_Capable: NO\r\n");
	fprintf(vif, "SOP_PP_Capable: NO\r\n");
	fprintf(vif, "SOP_P_Debug_Capable: NO\r\n");
	fprintf(vif, "SOP_PP_Debug_Capable: NO\r\n");

	/* Write Source Fields */
	if (type == DRP || type == SRC) {
		uint32_t max_power = 0;

		fprintf(vif, "USB_Suspend_May_Be_Cleared: YES\r\n");
		fprintf(vif, "Sends_Pings: NO\r\n");
		fprintf(vif, "Num_Src_PDOs: %d\r\n", src_pdo_cnt);

		/* Write Source PDOs */
		{
			int i;
			uint32_t pwr;

			for (i = 0; i < src_pdo_cnt; i++) {
				pwr = write_pdo_to_vif(vif, src_pdo[i],
							SRC, i+1);
				if (pwr > max_power)
					max_power = pwr;
			}
		}

		fprintf(vif, "PD_Power_as_Source: %d\r\n", max_power);
	}

	/* Write Sink Fields */
#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (type == DRP || type == SNK) {
		uint32_t max_power = 0;
		uint32_t pwr;
		int i;

		fprintf(vif, "USB_Suspend_May_Be_Cleared: NO\r\n");
		fprintf(vif, "GiveBack_May_Be_Set: %s\r\n", giveback());
		fprintf(vif, "Higher_Capability_Set: NO\r\n");
		fprintf(vif, "Num_Snk_PDOs: %d\r\n", pd_snk_pdo_cnt);

		/* Write Sink PDOs */
		for (i = 0; i < pd_snk_pdo_cnt; i++) {
			pwr = write_pdo_to_vif(vif, pd_snk_pdo[i], SNK, i+1);
			if (pwr > max_power)
				max_power = pwr;
		}

		fprintf(vif, "PD_Power_as_Sink: %d\r\n", max_power);
	}

	/* Write DRP Fields */
	if (type == DRP) {
		fprintf(vif, "Accepts_PR_Swap_As_Src: YES\r\n");
		fprintf(vif, "Accepts_PR_Swap_As_Snk: YES\r\n");
		fprintf(vif, "Requests_PR_Swap_As_Src: YES\r\n");
		fprintf(vif, "Requests_PR_Swap_As_Snk: YES\r\n");
	}
#endif

	/* SOP Discovery Fields */
	fprintf(vif, "Structured_VDM_Version_SOP: 0\r\n");
	fprintf(vif, "XID_SOP: 0\r\n");
	fprintf(vif, "Data_Capable_as_USB_Host_SOP: %s\r\n",
						can_act_as_host());
	fprintf(vif, "Data_Capable_as_USB_Device_SOP: %s\r\n",
						can_act_as_device());
	fprintf(vif, "Product_Type_SOP: %d\r\n", product_type());
	fprintf(vif, "Modal_Operation_Supported_SOP: YES\r\n");
	fprintf(vif, "USB_VID_SOP: 0x%04x\r\n", USB_VID_GOOGLE);
	fprintf(vif, "PID_SOP: 0x%04x\r\n", pid_sop());
	fprintf(vif, "bcdDevice_SOP: 0x%04x\r\n", bcddevice_sop());

	fprintf(vif, "SVID1_SOP: 0x%04x\r\n", USB_VID_GOOGLE);
	fprintf(vif, "SVID1_num_modes_min_SOP: 1\r\n");
	fprintf(vif, "SVID1_num_modes_max_SOP: 1\r\n");
	fprintf(vif, "SVID1_num_modes_fixed_SOP: YES\r\n");
	fprintf(vif, "SVID1_mode1_enter_SOP: YES\r\n");

#ifdef USB_SID_DISPLAYPORT
	fprintf(vif, "SVID2_SOP: 0x%04x\r\n", USB_SID_DISPLAYPORT);
	fprintf(vif, "SVID2_num_modes_min_SOP: 2\r\n");
	fprintf(vif, "SVID2_num_modes_max_SOP: 2\r\n");
	fprintf(vif, "SVID2_num_modes_fixed_SOP: YES\r\n");
	fprintf(vif, "SVID2_mode1_enter_SOP: YES\r\n");
	fprintf(vif, "SVID2_mode2_enter_SOP: YES\r\n");

	fprintf(vif, "Num_SVIDs_min_SOP: 2\r\n");
	fprintf(vif, "Num_SVIDs_max_SOP: 2\r\n");
	fprintf(vif, "SVID_fixed_SOP: YES\r\n");
#else
	fprintf(vif, "Num_SVIDs_min_SOP: 1\r\n");
	fprintf(vif, "Num_SVIDs_max_SOP: 1\r\n");
	fprintf(vif, "SVID_fixed_SOP: YES\r\n");
#endif

	/* set Type_C_State_Machine */
	{
		int typec;

		switch (type) {
		case DRP:
			typec = 2;
			break;

		case SNK:
			typec = 1;
			break;

		default:
			typec = 0;
		}

		fprintf(vif, "Type_C_State_Machine: %d\r\n", typec);
	}

	fprintf(vif, "Type_C_Implements_Try_SRC: %s\r\n", try_src());
	fprintf(vif, "Type_C_Implements_Try_SNK: NO\r\n");
	fprintf(vif, "Rp_Value: %d\r\n", rp_value());
	/* None of the current devices send SOP' / SOP", so NO.*/
	fprintf(vif, "Type_C_Supports_VCONN_Powered_Accessory: NO\r\n");
	fprintf(vif, "Type_C_Is_VCONN_Powered_Accessory: NO\r\n");
	fprintf(vif, "Type_C_Can_Act_As_Host: %s\r\n", can_act_as_host());
	fprintf(vif, "Type_C_Host_Speed: 4\r\n");
	fprintf(vif, "Type_C_Can_Act_As_Device: %s\r\n", can_act_as_device());
	fprintf(vif, "Type_C_Device_Speed: 4\r\n");
	fprintf(vif, "Type_C_Power_Source: 2\r\n");
	fprintf(vif, "Type_C_BC_1_2_Support: 1\r\n");
	fprintf(vif, "Type_C_Battery_Powered: %s\r\n", battery_powered());
	fprintf(vif, "Type_C_Port_On_Hub: NO\r\n");
	fprintf(vif, "Type_C_Supports_Audio_Accessory: NO\r\n");
	fprintf(vif, "Captive_Cable: %s\r\n", captive_cable());
	fprintf(vif, "Type_C_Source_Vconn: %s\r\n", sources_vconn());

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
