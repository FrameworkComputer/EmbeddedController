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

#define VIF_APP_VENDOR_VALUE	"Google"
#define VIF_APP_NAME_VALUE	"EC GENVIF"
#define VIF_APP_VERSION_VALUE	"3.0.0.5"
#define VENDOR_NAME_VALUE	"Google"

#define DEFAULT_MISSING_TID	0xFFFF
#define DEFAULT_MISSING_PID	0xFFFF

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

/*
 * BC_1_2_SUPPORT options
 */
enum bc_1_2_support {
	BC_1_2_SUPPORT_NONE = 0,
	BC_1_2_SUPPORT_PORTABLE_DEVICE = 1,
	BC_1_2_SUPPORT_CHARGING_PORT = 2,
	BC_1_2_SUPPORT_BOTH = 3
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

static bool can_act_as_device(void)
{
	#if defined(USB_DEV_CLASS) && defined(USB_CLASS_BILLBOARD)
		return (USB_DEV_CLASS == USB_CLASS_BILLBOARD);
	#else
		return false;
	#endif
}

static bool can_act_as_host(void)
{
	return (!(IS_ENABLED(CONFIG_USB_CTVPD) ||
		IS_ENABLED(CONFIG_USB_VPD)));
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

static int vif_output_xml(const char *name, struct vif_t *vif)
{
	int level = 0;
	FILE *vif_file;

	vif_file = fopen(name, "w+");
	if (vif_file == NULL)
		return 1;

	vif_out_str(vif_file, level,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	vif_out_str(vif_file, level++,
		"<VIF xmlns=\"http://usb.org/VendorInfoFile.xsd\">");

	vif_output_vif_xml(vif_file, vif, level);
	vif_output_vif_product(vif_file, vif, level);
	vif_output_vif_component(vif_file, vif, level);

	vif_out_str(vif_file, --level, "</VIF>");

	fclose(vif_file);
	return 0;
}
/*
 * VIF XML Output Functions
 *****************************************************************************/


/*****************************************************************************
 * VIF Structure Initialization Helper Functions
 */
static void set_vif_field(struct vif_field_t *vif_field,
			const char *name,
			const char *tag_value,
			const char *str_value)
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
			const char *name,
			const bool val)
{
	if (val)
		set_vif_field(vif_field, name, "true", "YES");
	else
		set_vif_field(vif_field, name, "false", "NO");
}
__maybe_unused static void set_vif_field_stis(struct vif_field_t *vif_field,
			const char *name,
			const char *tag_value,
			const int str_value)
{
	char str_str[80];

	sprintf(str_str, "%d", str_value);
	set_vif_field(vif_field, name, tag_value, str_str);
}
__maybe_unused static void set_vif_field_itss(struct vif_field_t *vif_field,
			const char *name,
			const int tag_value,
			const char *str_value)
{
	char str_tag[80];

	sprintf(str_tag, "%d", tag_value);
	set_vif_field(vif_field, name, str_tag, str_value);
}
__maybe_unused static void set_vif_field_itis(struct vif_field_t *vif_field,
			const char *name,
			const int tag_value,
			const int str_value)
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
/*
 * TODO: Generic todo to fill in additional fields as the need presents
 * itself
 *
 * Fields that are not currently being initialized
 *
 * vif_cableSVIDModeList
 *	SVID_Mode_Enter				booleanFieldType
 *	SVID_Mode_Recog_Mask			numericFieldType
 *	SVID_Mode_Recog_Value			numericFieldType
 *
 * vif_cableSVIDList
 *	SVID					numericFieldType
 *	SVID_Modes_Fixed			booleanFieldType
 *	SVID_Num_Modes_Min			numericFieldType
 *	SVID_Num_Modes_Max			numericFieldType
 *
 * vif_sopSVIDModeList
 *	SVID_Mode_Enter_SOP			booleanFieldType
 *	SVID_Mode_Recog_Mask_SOP		numericFieldType
 *	SVID_Mode_Recog_Value_SOP		numericFieldType
 *
 * vif_sopSVIDList
 *	SVID_SOP				numericFieldType
 *	SVID_Modes_Fixed_SOP			booleanFieldType
 *	SVID_Num_Modes_Min_SOP			numericFieldType
 *	SVID_Num_Modes_Max_SOP			numericFieldType
 *
 * vif_srcPdoList
 *	Src_PD_OCP_OC_Debounce			numericFieldType
 *	Src_PD_OCP_OC_Threshold			numericFieldType
 *	Src_PD_OCP_UV_Debounce			numericFieldType
 *	Src_PD_OCP_UV_Threshold_Type		numericFieldType
 *	Src_PD_OCP_UV_Threshold			numericFieldType
 *
 * vif_PCIeEndpointListType
 *	USB4_PCIe_Endpoint_Vendor_ID		numericFieldType
 *	USB4_PCIe_Endpoint_Device_ID		numericFieldType
 *	USB4_PCIe_Endpoint_Class_Code		numericFieldType
 *
 * vif_Usb4RouterListType
 *	USB4_Router_ID				numericFieldType
 *	USB4_Silicon_VID			numericFieldType
 *	USB4_Num_Lane_Adapters			numericFieldType
 *	USB4_Num_USB3_DN_Adapters		numericFieldType
 *	USB4_Num_DP_IN_Adapters			numericFieldType
 *	USB4_Num_DP_OUT_Adapters		numericFieldType
 *	USB4_Num_PCIe_DN_Adapters		numericFieldType
 *	USB4_TBT3_Not_Supported			numericFieldType
 *	USB4_PCIe_Wake_Supported		booleanFieldType
 *	USB4_USB3_Wake_Supported		booleanFieldType
 *	USB4_Num_Unused_Adapters		numericFieldType
 *	USB4_TBT3_VID				numericFieldType
 *	USB4_PCIe_Switch_Vendor_ID		numericFieldType
 *	USB4_PCIe_Switch_Device_ID		numericFieldType
 *	USB4_Num_PCIe_Endpoints			numericFieldType
 *
 * vif_Component
 *	Manufacturer_Info_VID_Port		numericFieldType
 *	USB4_Router_Index			numericFieldType
 *	USB4_Lane_0_Adapter			numericFieldType
 *	USB4_Max_Speed				numericFieldType
 *	USB4_DFP_Supported			booleanFieldType
 *	USB4_UFP_Supported			booleanFieldType
 *	USB4_USB3_Tunneling_Supported		booleanFieldType
 *	USB4_DP_Tunneling_Supported		booleanFieldType
 *	USB4_PCIe_Tunneling_Supported		booleanFieldType
 *	USB4_TBT3_Compatibility_Supported	booleanFieldType
 *	USB4_CL1_State_Supported		booleanFieldType
 *	USB4_CL2_State_Supported		booleanFieldType
 *	USB4_Num_Retimers			numericFieldType
 *	USB4_DP_Bit_Rate			numericFieldType
 *	USB4_Num_DP_Lanes			numericFieldType
 *	Host_Supports_USB_Data			booleanFieldType
 *	Host_Truncates_DP_For_tDHPResponse	booleanFieldType
 *	Host_Gen1x1_tLinkTurnaround		numericFieldType
 *	Host_Gen2x1_tLinkTurnaround		numericFieldType
 *	Host_Suspend_Supported			booleanFieldType
 *	Is_DFP_On_Hub				booleanFieldType
 *	Hub_Port_Number				numericFieldType
 *	Device_Supports_USB_Data		booleanFieldType
 *	Device_Contains_Captive_Retimer		booleanFieldType
 *	Device_Truncates_DP_For_tDHPResponse	booleanFieldType
 *	Device_Gen1x1_tLinkTurnaround		numericFieldType
 *	Device_Gen2x1_tLinkTurnaround		numericFieldType
 *	XID_SOP					numericFieldType
 *	Data_Capable_As_USB_Host_SOP		booleanFieldType
 *	Data_Capable_As_USB_Device_SOP		booleanFieldType
 *	Product_Type_UFP_SOP			numericFieldType
 *	Product_Type_DFP_SOP			numericFieldType
 *	DFP_VDO_Port_Number			numericFieldType
 *	Modal_Operation_Supported_SOP		booleanFieldType
 *	USB_VID_SOP				numericFieldType
 *	bcdDevice_SOP				numericFieldType
 *	SVID_Fixed_SOP				booleanFieldType
 *	Num_SVIDs_Min_SOP			numericFieldType
 *	Num_SVIDs_Max_SOP			numericFieldType
 *	AMA_HW_Vers				numericFieldType
 *	AMA_FW_Vers				numericFieldType
 *	AMA_VCONN_Reqd				booleanFieldType
 *	AMA_VCONN_Power				booleanFieldType
 *	AMA_VBUS_Reqd				booleanFieldType
 *	AMA_Superspeed_Support			numericFieldType
 *	Port_Source_Power_Gang			nonEmptyString
 *	Port_Source_Power_Gang_Max_Power	numericFieldType
 *	XID					numericFieldType
 *	Data_Capable_As_USB_Host		booleanFieldType
 *	Data_Capable_As_USB_Device		booleanFieldType
 *	Product_Type				numericFieldType
 *	Modal_Operation_Supported		booleanFieldType
 *	USB_VID					numericFieldType
 *	PID					numericFieldType
 *	bcdDevice				numericFieldType
 *	Cable_HW_Vers				numericFieldType
 *	Cable_FW_Vers				numericFieldType
 *	Type_C_To_Type_A_B_C			numericFieldType
 *	Type_C_To_Type_C_Capt_Vdm_V2		numericFieldType
 *	Cable_Latency				numericFieldType
 *	Cable_Termination_Type			numericFieldType
 *	VBUS_Through_Cable			booleanFieldType
 *	Cable_VBUS_Current			numericFieldType
 *	Cable_Superspeed_Support		numericFieldType
 *	Cable_USB_Highest_Speed			numericFieldType
 *	Max_VBUS_Voltage_Vdm_V2			numericFieldType
 *	Manufacturer_Info_Supported,		booleanFieldType
 *	Manufacturer_Info_VID,			numericFieldType
 *	Manufacturer_Info_PID,			numericFieldType
 *	Chunking_Implemented			booleanFieldType
 *	Security_Msgs_Supported			booleanFieldType
 *	ID_Header_Connector_Type		numericFieldType
 *	SVID_Fixed				booleanFieldType
 *	Cable_Num_SVIDs_Min			numericFieldType
 *	Cable_Num_SVIDs_Max			numericFieldType
 *	VPD_HW_Vers				numericFieldType
 *	VPD_FW_Vers				numericFieldType
 *	VPD_Max_VBUS_Voltage			numericFieldType
 *	VPD_Charge_Through_Support		booleanFieldType
 *	VPD_Charge_Through_Current		numericFieldType
 *	VPD_VBUS_Impedance			numericFieldType
 *	VPD_Ground_Impedance			numericFieldType
 *	Cable_SOP_PP_Controller			booleanFieldType
 *	SBU_Supported				booleanFieldType
 *	SBU_Type				numericFieldType
 *	Active_Cable_Operating_Temp_Support	booleanFieldType
 *	Active_Cable_Max_Operating_Temp		numericFieldType
 *	Active_Cable_Shutdown_Temp_Support	booleanFieldType
 *	Active_Cable_Shutdown_Temp		numericFieldType
 *	Active_Cable_U3_CLd_Power		numericFieldType
 *	Active_Cable_U3_U0_Trans_Mode		numericFieldType
 *	Active_Cable_Physical_Connection	numericFieldType
 *	Active_Cable_Active_Element		numericFieldType
 *	Active_Cable_USB4_Support		booleanFieldType
 *	Active_Cable_USB2_Hub_Hops_Consumed	numericFieldType
 *	Active_Cable_USB2_Supported		booleanFieldType
 *	Active_Cable_USB32_Supported		booleanFieldType
 *	Active_Cable_USB_Lanes			numericFieldType
 *	Active_Cable_Optically_Isolated		booleanFieldType
 *	Active_Cable_USB_Gen			numericFieldType
 *	Repeater_One_Type			numericFieldType
 *	Repeater_Two_Type			numericFieldType
 *
 * vif_Product
 *	USB4_Dock				booleanFieldType
 *	USB4_Num_Internal_Host_Controllers	numericFieldType
 *	USB4_Num_PCIe_DN_Bridges		numericFieldType
 *	USB4_Audio_Supported			booleanFieldType
 *	USB4_HID_Supported			booleanFieldType
 *	USB4_Printer_Supported			booleanFieldType
 *	USB4_Mass_Storage_Supported		booleanFieldType
 *	USB4_Video_Supported			booleanFieldType
 *	USB4_Comms_Networking_Supported		booleanFieldType
 *	USB4_Media_Transfer_Protocol_Supported	booleanFieldType
 *	USB4_Smart_Card_Supported		booleanFieldType
 *	USB4_Still_Image_Capture_Supported	booleanFieldType
 *	USB4_Monitor_Device_Supported		booleanFieldType
 */

__maybe_unused static int32_t set_vif_snk_pdo(struct vif_snkPdoList_t *snkPdo,
					      uint32_t pdo)
{
	int32_t power_mw;
	char str[40];

	/*********************************************************************
	 * Sink PDOs
	 */
	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;
		uint32_t voltage = (pdo >> 10) & 0x3ff;
		uint32_t voltage_mv = voltage * 50;

		power_mw = (current_ma * voltage_mv) / 1000;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"0", "Fixed");
		sprintf(str, "%dmV", voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Voltage],
				"Snk_PDO_Voltage",
				voltage, str);
		sprintf(str, "%dmA", current_ma);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				current, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		int32_t power;

