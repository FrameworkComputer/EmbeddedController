/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* for asprintf */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
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

#include "genvif.h"

const uint32_t *src_pdo;
uint32_t src_pdo_cnt;

/*
 * local type to make decisions on the output for Source, Sink and DRP
 */
enum dtype {
	SRC = 0,
	SNK,
	DRP
};

/*
 * Device_Speed options, defined in the VIF specification
 */
enum usb_speed {
	USB_2 = 0,
	USB_GEN11 = 1,
	USB_GEN21 = 2,
	USB_GEN12 = 3,
	USB_GEN22 = 4
};


/*****************************************************************************
 * Generic Helper Functions
 */
static bool is_src(void)
{
	return src_pdo_cnt;
}
static bool is_snk(void)
{
	return (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE)) ? pd_snk_pdo_cnt : 0;
}
static bool is_drp(void)
{
	if (is_src())
		return !!(src_pdo[0] & PDO_FIXED_DUAL_ROLE);
	return false;
}

static void init_src_pdos(void)
{
	if (IS_ENABLED(CONFIG_USB_PD_DYNAMIC_SRC_CAP))
		src_pdo_cnt = charge_manager_get_source_pdo(&src_pdo, 0);
	else {
		src_pdo_cnt = pd_src_pdo_cnt;
		src_pdo = pd_src_pdo;
	}
}

static bool vif_fields_present(struct vif_field_t *vif_fields, int count)
{
	int index;

	for (index = 0; index < count; ++index) {
		if (vif_fields[index].str_value ||
		    vif_fields[index].tag_value) {
			return true;
		}
	}
	return false;
}


/*****************************************************************************
 * VIF XML Output Functions
 */
static void vif_out_str(FILE *vif_file, int level, char *str)
{
	while (level-- > 0)
		fprintf(vif_file, "  ");
	fprintf(vif_file, "%s\r\n", str);
}

static void vif_out_field(FILE *vif_file, int level,
			  struct vif_field_t *vif_field)
{
	if (vif_field->str_value || vif_field->tag_value) {
		while (level-- > 0)
			fprintf(vif_file, "  ");

		fprintf(vif_file, "<%s", vif_field->name);
		if (vif_field->tag_value)
			fprintf(vif_file, " value=\"%s\"",
				vif_field->tag_value);
		if (vif_field->str_value)
			fprintf(vif_file, ">%s</%s>\r\n",
				vif_field->str_value,
				vif_field->name);
		else
			fprintf(vif_file, "/>\r\n");
	}
}

static void vif_out_fields_range(FILE *vif_file, int level,
			   struct vif_field_t *vif_fields,
			   int start, int count)
{
	int index;

	for (index = start; index < count; ++index)
		vif_out_field(vif_file, level, &vif_fields[index]);
}

static void vif_out_fields(FILE *vif_file, int level,
			   struct vif_field_t *vif_fields, int count)
{
	vif_out_fields_range(vif_file, level, vif_fields, 0, count);
}



static void vif_output_vif_component_cable_svid_mode_list(FILE *vif_file,
			struct vif_cableSVIDList_t *svid_list, int level)
{
	int index;

	if (!vif_fields_present(svid_list->CableSVIDModeList[0].vif_field,
				CableSVID_Mode_Indexes))
		return;

	vif_out_str(vif_file, level++, "<CableSVIDModeList>");
	for (index = 0; index < MAX_NUM_CABLE_SVID_MODES; ++index) {
		struct vif_cableSVIDModeList_t *mode_list =
				&svid_list->CableSVIDModeList[index];

		if (!vif_fields_present(mode_list->vif_field,
					CableSVID_Mode_Indexes))
			break;

		vif_out_str(vif_file, level++, "<SOPSVIDMode>");
		vif_out_fields(vif_file, level,
			       mode_list->vif_field, CableSVID_Mode_Indexes);
		vif_out_str(vif_file, --level, "</SOPSVIDMode>");
	}
	vif_out_str(vif_file, --level, "</CableSVIDModeList>");
}

static void vif_output_vif_component_cable_svid_list(FILE *vif_file,
			struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->CableSVIDList[0].vif_field,
				CableSVID_Indexes))
		return;

	vif_out_str(vif_file, level++, "<CableSVIDList>");
	for (index = 0; index < MAX_NUM_CABLE_SVIDS; ++index) {
		struct vif_cableSVIDList_t *svid_list =
				&component->CableSVIDList[index];

		if (!vif_fields_present(svid_list->vif_field,
					CableSVID_Indexes))
			break;

		vif_out_str(vif_file, level++, "<CableSVID>");
		vif_out_fields(vif_file, level,
			       svid_list->vif_field, CableSVID_Indexes);
		vif_output_vif_component_cable_svid_mode_list(vif_file,
						svid_list, level);
		vif_out_str(vif_file, --level, "</CableSVID>");
	}
	vif_out_str(vif_file, --level, "</CableSVIDList>");
}

static void vif_output_vif_component_sop_svid_mode_list(FILE *vif_file,
			struct vif_sopSVIDList_t *svid_list, int level)
{
	int index;

	if (!vif_fields_present(svid_list->SOPSVIDModeList[0].vif_field,
				SopSVID_Mode_Indexes))
		return;

	vif_out_str(vif_file, level++, "<SOPSVIDModeList>");
	for (index = 0; index < MAX_NUM_SOP_SVID_MODES; ++index) {
		struct vif_sopSVIDModeList_t *mode_list =
				&svid_list->SOPSVIDModeList[index];

		if (!vif_fields_present(mode_list->vif_field,
					SopSVID_Mode_Indexes))
			break;

		vif_out_str(vif_file, level++, "<SOPSVIDMode>");
		vif_out_fields(vif_file, level,
			       mode_list->vif_field, SopSVID_Mode_Indexes);
		vif_out_str(vif_file, --level, "</SOPSVIDMode>");
	}
	vif_out_str(vif_file, --level, "</SOPSVIDModeList>");
}

static void vif_output_vif_component_sop_svid_list(FILE *vif_file,
			struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SOPSVIDList[0].vif_field,
				SopSVID_Indexes))
		return;

	vif_out_str(vif_file, level++, "<SOPSVIDList>");
	for (index = 0; index < MAX_NUM_SOP_SVIDS; ++index) {
		struct vif_sopSVIDList_t *svid_list =
				&component->SOPSVIDList[index];

		if (!vif_fields_present(svid_list->vif_field,
					SopSVID_Indexes))
			break;

		vif_out_str(vif_file, level++, "<SOPSVID>");
		vif_out_fields(vif_file, level,
			       svid_list->vif_field, SopSVID_Indexes);
		vif_output_vif_component_sop_svid_mode_list(vif_file,
						svid_list, level);
		vif_out_str(vif_file, --level, "</SOPSVID>");
	}
	vif_out_str(vif_file, --level, "</SOPSVIDList>");
}

static void vif_output_vif_component_snk_pdo_list(FILE *vif_file,
			struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SnkPdoList[0].vif_field,
				Snk_PDO_Indexes))
		return;

	vif_out_str(vif_file, level++, "<SnkPdoList>");
	for (index = 0; index < MAX_NUM_SNK_PDOS; ++index) {
		struct vif_snkPdoList_t *pdo_list =
				&component->SnkPdoList[index];

		if (!vif_fields_present(pdo_list->vif_field,
					Snk_PDO_Indexes))
			break;

		vif_out_str(vif_file, level++, "<SnkPDO>");
		vif_out_fields(vif_file, level,
			       pdo_list->vif_field, Snk_PDO_Indexes);
		vif_out_str(vif_file, --level, "</SnkPDO>");
	}
	vif_out_str(vif_file, --level, "</SnkPdoList>");
}

static void vif_output_vif_component_src_pdo_list(FILE *vif_file,
			struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SrcPdoList[0].vif_field,
				Src_PDO_Indexes))
		return;

	vif_out_str(vif_file, level++, "<SrcPdoList>");
	for (index = 0; index < MAX_NUM_SRC_PDOS; ++index) {
		struct vif_srcPdoList_t *pdo_list =
				&component->SrcPdoList[index];

		if (!vif_fields_present(pdo_list->vif_field,
					Src_PDO_Indexes))
			break;

		vif_out_str(vif_file, level++, "<SrcPDO>");
		vif_out_fields(vif_file, level,
			       pdo_list->vif_field, Src_PDO_Indexes);
		vif_out_str(vif_file, --level, "</SrcPDO>");
	}
	vif_out_str(vif_file, --level, "</SrcPdoList>");
}

static void vif_output_vif_component(FILE *vif_file,
			struct vif_t *vif, int level)
{
	int index;