		power = pdo & 0x3ff;
		power_mw = power * 250;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"1", "Battery");
		sprintf(str, "%dmV", min_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				min_voltage, str);
		sprintf(str, "%dmV", max_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				max_voltage, str);
		sprintf(str, "%dmW", power_mw);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Power],
				"Snk_PDO_Op_Power",
				power, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;

		power_mw = (current_ma * max_voltage_mv) / 1000;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"2", "Variable (non-battery)");
		sprintf(str, "%dmV", min_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				min_voltage, str);
		sprintf(str, "%dmV", max_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				max_voltage, str);
		sprintf(str, "%dmA", current_ma);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				current, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		uint32_t pps = (pdo >> 28) & 3;
		uint32_t pps_max_voltage = (pdo >> 17) & 0xff;
		uint32_t pps_max_voltage_mv = pps_max_voltage * 100;
		uint32_t pps_min_voltage = (pdo >> 8) & 0xff;
		uint32_t pps_min_voltage_mv = pps_min_voltage * 100;
		uint32_t pps_current = pdo & 0x7f;
		uint32_t pps_current_ma = pps_current * 50;

		if (pps) {
			fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
			return -1;
		}

		power_mw = (pps_current_ma * pps_max_voltage_mv) / 1000;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
				"Snk_PDO_Supply_Type",
				"3", "PPS");
		sprintf(str, "%dmA", pps_current_ma);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Current],
				"Snk_PDO_Op_Current",
				pps_current, str);
		sprintf(str, "%dmV", pps_min_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Min_Voltage],
				"Snk_PDO_Min_Voltage",
				pps_min_voltage, str);
		sprintf(str, "%dmV", pps_max_voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Max_Voltage],
				"Snk_PDO_Max_Voltage",
				pps_max_voltage, str);
	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power_mw;
}