	for (index = 0; index < MAX_NUM_COMPONENTS; ++index) {
		struct vif_Component_t *component = &vif->Component[index];

		if (!vif_fields_present(component->vif_field,
					Component_Indexes))
			return;

		vif_out_str(vif_file, level++, "<Component>");
		vif_out_fields(vif_file, level,
			       component->vif_field, Component_Indexes);
		vif_output_vif_component_snk_pdo_list(vif_file,
						component,
						level);
		vif_output_vif_component_src_pdo_list(vif_file,
						component,
						level);
		vif_output_vif_component_sop_svid_list(vif_file,
						component,
						level);
		vif_output_vif_component_cable_svid_list(vif_file,
						component,
						level);
		vif_out_str(vif_file, --level, "</Component>");
	}
}

static void vif_output_vif_product_usb4router_endpoint(FILE *vif_file,
			struct vif_Usb4RouterListType_t *router, int level)
{
	int index;

	if (!vif_fields_present(router->PCIeEndpointList[0].vif_field,
				PCIe_Endpoint_Indexes))
		return;

	vif_out_str(vif_file, level++, "<PCIeEndpointList>");
	for (index = 0; index < MAX_NUM_PCIE_ENDPOINTS; ++index) {
		struct vif_PCIeEndpointListType_t *endpont =
				&router->PCIeEndpointList[index];

		if (!vif_fields_present(endpont->vif_field,
					PCIe_Endpoint_Indexes))
			break;

		vif_out_str(vif_file, level++, "<PCIeEndpoint>");
		vif_out_fields(vif_file, level,
			       endpont->vif_field, PCIe_Endpoint_Indexes);
		vif_out_str(vif_file, --level, "</PCIeEndpoint>");
	}
	vif_out_str(vif_file, --level, "</PCIeEndpointList>");
}

static void vif_output_vif_product_usb4router(FILE *vif_file,
			struct vif_t *vif, int level)
{
	int index;

	if (!vif_fields_present(vif->Product.USB4RouterList[0].vif_field,
				USB4_Router_Indexes))
		return;

	vif_out_str(vif_file, level++, "<USB4RouterList>");
	for (index = 0; index < MAX_NUM_USB4_ROUTERS; ++index) {
		struct vif_Usb4RouterListType_t *router =
				&vif->Product.USB4RouterList[index];

		if (!vif_fields_present(router->vif_field,
					USB4_Router_Indexes))
			break;

		vif_out_str(vif_file, level++, "<USB4Router>");
		vif_out_fields(vif_file, level,
			       router->vif_field, USB4_Router_Indexes);
		vif_output_vif_product_usb4router_endpoint(vif_file,
							   router,
							   level);
		vif_out_str(vif_file, --level, "</USB4Router>");
	}
	vif_out_str(vif_file, --level, "</USB4RouterList>");
}

static void vif_output_vif_product(FILE *vif_file,
			struct vif_t *vif, int level)
{
	if (!vif_fields_present(vif->Product.vif_field, Product_Indexes))
		return;

	vif_out_str(vif_file, level++, "<Product>");
	vif_out_fields(vif_file, level,
		       vif->Product.vif_field, Product_Indexes);
	vif_output_vif_product_usb4router(vif_file, vif, level);
	vif_out_str(vif_file, --level, "</Product>");
}

static void vif_output_vif_xml(FILE *vif_file, struct vif_t *vif, int level)
{
	vif_out_field(vif_file, level, &vif->vif_field[VIF_Specification]);

	vif_out_str(vif_file, level++, "<VIF_App>");
	vif_out_field(vif_file, level, &vif->vif_field[VIF_App_Vendor]);
	vif_out_field(vif_file, level, &vif->vif_field[VIF_App_Name]);
	vif_out_field(vif_file, level, &vif->vif_field[VIF_App_Version]);
	vif_out_str(vif_file, --level, "</VIF_App>");

	vif_out_fields_range(vif_file, level,
		       vif->vif_field, Vendor_Name, VIF_Indexes);
}

static void vif_output_xml(FILE *vif_file, struct vif_t *vif)
{
	int level = 0;

	vif_out_str(vif_file, level++,
		"<VIF xmlns=\"http://usb.org/VendorInfoFile.xsd\">");

	vif_output_vif_xml(vif_file, vif, level);
	vif_output_vif_product(vif_file, vif, level);
	vif_output_vif_component(vif_file, vif, level);

	vif_out_str(vif_file, --level, "</VIF>");
}
/*
 * VIF XML Output Functions
 *****************************************************************************/


/*****************************************************************************
 * VIF Structure Initialization Helper Functions
 */
static void set_vif_field(struct vif_field_t *vif_field,
			char *name,
			char *tag_value,
			char *str_value)
{
	char *ptr;

	if (name) {
		ptr = malloc(strlen(name)+1);
		strcpy(ptr, name);
		vif_field->name = ptr;
	}
	if (str_value) {
		ptr = malloc(strlen(str_value)+1);
		strcpy(ptr, str_value);
		vif_field->str_value = ptr;
	}
	if (tag_value) {
		ptr = malloc(strlen(tag_value)+1);
		strcpy(ptr, tag_value);
		vif_field->tag_value = ptr;
	}
}
__maybe_unused static void set_vif_field_b(struct vif_field_t *vif_field,
			char *name,
			bool val)
{
	if (val)
		set_vif_field(vif_field, name, "true", "YES");
	else
		set_vif_field(vif_field, name, "false", "NO");
}
__maybe_unused static void set_vif_field_stis(struct vif_field_t *vif_field,
			char *name,
			char *tag_value,
			int str_value)
{
	char str_str[80];

	sprintf(str_str, "%d", str_value);
	set_vif_field(vif_field, name, tag_value, str_str);
}
__maybe_unused static void set_vif_field_itss(struct vif_field_t *vif_field,
			char *name,
			int tag_value,
			char *str_value)
{
	char str_tag[80];

	sprintf(str_tag, "%d", tag_value);
	set_vif_field(vif_field, name, str_tag, str_value);
}
__maybe_unused static void set_vif_field_itis(struct vif_field_t *vif_field,
			char *name,
			int tag_value,
			int str_value)
{
	char str_tag[80];
	char str_str[80];

	sprintf(str_tag, "%d", tag_value);
	sprintf(str_str, "%d", str_value);
	set_vif_field(vif_field, name, str_tag, str_str);
}
/*
 * VIF Structure Initialization Helper Functions
 *****************************************************************************/


/*****************************************************************************
 * VIF Structure Initialization from Config Functions
 */
__maybe_unused static int32_t set_vif_snk_pdo(struct vif_snkPdoList_t *snkPdo,
					      uint32_t pdo)
{
	int32_t power;

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t voltage = (pdo >> 10) & 0x3ff;

		power = ((current * 10) * (voltage * 50)) / 1000;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"0", NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Voltage],
				"Snk_PDO_Voltage",
				voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				current, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;

		power = pdo & 0x3ff;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"1", NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				min_voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				max_voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Power],
				"Snk_PDO_Op_Power",
				power, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t current = pdo & 0x3ff;

		power = ((current * 10) * (max_voltage * 50)) / 1000;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"2", NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				min_voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				max_voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				current, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		uint32_t pps = (pdo >> 28) & 3;
		uint32_t pps_max_voltage = (pdo >> 17) & 0xff;
		uint32_t pps_min_voltage = (pdo >> 8) & 0xff;
		uint32_t pps_current = pdo & 0x7f;

		if (pps) {
			fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
			return -1;
		}

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"3", NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				pps_current, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				pps_min_voltage, NULL);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				pps_max_voltage, NULL);
	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power;
}

__maybe_unused static int32_t set_vif_src_pdo(struct vif_srcPdoList_t *srcPdo,
					      uint32_t pdo)
{
	int32_t power;

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t voltage = (pdo >> 10) & 0x3ff;

		power = ((current * 10) * (voltage * 50)) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"0", NULL);
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
				"Src_PDO_Peak_Current",
				"0", NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Voltage],
				"Src_PDO_Voltage",
				voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				current, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;

		power = pdo & 0x3ff;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"1", NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				min_voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Voltage],
				"Src_PDO_Max_Voltage",
				max_voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Power],
				"Src_PDO_Max_Power",
				power, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t current = pdo & 0x3ff;

		power = ((current * 10) * (max_voltage * 50)) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"2", NULL);
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
				"Src_PDO_Peak_Current",
				"0", NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				min_voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Voltage],
				"Src_PDO_Max_Voltage",
				max_voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				current, NULL);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		uint32_t pps = (pdo >> 28) & 3;
		uint32_t pps_max_voltage = (pdo >> 17) & 0xff;
		uint32_t pps_min_voltage = (pdo >> 8) & 0xff;
		uint32_t pps_current = pdo & 0x7f;

		if (pps) {
			fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
			return -1;
		}

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"3", NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				pps_current, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				pps_min_voltage, NULL);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				pps_max_voltage, NULL);

	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power;
}