__maybe_unused static int32_t set_vif_src_pdo(struct vif_srcPdoList_t *srcPdo,
					      uint32_t pdo)
{
	int32_t power_mw;
	char str[40];

	/*********************************************************************
	 * Source PDOs
	 */
	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;
		uint32_t voltage = (pdo >> 10) & 0x3ff;
		uint32_t voltage_mv = voltage * 50;

		power_mw = (current_ma * voltage_mv) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"0", "Fixed");
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
				"Src_PDO_Peak_Current",
				"0", "100% IOC");
		sprintf(str, "%dmV", voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Voltage],
				"Src_PDO_Voltage",
				voltage, str);
		sprintf(str, "%dmA", current_ma);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				current, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		int32_t power;

		power = pdo & 0x3ff;
		power_mw = power * 250;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"1", "Battery");
		sprintf(str, "%dmV", min_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				min_voltage, str);
		sprintf(str, "%dmV", max_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Voltage],
				"Src_PDO_Max_Voltage",
				max_voltage, str);
		sprintf(str, "%dmW", power_mw);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Power],
				"Src_PDO_Max_Power",
				power, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;

		power_mw = (current_ma * max_voltage_mv) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"2", "Variable (non-battery)");
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
				"Src_PDO_Peak_Current",
				"0", "100% IOC");
		sprintf(str, "%dmV", min_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				min_voltage, str);
		sprintf(str, "%dmV", max_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Voltage],
				"Src_PDO_Max_Voltage",
				max_voltage, str);
		sprintf(str, "%dmA", current_ma);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				current, str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED) {
		uint32_t pps = (pdo >> 28) & 3;
		uint32_t pps_max_voltage = (pdo >> 17) & 0xff;
		uint32_t pps_max_voltage_mv = pps_max_voltage * 100;
		uint32_t pps_min_voltage = (pdo >> 8) & 0xff;
		uint32_t pps_min_voltage_mv = pps_min_voltage * 100;
		uint32_t pps_current = pdo & 0x7f;
		uint32_t pps_current_ma = pps_current * 50;

		if (pps) {
			fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
			return -1;
		}

		power_mw = (pps_current_ma * pps_max_voltage_mv) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
				"Src_PDO_Supply_Type",
				"3", "PPS");
		sprintf(str, "%dmA", pps_current_ma);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Current],
				"Src_PDO_Max_Current",
				pps_current, str);
		sprintf(str, "%dmV", pps_min_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Min_Voltage],
				"Src_PDO_Min_Voltage",
				pps_min_voltage, str);
		sprintf(str, "%dmV", pps_max_voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Max_Voltage],
				"Src_PDO_Max_Voltage",
				pps_max_voltage, str);

	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power_mw;
}