static int gen_vif(const char *name,
		   const char *board,
		   const char *vif_producer)
{
	FILE *vif_file;
	enum dtype type;
	struct vif_t vif;
	struct vif_field_t *vif_fields;

	/* Determine if we are DRP, SRC or SNK */
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

	/* Start with an empty vif */
	memset(&vif, 0, sizeof(struct vif_t));

	/* VIF */
	vif_fields = vif.vif_field;

	set_vif_field(&vif_fields[VIF_Specification],
			"VIF_Specification",
			NULL,
			"Version 3.12");

	set_vif_field(&vif_fields[VIF_App_Vendor],
			"Vendor",
			NULL,
			"Google");

	set_vif_field(&vif_fields[VIF_App_Name],
			"Name",
			NULL,
			(char *)vif_producer);

	set_vif_field(&vif_fields[VIF_App_Version],
			"Version",
			NULL,
			"3.0.0.2");

	set_vif_field(&vif_fields[Vendor_Name],
			"Vendor_Name",
			NULL,
			"Google");

	#if defined(CONFIG_USB_PD_MODEL_PART_NUMBER)
		set_vif_field_stis(&vif_fields[Model_Part_Number],
				"Model_Part_Number",
				NULL,
				CONFIG_USB_PD_MODEL_PART_NUMBER);
	#endif

	#if defined(CONFIG_USB_PD_PRODUCT_REVISION)
		set_vif_field_stis(&vif_fields[Product_Revision],
				"Product_Revision",
				NULL,
				CONFIG_USB_PD_PRODUCT_REVISION);
	#endif

	#if defined(CONFIG_USB_PD_TID)
		set_vif_field_stis(&vif_fields[TID],
				"TID",
				NULL,
				CONFIG_USB_PD_TID);
	#endif

	set_vif_field(&vif_fields[VIF_Product_Type],
			"VIF_Product_Type",
			"0",
			"Port Product");

	set_vif_field(&vif_fields[Certification_Type],
			"Certification_Type",
			"1",
			"Reference Platform");

	/* VIF/Product */
	vif_fields = vif.Product.vif_field;

	#if defined(CONFIG_USB_PD_PORT_LABEL)
		set_vif_field_stis(&vif_fields[Port_Label],
				"Port_Label",
				NULL,
				CONFIG_USB_PD_PORT_LABEL);
	#endif

	/* VIF/Component[0] */
	vif_fields = vif.Component[0].vif_field;

	set_vif_field(&vif_fields[Connector_Type],
			"Connector_Type",
			"2",
			"USB Type-C");

	set_vif_field_b(&vif_fields[USB_PD_Support],
			"USB_PD_Support",
			(IS_ENABLED(CONFIG_USB_PRL_SM) ||
			 IS_ENABLED(CONFIG_USB_POWER_DELIVERY)));

	switch (type) {
	case SNK:
		set_vif_field(&vif_fields[PD_Port_Type],
				"PD_Port_Type",
				"0",
				"Consumer Only");
		set_vif_field(&vif_fields[Type_C_State_Machine],
				"Type_C_State_Machine",
				"1",
				"SNK");
		break;
	case SRC:
		set_vif_field(&vif_fields[PD_Port_Type],
				"PD_Port_Type",
				"3",
				"Provider Only");
		set_vif_field(&vif_fields[Type_C_State_Machine],
				"Type_C_State_Machine",
				"0",
				"SRC");
		break;
	case DRP:
		set_vif_field(&vif_fields[PD_Port_Type],
				"PD_Port_Type",
				"4",
				"DRP");
		set_vif_field(&vif_fields[Type_C_State_Machine],
				"Type_C_State_Machine",
				"2",
				"DRP");
		break;
	}

	set_vif_field_b(&vif_fields[Captive_Cable],
			"Captive_Cable",
			false);

	set_vif_field_b(&vif_fields[Port_Battery_Powered],
			"Port_Battery_Powered",
			IS_ENABLED(CONFIG_BATTERY));

	set_vif_field_b(&vif_fields[BC_1_2_Support],
			"BC_1_2_Support",
			false);

	if (IS_ENABLED(CONFIG_USB_PD_REV30) || IS_ENABLED(CONFIG_USB_PRL_SM))
		set_vif_field(&vif_fields[PD_Specification_Revision],
				"PD_Specification_Revision",
				"2",
				"Revision 3.0");
	else
		set_vif_field(&vif_fields[PD_Specification_Revision],
				"PD_Specification_Revision",
				"1",
				"Revision 2.0");

	set_vif_field_b(&vif_fields[USB_Comms_Capable],
			"USB_Comms_Capable",
			(!(IS_ENABLED(CONFIG_USB_VPD) ||
			   IS_ENABLED(CONFIG_USB_CTVPD))));

	if (is_src() && (src_pdo[0] & PDO_FIXED_DATA_SWAP))
		set_vif_field_b(&vif_fields[DR_Swap_To_DFP_Supported],
				"DR_Swap_To_DFP_Supported",
				pd_check_data_swap(0, PD_ROLE_DFP));
	else
		set_vif_field_b(&vif_fields[DR_Swap_To_DFP_Supported],
				"DR_Swap_To_DFP_Supported",
				false);

	if (is_src() && (src_pdo[0] & PDO_FIXED_DATA_SWAP))
		set_vif_field_b(&vif_fields[DR_Swap_To_UFP_Supported],
				"DR_Swap_To_UFP_Supported",
				pd_check_data_swap(0, PD_ROLE_UFP));
	else
		set_vif_field_b(&vif_fields[DR_Swap_To_UFP_Supported],
				"DR_Swap_To_UFP_Supported",
				false);

	if (is_src())
		set_vif_field_b(&vif_fields[Unconstrained_Power],
				"Unconstrained_Power",
				src_pdo[0] & PDO_FIXED_UNCONSTRAINED);
	else
		set_vif_field_b(&vif_fields[Unconstrained_Power],
				"Unconstrained_Power",
				false);

	set_vif_field_b(&vif_fields[VCONN_Swap_To_On_Supported],
			"VCONN_Swap_To_On_Supported",
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP));

	set_vif_field_b(&vif_fields[VCONN_Swap_To_Off_Supported],
			"VCONN_Swap_To_Off_Supported",
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP));

	set_vif_field_b(&vif_fields[Responds_To_Discov_SOP_UFP],
			"Responds_To_Discov_SOP_UFP",
			false);

	set_vif_field_b(&vif_fields[Responds_To_Discov_SOP_DFP],
			"Responds_To_Discov_SOP_DFP",
			false);

	set_vif_field_b(&vif_fields[Attempts_Discov_SOP],
			"Attempts_Discov_SOP",
			((!IS_ENABLED(CONFIG_USB_PD_SIMPLE_DFP)) ||
			 (type != SRC)));

	set_vif_field_b(&vif_fields[Chunking_Implemented_SOP],
			"Chunking_Implemented_SOP",
			(IS_ENABLED(CONFIG_USB_PD_REV30) &&
			 IS_ENABLED(CONFIG_USB_PRL_SM)));

	set_vif_field_b(&vif_fields[Unchunked_Extended_Messages_Supported],
			"Unchunked_Extended_Messages_Supported",
			false);

	set_vif_field_b(&vif_fields[Manufacturer_Info_Supported_Port],
			"Manufacturer_Info_Supported_Port",
			IS_ENABLED(CONFIG_USB_PD_MANUFACTURER_INFO));

	#if defined(USB_PID_GOOGLE)
	{
		char hex_str[10];

		sprintf(hex_str, "0x%04X", USB_PID_GOOGLE);
		set_vif_field(&vif_fields[Manufacturer_Info_PID_Port],
			      "Manufacturer_Info_PID_Port",
			      hex_str, hex_str);
	}
	#endif

	set_vif_field_b(&vif_fields[Security_Msgs_Supported_SOP],
			"Security_Msgs_Supported_SOP",
			IS_ENABLED(CONFIG_USB_PD_SECURITY_MSGS));

	#if defined(CONFIG_NUM_FIXED_BATTERIES)
		set_vif_field_itss(&vif_fields[Num_Fixed_Batteries],
				"Num_Fixed_Batteries",
				CONFIG_NUM_FIXED_BATTERIES, NULL);
	#elif defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
		set_vif_field(&vif_fields[Num_Fixed_Batteries],
				"Num_Fixed_Batteries",
				"0", NULL);
	#else
		set_vif_field(&vif_fields[Num_Fixed_Batteries],
				"Num_Fixed_Batteries",
				"1", NULL);
	#endif

	set_vif_field(&vif_fields[Num_Swappable_Battery_Slots],
			"Num_Swappable_Battery_Slots",
			"0", NULL);

	set_vif_field_b(&vif_fields[SOP_Capable],
			"SOP_Capable",
			(!(IS_ENABLED(CONFIG_USB_CTVPD) ||
			   IS_ENABLED(CONFIG_USB_VPD))));

	set_vif_field_b(&vif_fields[SOP_P_Capable],
			"SOP_P_Capable",
			(IS_ENABLED(CONFIG_USB_CTVPD) ||
			 IS_ENABLED(CONFIG_USB_VPD)));

	set_vif_field_b(&vif_fields[SOP_PP_Capable],
			"SOP_PP_Capable",
			false);

	set_vif_field_b(&vif_fields[SOP_P_Debug_Capable],
			"SOP_P_Debug_Capable",
			false);

	set_vif_field_b(&vif_fields[SOP_PP_Debug_Capable],
			"SOP_PP_Debug_Capable",
			false);

	set_vif_field_b(&vif_fields[Type_C_Implements_Try_SRC],
			"Type_C_Implements_Try_SRC",
			IS_ENABLED(CONFIG_USB_PD_TRY_SRC));

	set_vif_field_b(&vif_fields[Type_C_Implements_Try_SNK],
			"Type_C_Implements_Try_SNK",
			false);

	{
		int rp = CONFIG_USB_PD_PULLUP;

		#if defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
			rp = CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
		#endif

		switch (rp) {
		case 0:
			set_vif_field(&vif_fields[RP_Value],
					"RP_Value",
					"0", "Default");
			break;
		case 1:
			set_vif_field(&vif_fields[RP_Value],
					"RP_Value",
					"1", "1.5A");
			break;
		case 2:
			set_vif_field(&vif_fields[RP_Value],
					"RP_Value",
					"2", "3A");
			break;
		default:
			set_vif_field_itss(&vif_fields[RP_Value],
					"RP_Value",
					rp, NULL);
		}
	}

	set_vif_field_b(&vif_fields[Type_C_Supports_VCONN_Powered_Accessory],
			"Type_C_Supports_VCONN_Powered_Accessory",
			false);
	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SRC],
			"Type_C_Is_Debug_Target_SRC",
			true);
	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SNK],
			"Type_C_Is_Debug_Target_SNK",
			true);

	set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Host],
			"Type_C_Can_Act_As_Host",
			(!(IS_ENABLED(CONFIG_USB_CTVPD) ||
			   IS_ENABLED(CONFIG_USB_VPD))));

	set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Controller],
			"Type_C_Is_Alt_Mode_Controller",
			false);

	#if defined(USB_DEV_CLASS) && defined(USB_CLASS_BILLBOARD)
		set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Device],
				"Type_C_Can_Act_As_Device",
				(USB_DEV_CLASS == USB_CLASS_BILLBOARD));
	#else
		set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Device],
				"Type_C_Can_Act_As_Device",
				false);
	#endif

	set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Adapter],
			"Type_C_Is_Alt_Mode_Adapter",
			(IS_ENABLED(CONFIG_USB_ALT_MODE_ADAPTER)));

	{
		int ps = 1;

		#if defined(CONFIG_DEDICATED_CHARGE_PORT_COUNT)
			if (CONFIG_DEDICATED_CHARGE_PORT_COUNT == 1)
				ps = 0;
		#endif

		switch (ps) {
		case 0:
			set_vif_field(&vif_fields[Type_C_Power_Source],
					"Type_C_Power_Source",
					"0", "Externally Powered");
			break;
		case 1:
			set_vif_field(&vif_fields[Type_C_Power_Source],
					"Type_C_Power_Source",
					"1", "USB-powered");
			break;
		case 2:
			set_vif_field(&vif_fields[Type_C_Power_Source],
					"Type_C_Power_Source",
					"2", "Both");
			break;
		default:
			set_vif_field_itss(&vif_fields[Type_C_Power_Source],
					"Type_C_Power_Source",
					ps, NULL);
		}
	}

	set_vif_field_b(&vif_fields[Type_C_Port_On_Hub],
			"Type_C_Port_On_Hub",
			false);
	set_vif_field_b(&vif_fields[Type_C_Supports_Audio_Accessory],
			"Type_C_Supports_Audio_Accessory",
			false);

	set_vif_field_b(&vif_fields[Type_C_Sources_VCONN],
			"Type_C_Sources_VCONN",
			IS_ENABLED(CONFIG_USBC_VCONN));

	{
		int ds = USB_2;

		switch (ds) {
		case USB_2:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					USB_2, "USB 2");
			break;
		case USB_GEN11:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					USB_GEN11, "USB 3.2 GEN 1x1");
			break;
		case USB_GEN21:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					USB_GEN21, "USB 3.2 GEN 2x1");
			break;
		case USB_GEN12:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					USB_GEN12, "USB 3.2 GEN 1x2");
			break;
		case USB_GEN22:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					USB_GEN22, "USB 3.2 GEN 2x2");
			break;
		default:
			set_vif_field_itss(&vif_fields[Device_Speed],
					"Device_Speed",
					ds, NULL);
		}
	}

	if (type == DRP || type == SRC) {
		uint32_t max_power = 0;
		int i;
		int32_t pwr;

		/* Source PDOs */
		for (i = 0; i < src_pdo_cnt; i++) {
			pwr = set_vif_src_pdo(&vif.Component[0].SrcPdoList[i],
					      src_pdo[i]);
			if (pwr < 0) {
				fprintf(stderr, "ERROR: Setting SRC PDO.\n");
				return 1;
			}

			if (pwr > max_power)
				max_power = pwr;
		}

		/* Source Fields */
		set_vif_field_itss(&vif_fields[PD_Power_As_Source],
				"PD_Power_As_Source",
				max_power, NULL);
		set_vif_field_b(&vif_fields[USB_Suspend_May_Be_Cleared],
				"USB_Suspend_May_Be_Cleared",
				true);
		set_vif_field_b(&vif_fields[Sends_Pings],
				"Sends_Pings",
				false);
		set_vif_field_itss(&vif_fields[Num_Src_PDOs],
				"Num_Src_PDOs",
				src_pdo_cnt, NULL);

		if (IS_ENABLED(CONFIG_USBC_PPC)) {
			int resp = 0;

			set_vif_field_b(&vif_fields[PD_OC_Protection],
					"PD_OC_Protection",
					true);

			switch (resp) {
			case 0:
				set_vif_field(&vif_fields[PD_OCP_Method],
						"PD_OCP_Method",
						"0", "Over-Current Response");
				break;
			case 1:
				set_vif_field(&vif_fields[PD_OCP_Method],
						"PD_OCP_Method",
						"1", "Under-Voltage Response");
				break;
			case 2:
				set_vif_field(&vif_fields[PD_OCP_Method],
						"PD_OCP_Method",
						"2", "Both");
				break;
			default:
				set_vif_field_itss(&vif_fields[PD_OCP_Method],
						"PD_OCP_Method",
						resp, NULL);
			}
		} else {
			set_vif_field_b(&vif_fields[PD_OC_Protection],
					"PD_OC_Protection",
					false);
		}
	}

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK)) {
		uint32_t max_power = 0;
		int32_t pwr;
		int i;
		bool giveback = false;

		if (IS_ENABLED(CONFIG_USB_PD_GIVE_BACK))
			giveback = true;

		/* Sink PDOs */
		for (i = 0; i < pd_snk_pdo_cnt; i++) {
			pwr = set_vif_snk_pdo(&vif.Component[0].SnkPdoList[i],
					      pd_snk_pdo[i]);

			if (pwr < 0) {
				fprintf(stderr, "ERROR: Setting SNK PDO.\n");
				return 1;
			}

			if (pwr > max_power)
				max_power = pwr;
		}

		/* Sink Fields */
		set_vif_field_itss(&vif_fields[PD_Power_As_Sink],
				"PD_Power_As_Sink",
				max_power, NULL);
		set_vif_field_b(&vif_fields[No_USB_Suspend_May_Be_Set],
				"No_USB_Suspend_May_Be_Set",
				true);
		set_vif_field_b(&vif_fields[GiveBack_May_Be_Set],
				"GiveBack_May_Be_Set",
				giveback);
		set_vif_field_b(&vif_fields[Higher_Capability_Set],
				"Higher_Capability_Set",
				false);
		set_vif_field_itss(&vif_fields[Num_Snk_PDOs],
				"Num_Snk_PDOs",
				pd_snk_pdo_cnt, NULL);
	}

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP) {
		set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Src],
				"Accepts_PR_Swap_As_Src",
				true);
		set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Snk],
				"Accepts_PR_Swap_As_Snk",
				true);
		set_vif_field_b(&vif_fields[Requests_PR_Swap_As_Src],
				"Requests_PR_Swap_As_Src",
				true);
		set_vif_field_b(&vif_fields[FR_Swap_Supported_As_Initial_Sink],
				"FR_Swap_Supported_As_Initial_Sink",
				false);
	}

	/* Format the structure in XML */
	vif_file = fopen(name, "w+");
	if (vif_file == NULL)
		return 1;

	vif_output_xml(vif_file, &vif);
	fclose(vif_file);

	return 0;
}
/*
 * VIF Structure Initialization from Config Functions
 *****************************************************************************/

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