static int gen_vif(const char *name,
		   const char *board,
		   const char *vif_producer)
{
	enum dtype type;
	struct vif_t vif;
	struct vif_field_t *vif_fields;

	int32_t src_max_power = 0;
	enum bc_1_2_support bc_support;


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

	/*********************************************************************
	 * VIF Fields
	 */
	vif_fields = vif.vif_field;

	set_vif_field(&vif_fields[VIF_Specification],
			"VIF_Specification",
			NULL,
			"3.12");

	set_vif_field(&vif_fields[VIF_App_Vendor],
			"Vendor",
			NULL,
			VIF_APP_VENDOR_VALUE);

	set_vif_field(&vif_fields[VIF_App_Name],
			"Name",
			NULL,
			VIF_APP_NAME_VALUE);

	set_vif_field(&vif_fields[VIF_App_Version],
			"Version",
			NULL,
			VIF_APP_VERSION_VALUE);

	set_vif_field(&vif_fields[Vendor_Name],
			"Vendor_Name",
			NULL,
			VENDOR_NAME_VALUE);

	#if defined(CONFIG_USB_PD_MODEL_PART_NUMBER)
		set_vif_field(&vif_fields[Model_Part_Number],
				"Model_Part_Number",
				NULL,
				CONFIG_USB_PD_MODEL_PART_NUMBER);
	#else
		if (board && strlen(board) > 0)
			set_vif_field(&vif_fields[Model_Part_Number],
					"Model_Part_Number",
					NULL,
					board);
		else
			set_vif_field(&vif_fields[Model_Part_Number],
					"Model_Part_Number",
					NULL,
					"FIX-ME");
	#endif

	#if defined(CONFIG_USB_PD_PRODUCT_REVISION)
		set_vif_field(&vif_fields[Product_Revision],
				"Product_Revision",
				NULL,
				CONFIG_USB_PD_PRODUCT_REVISION);
	#else
		set_vif_field(&vif_fields[Product_Revision],
				"Product_Revision",
				NULL,
				"FIX-ME");
	#endif

	#if defined(CONFIG_USB_PD_TID)
		set_vif_field_stis(&vif_fields[TID],
				"TID",
				NULL,
				CONFIG_USB_PD_TID);
	#else
		set_vif_field_stis(&vif_fields[TID],
				"TID",
				NULL,
				DEFAULT_MISSING_TID);
	#endif

	set_vif_field(&vif_fields[VIF_Product_Type],
			"VIF_Product_Type",
			"0",
			"Port Product");

	set_vif_field(&vif_fields[Certification_Type],
			"Certification_Type",
			"1",
			"Reference Platform");

	/*********************************************************************
	 * VIF/Product Fields
	 */
	vif_fields = vif.Product.vif_field;

	{
		char hex_str[10];

		sprintf(hex_str, "%04X", USB_VID_GOOGLE);
		set_vif_field_itss(&vif_fields[Product_VID],
				"Product_VID",
				USB_VID_GOOGLE, hex_str);
	}

	/*********************************************************************
	 * VIF/Component[] Fields
	 */
	vif_fields = vif.Component[0].vif_field;

	#if defined(CONFIG_USB_PD_PORT_LABEL)
		set_vif_field_stis(&vif_fields[Port_Label],
				"Port_Label",
				NULL,
				CONFIG_USB_PD_PORT_LABEL);
	#else
		set_vif_field(&vif_fields[Port_Label],
				"Port_Label",
				NULL,
				"0");
	#endif

	set_vif_field(&vif_fields[Connector_Type],
			"Connector_Type",
			"2",
			"USB Type-C");

	set_vif_field_b(&vif_fields[USB4_Supported],
			"USB4_Supported",
			IS_ENABLED(CONFIG_USB_PD_USB4));

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

	bc_support = BC_1_2_SUPPORT_NONE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_MAX14637))
		bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_MT6360))
		bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_PI3USB9201))
		bc_support = BC_1_2_SUPPORT_BOTH;
	if (IS_ENABLED(CONFIG_BC12_DETECT_PI3USB9281))
		bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;

	switch (bc_support) {
	case BC_1_2_SUPPORT_NONE:
		set_vif_field(&vif_fields[BC_1_2_Support],
				"BC_1_2_Support",
				"0",
				"None");
		break;
	case BC_1_2_SUPPORT_PORTABLE_DEVICE:
		set_vif_field(&vif_fields[BC_1_2_Support],
				"BC_1_2_Support",
				"1",
				"Portable Device");
		break;
	case BC_1_2_SUPPORT_CHARGING_PORT:
		set_vif_field(&vif_fields[BC_1_2_Support],
				"BC_1_2_Support",
				"2",
				"Charging Port");
		break;
	case BC_1_2_SUPPORT_BOTH:
		set_vif_field(&vif_fields[BC_1_2_Support],
				"BC_1_2_Support",
				"3",
				"Both");
		break;
	}

	/*********************************************************************
	 * General PD Fields
	 */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) || IS_ENABLED(CONFIG_USB_PRL_SM)) {
		set_vif_field(&vif_fields[PD_Spec_Revision_Major],
				"PD_Spec_Revision_Major",
				"3",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Revision_Minor],
				"PD_Spec_Revision_Minor",
				"0",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Major],
				"PD_Spec_Version_Major",
				"2",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Minor],
				"PD_Spec_Version_Minor",
				"0",
				NULL);

		set_vif_field(&vif_fields[PD_Specification_Revision],
				"PD_Specification_Revision",
				"2",
				"Revision 3.0");
	} else {
		set_vif_field(&vif_fields[PD_Spec_Revision_Major],
				"PD_Spec_Revision_Major",
				"2",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Revision_Minor],
				"PD_Spec_Revision_Minor",
				"0",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Major],
				"PD_Spec_Version_Major",
				"1",
				NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Minor],
				"PD_Spec_Version_Minor",
				"3",
				NULL);

		set_vif_field(&vif_fields[PD_Specification_Revision],
				"PD_Specification_Revision",
				"1",
				"Revision 2.0");
	}

	set_vif_field_b(&vif_fields[USB_Comms_Capable],
			"USB_Comms_Capable",
			(!(IS_ENABLED(CONFIG_USB_VPD) ||
			   IS_ENABLED(CONFIG_USB_CTVPD))));

	{
		bool supports_to_dfp = true;

		if (type == DRP || type == SNK)
			/*
			 * DR_Swap_To_DFP_Supported requires
			 *    Type_C_Can_Act_As_Host to be YES
			 */
			supports_to_dfp &= can_act_as_host();

		if (type == DRP)
			/*
			 * DR_Swap_To_DFP_Supported requires
			 *    Type_C_Can_Act_As_Device to be NO
			 */
			supports_to_dfp &= !can_act_as_device();
		else if (type == SRC)
			/*
			 * DR_Swap_To_DFP_Supported requires
			 *    Type_C_Can_Act_As_Device to be YES
			 */
			supports_to_dfp &= can_act_as_device();

		set_vif_field_b(&vif_fields[DR_Swap_To_DFP_Supported],
				"DR_Swap_To_DFP_Supported",
				supports_to_dfp);
	}

	{
		bool supports_to_ufp = true;

		if (type == DRP || type == SRC)
			/*
			 * DR_Swap_To_UFP_Supported requires
			 *    Type_C_Can_Act_As_Device to be YES
			 */
			supports_to_ufp &= can_act_as_device();

		if (type == DRP)
			/*
			 * DR_Swap_To_UFP_Supported requires
			 *    Type_C_Can_Act_As_Host to be NO
			 */
			supports_to_ufp &= !can_act_as_host();
		else if (type == SNK)
			/*
			 * DR_Swap_To_DFP_Supported requires
			 *    Type_C_Can_Act_As_Host to be YES
			 */
			supports_to_ufp &= can_act_as_host();

		set_vif_field_b(&vif_fields[DR_Swap_To_UFP_Supported],
				"DR_Swap_To_UFP_Supported",
				supports_to_ufp);
	}

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
			(IS_ENABLED(CONFIG_USB_PD_USB4) ||
			 IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)));

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

	{
		char hex_str[10];

		#if defined(CONFIG_USB_PID)
			sprintf(hex_str, "%04X", CONFIG_USB_PID);
			set_vif_field_itss(&vif_fields[
					Manufacturer_Info_PID_Port],
					"Manufacturer_Info_PID_Port",
					CONFIG_USB_PID, hex_str);
		#else
			sprintf(hex_str, "%04X", DEFAULT_MISSING_PID);
			set_vif_field_itss(&vif_fields[
					Manufacturer_Info_PID_Port],
					"Manufacturer_Info_PID_Port",
					DEFAULT_MISSING_PID, hex_str);
		#endif
	}

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

	set_vif_field(&vif_fields[ID_Header_Connector_Type_SOP],
			"ID_Header_Connector_Type_SOP",
			"2", "USB Type-C Receptacle");

	/*********************************************************************
	 * SOP* Capabilities
	 */
	set_vif_field_b(&vif_fields[SOP_Capable],
			"SOP_Capable",
			can_act_as_host());

	set_vif_field_b(&vif_fields[SOP_P_Capable],
			"SOP_P_Capable",
			IS_ENABLED(CONFIG_USB_PD_DECODE_SOP));

	set_vif_field_b(&vif_fields[SOP_PP_Capable],
			"SOP_PP_Capable",
			IS_ENABLED(CONFIG_USB_PD_DECODE_SOP));

	set_vif_field_b(&vif_fields[SOP_P_Debug_Capable],
			"SOP_P_Debug_Capable",
			false);

	set_vif_field_b(&vif_fields[SOP_PP_Debug_Capable],
			"SOP_PP_Debug_Capable",
			false);

	/*********************************************************************
	 * USB Type-C Fields
	 */
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

	if (type == SNK)
		set_vif_field_b(
			&vif_fields[Type_C_Supports_VCONN_Powered_Accessory],
				"Type_C_Supports_VCONN_Powered_Accessory",
				false);

	set_vif_field_b(&vif_fields[Type_C_Is_VCONN_Powered_Accessory],
			"Type_C_Is_VCONN_Powered_Accessory",
			false);

	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SRC],
			"Type_C_Is_Debug_Target_SRC",
			true);

	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SNK],
			"Type_C_Is_Debug_Target_SNK",
			true);

	set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Host],
			"Type_C_Can_Act_As_Host",
			can_act_as_host());

	set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Controller],
			"Type_C_Is_Alt_Mode_Controller",
			IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP));

	set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Device],
			"Type_C_Can_Act_As_Device",
			can_act_as_device());

	set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Adapter],
			"Type_C_Is_Alt_Mode_Adapter",
			IS_ENABLED(CONFIG_USB_ALT_MODE_ADAPTER));

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
					"1", "UFP-powered");
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

	/*********************************************************************
	 * USB Data - Upstream Facing Port Fields
	 */
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

	/*********************************************************************
	 * USB Data - Downstream Facing Port Fields
	 */
	{
		int ds = USB_2;

		switch (ds) {
		case USB_2:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					USB_2, "USB 2");
			break;
		case USB_GEN11:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					USB_GEN11, "USB 3.2 GEN 1x1");
			break;
		case USB_GEN21:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					USB_GEN21, "USB 3.2 GEN 2x1");
			break;
		case USB_GEN12:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					USB_GEN12, "USB 3.2 GEN 1x2");
			break;
		case USB_GEN22:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					USB_GEN22, "USB 3.2 GEN 2x2");
			break;
		default:
			set_vif_field_itss(&vif_fields[Host_Speed],
					"Host_Speed",
					ds, NULL);
		}
	}

	set_vif_field_b(&vif_fields[Host_Contains_Captive_Retimer],
			"Host_Contains_Captive_Retimer",
			false);

	set_vif_field_b(&vif_fields[Host_Is_Embedded],
			"Host_Is_Embedded",
			false);

	/*********************************************************************
	 * PD Source Fields
	 */
	if (type == DRP || type == SRC) {
		int i;

		/* Source PDOs */
		for (i = 0; i < src_pdo_cnt; i++) {
			int32_t pwr;

			pwr = set_vif_src_pdo(&vif.Component[0].SrcPdoList[i],
					      src_pdo[i]);
			if (pwr < 0) {
				fprintf(stderr, "ERROR: Setting SRC PDO.\n");
				return 1;
			}

			if (pwr > src_max_power)
				src_max_power = pwr;
		}

		set_vif_field_itss(&vif_fields[PD_Power_As_Source],
				"PD_Power_As_Source",
				src_max_power, NULL);
	}

	if (type == DRP || type == SRC)
		set_vif_field_b(&vif_fields[USB_Suspend_May_Be_Cleared],
				"USB_Suspend_May_Be_Cleared",
				false);

	if (type == DRP || type == SRC)
		set_vif_field_b(&vif_fields[Sends_Pings],
				"Sends_Pings",
				false);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    type == DRP &&
	    IS_ENABLED(CONFIG_USB_PD_FRS))
		set_vif_field(&vif_fields[
			FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			"FR_Swap_Type_C_Current_Capability_As_Initial_Sink",
			"3", "3A @ 5V");
	else
		set_vif_field(&vif_fields[
			FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			"FR_Swap_Type_C_Current_Capability_As_Initial_Sink",
			"0", "FR_Swap not supported");

	if (IS_ENABLED(CONFIG_USB_PD_REV30) || IS_ENABLED(CONFIG_USB_PRL_SM))
		set_vif_field_b(&vif_fields[Master_Port],
				"Master_Port",
				false);

	if (type == DRP || type == SRC)
		set_vif_field_itss(&vif_fields[Num_Src_PDOs],
				"Num_Src_PDOs",
				src_pdo_cnt, NULL);

	if (type == DRP || type == SRC) {
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

	/*********************************************************************
	 * PD Sink Fields
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK)) {
		int i;
		int32_t snk_max_power = 0;

		/* Sink PDOs */
		for (i = 0; i < pd_snk_pdo_cnt; i++) {
			int32_t pwr;

			pwr = set_vif_snk_pdo(&vif.Component[0].SnkPdoList[i],
					      pd_snk_pdo[i]);
			if (pwr < 0) {
				fprintf(stderr, "ERROR: Setting SNK PDO.\n");
				return 1;
			}

			if (pwr > snk_max_power)
				snk_max_power = pwr;
		}

		set_vif_field_itss(&vif_fields[PD_Power_As_Sink],
				"PD_Power_As_Sink",
				snk_max_power, NULL);
	}

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK))
		set_vif_field_b(&vif_fields[No_USB_Suspend_May_Be_Set],
				"No_USB_Suspend_May_Be_Set",
				true);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK))
		set_vif_field_b(&vif_fields[GiveBack_May_Be_Set],
				"GiveBack_May_Be_Set",
				IS_ENABLED(CONFIG_USB_PD_GIVE_BACK));

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK))
		set_vif_field_b(&vif_fields[Higher_Capability_Set],
				"Higher_Capability_Set",
				false);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK))
		set_vif_field(&vif_fields[
				FR_Swap_Reqd_Type_C_Current_As_Initial_Source],
				"FR_Swap_Reqd_Type_C_Current_As_Initial_Source",
				"0", "FR_Swap not supported");

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
	    (type == DRP || type == SNK))
		set_vif_field_itss(&vif_fields[Num_Snk_PDOs],
				"Num_Snk_PDOs",
				pd_snk_pdo_cnt, NULL);

	/*********************************************************************
	 * PD Dual Role Fields
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP)
		set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Src],
				"Accepts_PR_Swap_As_Src",
				true);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP)
		set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Snk],
				"Accepts_PR_Swap_As_Snk",
				true);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP)
		set_vif_field_b(&vif_fields[Requests_PR_Swap_As_Src],
				"Requests_PR_Swap_As_Src",
				true);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP)
		set_vif_field_b(&vif_fields[Requests_PR_Swap_As_Snk],
				"Requests_PR_Swap_As_Snk",
				true);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&  type == DRP)
		set_vif_field_b(&vif_fields[FR_Swap_Supported_As_Initial_Sink],
				"FR_Swap_Supported_As_Initial_Sink",
				IS_ENABLED(CONFIG_USB_PD_FRS));

	/*********************************************************************
	 * SOP Discovery Fields
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TCPMV2)) {
		char hex_str[10];

		#if defined(CONFIG_USB_PID)
			sprintf(hex_str, "%04X", CONFIG_USB_PID);
			set_vif_field_itss(&vif_fields[PID_SOP],
					"PID_SOP",
					CONFIG_USB_PID, hex_str);
		#else
			sprintf(hex_str, "%04X", DEFAULT_MISSING_PID);
			set_vif_field_itss(&vif_fields[PID_SOP],
					"PID_SOP",
					DEFAULT_MISSING_PID, hex_str);
		#endif
	}

	/*********************************************************************
	 * Battery Charging 1.2 Fields
	 */
	if (bc_support == BC_1_2_SUPPORT_CHARGING_PORT ||
	    bc_support == BC_1_2_SUPPORT_BOTH)
		set_vif_field(&vif_fields[BC_1_2_Charging_Port_Type],
				"BC_1_2_Charging_Port_Type",
				"1",
				"CDP");

	/*********************************************************************
	 * Product Power Fields
	 */
	if (type == DRP || type == SRC) {
		char str[10];

		sprintf(str, "%dmW", src_max_power);
		set_vif_field_itss(&vif_fields[Product_Total_Source_Power_mW],
				"Product_Total_Source_Power_mW",
				src_max_power, str);
	}

	if (type == DRP || type == SRC)
		set_vif_field(&vif_fields[Port_Source_Power_Type],
				"Port_Source_Power_Type",
				"0", "Assured");

	/*********************************************************************
	 * Format the structure in XML and output it to file
	 */
	return vif_output_xml(name, &vif);
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

	name_size = asprintf(&name, "%s/%s_vif.xml", out, board);
	if (name_size < 0) {
		fprintf(stderr, "ERROR: Out of memory.\n");
		return 1;
	}

	ret = gen_vif(name, board, vif_producer);

	free(name);

	return ret;
}
