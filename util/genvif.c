/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* for asprintf */

#include "charge_manager.h"
#include "config.h"
#include "genvif.h"
#include "usb_pd.h"
#include "usb_pd_pdo.h"
#include "usb_pd_tcpm.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <getopt.h>

#define VIF_APP_VENDOR_VALUE "Google"
#define VIF_APP_NAME_VALUE "EC GENVIF"
#define VIF_APP_VERSION_VALUE "3.2.3.0"
#define VENDOR_NAME_VALUE "Google"

#define DEFAULT_MISSING_TID 0xFFFF
#define DEFAULT_MISSING_PID 0xFFFF
#define DEFAULT_MISSING_BCD_DEV 0x0000

/*
 * XML namespace for VIF as of VifEditorRelease 3.2.3.0
 */
#define VIF_ "vif:"

const uint32_t *src_pdo;
uint32_t src_pdo_cnt;

struct vif_t vif;

/*
 * local type to make decisions on the output for Source, Sink and DRP
 */
enum dtype { SRC = 0, SNK, DRP };

enum ptype {
	PORT_CONSUMER_ONLY = 0,
	PORT_CONSUMER_PRODUCER = 1,
	PORT_PRODUCER_CONSUMER = 2,
	PORT_PROVIDER_ONLY = 3,
	PORT_DRP = 4,
	PORT_EMARKER = 5,
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

enum power_source {
	POWER_EXTERNAL = 0,
	POWER_UFP = 1,
	POWER_BOTH = 2,
};

static void set_vif_field_c(struct vif_field_t *vif_field, const char *comment);

/*
 * index of component being set
 */
int component_index;

/*
 * TAG Name Strings
 */
#define NAME_INIT(str) [str] = VIF_ #str

const char *vif_name[] = {
	NAME_INIT(VIF_Specification),
	NAME_INIT(Vendor_Name),
	NAME_INIT(Model_Part_Number),
	NAME_INIT(Product_Revision),
	NAME_INIT(TID),
	NAME_INIT(VIF_Product_Type),
	NAME_INIT(Certification_Type),
};
BUILD_ASSERT(ARRAY_SIZE(vif_name) == VIF_Indexes);

const char *vif_app_name[] = {
	NAME_INIT(Vendor),
	NAME_INIT(Name),
	NAME_INIT(Version),
};
BUILD_ASSERT(ARRAY_SIZE(vif_app_name) == VIF_App_Indexes);

const char *vif_component_name[] = {
	NAME_INIT(Port_Label),
	NAME_INIT(Connector_Type),
	NAME_INIT(USB4_Supported),
	NAME_INIT(USB_PD_Support),
	NAME_INIT(PD_Port_Type),
	NAME_INIT(Type_C_State_Machine),
	NAME_INIT(Port_Battery_Powered),
	NAME_INIT(BC_1_2_Support),
	NAME_INIT(PD_Spec_Revision_Major),
	NAME_INIT(PD_Spec_Revision_Minor),
	NAME_INIT(PD_Spec_Version_Major),
	NAME_INIT(PD_Spec_Version_Minor),
	NAME_INIT(PD_Specification_Revision),
	NAME_INIT(SOP_Capable),
	NAME_INIT(SOP_P_Capable),
	NAME_INIT(SOP_PP_Capable),
	NAME_INIT(SOP_P_Debug_Capable),
	NAME_INIT(SOP_PP_Debug_Capable),
	NAME_INIT(Manufacturer_Info_Supported_Port),
	NAME_INIT(Manufacturer_Info_VID_Port),
	NAME_INIT(Manufacturer_Info_PID_Port),
	NAME_INIT(USB_Comms_Capable),
	NAME_INIT(DR_Swap_To_DFP_Supported),
	NAME_INIT(DR_Swap_To_UFP_Supported),
	NAME_INIT(Unconstrained_Power),
	NAME_INIT(VCONN_Swap_To_On_Supported),
	NAME_INIT(VCONN_Swap_To_Off_Supported),
	NAME_INIT(Responds_To_Discov_SOP_UFP),
	NAME_INIT(Responds_To_Discov_SOP_DFP),
	NAME_INIT(Attempts_Discov_SOP),
	NAME_INIT(Power_Interruption_Available),
	NAME_INIT(Data_Reset_Supported),
	NAME_INIT(Enter_USB_Supported),
	NAME_INIT(Chunking_Implemented_SOP),
	NAME_INIT(Unchunked_Extended_Messages_Supported),
	NAME_INIT(Security_Msgs_Supported_SOP),
	NAME_INIT(Num_Fixed_Batteries),
	NAME_INIT(Num_Swappable_Battery_Slots),
	NAME_INIT(ID_Header_Connector_Type_SOP),
	NAME_INIT(Type_C_Can_Act_As_Host),
	NAME_INIT(Type_C_Can_Act_As_Device),
	NAME_INIT(Type_C_Implements_Try_SRC),
	NAME_INIT(Type_C_Implements_Try_SNK),
	NAME_INIT(Type_C_Supports_Audio_Accessory),
	NAME_INIT(Type_C_Supports_VCONN_Powered_Accessory),
	NAME_INIT(Type_C_Is_VCONN_Powered_Accessory),
	NAME_INIT(Type_C_Is_Debug_Target_SRC),
	NAME_INIT(Type_C_Is_Debug_Target_SNK),
	NAME_INIT(Captive_Cable),
	NAME_INIT(Captive_Cable_Is_eMarked),
	NAME_INIT(RP_Value),
	NAME_INIT(Type_C_Port_On_Hub),
	NAME_INIT(Type_C_Power_Source),
	NAME_INIT(Type_C_Sources_VCONN),
	NAME_INIT(Type_C_Is_Alt_Mode_Controller),
	NAME_INIT(Type_C_Is_Alt_Mode_Adapter),
	NAME_INIT(USB4_Router_Index),
	NAME_INIT(USB4_Lane_0_Adapter),
	NAME_INIT(USB4_Max_Speed),
	NAME_INIT(USB4_DFP_Supported),
	NAME_INIT(USB4_UFP_Supported),
	NAME_INIT(USB4_USB3_Tunneling_Supported),
	NAME_INIT(USB4_DP_Tunneling_Supported),
	NAME_INIT(USB4_PCIe_Tunneling_Supported),
	NAME_INIT(USB4_TBT3_Compatibility_Supported),
	NAME_INIT(USB4_CL1_State_Supported),
	NAME_INIT(USB4_CL2_State_Supported),
	NAME_INIT(USB4_Num_Retimers),
	NAME_INIT(USB4_DP_Bit_Rate),
	NAME_INIT(USB4_Num_DP_Lanes),
	NAME_INIT(Host_Supports_USB_Data),
	NAME_INIT(Host_Speed),
	NAME_INIT(Host_Contains_Captive_Retimer),
	NAME_INIT(Host_Truncates_DP_For_tDHPResponse),
	NAME_INIT(Host_Gen1x1_tLinkTurnaround),
	NAME_INIT(Host_Gen2x1_tLinkTurnaround),
	NAME_INIT(Host_Is_Embedded),
	NAME_INIT(Host_Suspend_Supported),
	NAME_INIT(Is_DFP_On_Hub),
	NAME_INIT(Hub_Port_Number),
	NAME_INIT(Device_Supports_USB_Data),
	NAME_INIT(Device_Speed),
	NAME_INIT(Device_Contains_Captive_Retimer),
	NAME_INIT(Device_Truncates_DP_For_tDHPResponse),
	NAME_INIT(Device_Gen1x1_tLinkTurnaround),
	NAME_INIT(Device_Gen2x1_tLinkTurnaround),
	NAME_INIT(BC_1_2_Charging_Port_Type),
	NAME_INIT(PD_Power_As_Source),
	NAME_INIT(EPR_Supported_As_Src),
	NAME_INIT(USB_Suspend_May_Be_Cleared),
	NAME_INIT(Sends_Pings),
	NAME_INIT(Accepts_PR_Swap_As_Src),
	NAME_INIT(Accepts_PR_Swap_As_Snk),
	NAME_INIT(Requests_PR_Swap_As_Src),
	NAME_INIT(Requests_PR_Swap_As_Snk),
	NAME_INIT(FR_Swap_Supported_As_Initial_Sink),
	NAME_INIT(FR_Swap_Type_C_Current_Capability_As_Initial_Sink),
	NAME_INIT(FR_Swap_Reqd_Type_C_Current_As_Initial_Source),
	NAME_INIT(Master_Port),
	NAME_INIT(Num_Src_PDOs),
	NAME_INIT(PD_OC_Protection),
	NAME_INIT(PD_OCP_Method),
	NAME_INIT(PD_Power_As_Sink),
	NAME_INIT(EPR_Supported_As_Snk),
	NAME_INIT(No_USB_Suspend_May_Be_Set),
	NAME_INIT(GiveBack_May_Be_Set),
	NAME_INIT(Higher_Capability_Set),
	NAME_INIT(Num_Snk_PDOs),
	NAME_INIT(XID_SOP),
	NAME_INIT(Data_Capable_As_USB_Host_SOP),
	NAME_INIT(Data_Capable_As_USB_Device_SOP),
	NAME_INIT(Product_Type_UFP_SOP),
	NAME_INIT(Product_Type_DFP_SOP),
	NAME_INIT(DFP_VDO_Port_Number),
	NAME_INIT(Modal_Operation_Supported_SOP),
	NAME_INIT(USB_VID_SOP),
	NAME_INIT(PID_SOP),
	NAME_INIT(bcdDevice_SOP),
	NAME_INIT(SVID_Fixed_SOP),
	NAME_INIT(Num_SVIDs_Min_SOP),
	NAME_INIT(Num_SVIDs_Max_SOP),
	NAME_INIT(AMA_HW_Vers),
	NAME_INIT(AMA_FW_Vers),
	NAME_INIT(AMA_VCONN_Reqd),
	NAME_INIT(AMA_VCONN_Power),
	NAME_INIT(AMA_VBUS_Reqd),
	NAME_INIT(AMA_Superspeed_Support),
	NAME_INIT(Product_Total_Source_Power_mW),
	NAME_INIT(Port_Source_Power_Type),
	NAME_INIT(Port_Source_Power_Gang),
	NAME_INIT(Port_Source_Power_Gang_Max_Power),
	NAME_INIT(XID),
	NAME_INIT(Data_Capable_As_USB_Host),
	NAME_INIT(Data_Capable_As_USB_Device),
	NAME_INIT(Product_Type),
	NAME_INIT(Modal_Operation_Supported),
	NAME_INIT(USB_VID),
	NAME_INIT(PID),
	NAME_INIT(bcdDevice),
	NAME_INIT(Cable_HW_Vers),
	NAME_INIT(Cable_FW_Vers),
	NAME_INIT(Type_C_To_Type_A_B_C),
	NAME_INIT(Type_C_To_Type_C_Capt_Vdm_V2),
	NAME_INIT(EPR_Mode_Capable),
	NAME_INIT(Cable_Latency),
	NAME_INIT(Cable_Termination_Type),
	NAME_INIT(VBUS_Through_Cable),
	NAME_INIT(Cable_VBUS_Current),
	NAME_INIT(Cable_Superspeed_Support),
	NAME_INIT(Cable_USB_Highest_Speed),
	NAME_INIT(Max_VBUS_Voltage_Vdm_V2),
	NAME_INIT(Manufacturer_Info_Supported),
	NAME_INIT(Manufacturer_Info_VID),
	NAME_INIT(Manufacturer_Info_PID),
	NAME_INIT(Chunking_Implemented),
	NAME_INIT(Security_Msgs_Supported),
	NAME_INIT(ID_Header_Connector_Type),
	NAME_INIT(SVID_Fixed),
	NAME_INIT(Cable_Num_SVIDs_Min),
	NAME_INIT(Cable_Num_SVIDs_Max),
	NAME_INIT(VPD_HW_Vers),
	NAME_INIT(VPD_FW_Vers),
	NAME_INIT(VPD_Max_VBUS_Voltage),
	NAME_INIT(VPD_Charge_Through_Support),
	NAME_INIT(VPD_Charge_Through_Current),
	NAME_INIT(VPD_VBUS_Impedance),
	NAME_INIT(VPD_Ground_Impedance),
	NAME_INIT(Cable_SOP_PP_Controller),
	NAME_INIT(SBU_Supported),
	NAME_INIT(SBU_Type),
	NAME_INIT(Active_Cable_Max_Operating_Temp),
	NAME_INIT(Active_Cable_Shutdown_Temp),
	NAME_INIT(Active_Cable_U3_CLd_Power),
	NAME_INIT(Active_Cable_U3_U0_Trans_Mode),
	NAME_INIT(Active_Cable_Physical_Connection),
	NAME_INIT(Active_Cable_Active_Element),
	NAME_INIT(Active_Cable_USB4_Support),
	NAME_INIT(Active_Cable_USB2_Hub_Hops_Consumed),
	NAME_INIT(Active_Cable_USB2_Supported),
	NAME_INIT(Active_Cable_USB32_Supported),
	NAME_INIT(Active_Cable_USB_Lanes),
	NAME_INIT(Active_Cable_Optically_Isolated),
	NAME_INIT(Active_Cable_USB_Gen),
	NAME_INIT(Repeater_One_Type),
	NAME_INIT(Repeater_Two_Type),
};
BUILD_ASSERT(ARRAY_SIZE(vif_component_name) == Component_Indexes);

const char *vif_component_snk_pdo_name[] = {
	NAME_INIT(Snk_PDO_Supply_Type), NAME_INIT(Snk_PDO_APDO_Type),
	NAME_INIT(Snk_PDO_Voltage),	NAME_INIT(Snk_PDO_PDP_Rating),
	NAME_INIT(Snk_PDO_Op_Power),	NAME_INIT(Snk_PDO_Min_Voltage),
	NAME_INIT(Snk_PDO_Max_Voltage), NAME_INIT(Snk_PDO_Op_Current),
};
BUILD_ASSERT(ARRAY_SIZE(vif_component_snk_pdo_name) == Snk_PDO_Indexes);

const char *vif_component_src_pdo_name[] = {
	NAME_INIT(Src_PDO_Supply_Type),
	NAME_INIT(Src_PDO_APDO_Type),
	NAME_INIT(Src_PDO_Peak_Current),
	NAME_INIT(Src_PDO_Voltage),
	NAME_INIT(Src_PDO_Max_Current),
	NAME_INIT(Src_PDO_Min_Voltage),
	NAME_INIT(Src_PDO_Max_Voltage),
	NAME_INIT(Src_PDO_Max_Power),
	NAME_INIT(Src_PD_OCP_OC_Debounce),
	NAME_INIT(Src_PD_OCP_OC_Threshold),
	NAME_INIT(Src_PD_OCP_UV_Debounce),
	NAME_INIT(Src_PD_OCP_UV_Threshold_Type),
	NAME_INIT(Src_PD_OCP_UV_Threshold),
};
BUILD_ASSERT(ARRAY_SIZE(vif_component_src_pdo_name) == Src_PDO_Indexes);

const char *vif_component_sop_svid_mode_name[] = {
	NAME_INIT(SVID_Mode_Enter_SOP),
	NAME_INIT(SVID_Mode_Recog_Mask_SOP),
	NAME_INIT(SVID_Mode_Recog_Value_SOP),
};
BUILD_ASSERT(ARRAY_SIZE(vif_component_sop_svid_mode_name) ==
	     SopSVID_Mode_Indexes);

const char *vif_component_sop_svid_name[] = {
	NAME_INIT(SVID_SOP),
	NAME_INIT(SVID_Modes_Fixed_SOP),
	NAME_INIT(SVID_Num_Modes_Min_SOP),
	NAME_INIT(SVID_Num_Modes_Max_SOP),
};
BUILD_ASSERT(ARRAY_SIZE(vif_component_sop_svid_name) == SopSVID_Indexes);

const char *vif_cable_mode_name[] = {
	NAME_INIT(SVID_Mode_Enter),
	NAME_INIT(SVID_Mode_Recog_Mask),
	NAME_INIT(SVID_Mode_Recog_Value),
};
BUILD_ASSERT(ARRAY_SIZE(vif_cable_mode_name) == CableSVID_Mode_Indexes);

const char *vif_cable_svid_name[] = {
	NAME_INIT(SVID),
	NAME_INIT(SVID_Modes_Fixed),
	NAME_INIT(SVID_Num_Modes_Min),
	NAME_INIT(SVID_Num_Modes_Max),
};
BUILD_ASSERT(ARRAY_SIZE(vif_cable_svid_name) == CableSVID_Indexes);

const char *vif_product_name[] = {
	NAME_INIT(USB4_DROM_Vendor_ID),
	NAME_INIT(USB4_Dock),
	NAME_INIT(USB4_Num_Internal_Host_Controllers),
	NAME_INIT(USB4_Num_PCIe_DN_Bridges),
	NAME_INIT(USB4_Device_HiFi_Bi_TMU_Mode_Required),
	NAME_INIT(USB4_Audio_Supported),
	NAME_INIT(USB4_HID_Supported),
	NAME_INIT(USB4_Printer_Supported),
	NAME_INIT(USB4_Mass_Storage_Supported),
	NAME_INIT(USB4_Video_Supported),
	NAME_INIT(USB4_Comms_Networking_Supported),
	NAME_INIT(USB4_Media_Transfer_Protocol_Supported),
	NAME_INIT(USB4_Smart_Card_Supported),
	NAME_INIT(USB4_Still_Image_Capture_Supported),
	NAME_INIT(USB4_Monitor_Device_Supported),
};
BUILD_ASSERT(ARRAY_SIZE(vif_product_name) == Product_Indexes);

const char *vif_product_pcie_endpoint_name[] = {
	NAME_INIT(USB4_PCIe_Endpoint_Vendor_ID),
	NAME_INIT(USB4_PCIe_Endpoint_Device_ID),
	NAME_INIT(USB4_PCIe_Endpoint_Class_Code),
};
BUILD_ASSERT(ARRAY_SIZE(vif_product_pcie_endpoint_name) ==
	     PCIe_Endpoint_Indexes);

const char *vif_product_usb4_router_name[] = {
	NAME_INIT(USB4_Router_ID),
	NAME_INIT(USB4_Silicon_VID),
	NAME_INIT(USB4_Num_Lane_Adapters),
	NAME_INIT(USB4_Num_USB3_DN_Adapters),
	NAME_INIT(USB4_Num_DP_IN_Adapters),
	NAME_INIT(USB4_Num_DP_OUT_Adapters),
	NAME_INIT(USB4_Num_PCIe_DN_Adapters),
	NAME_INIT(USB4_TBT3_Not_Supported),
	NAME_INIT(USB4_PCIe_Wake_Supported),
	NAME_INIT(USB4_USB3_Wake_Supported),
	NAME_INIT(USB4_Num_Unused_Adapters),
	NAME_INIT(USB4_TBT3_VID),
	NAME_INIT(USB4_PCIe_Switch_Vendor_ID),
	NAME_INIT(USB4_PCIe_Switch_Device_ID),
	NAME_INIT(USB4_Num_PCIe_Endpoints),
};
BUILD_ASSERT(ARRAY_SIZE(vif_product_usb4_router_name) == USB4_Router_Indexes);

static bool streq(const char *str1, const char *str2)
{
	if (str1 == NULL && str2 == NULL)
		return 1;
	if (str1 == NULL || str2 == NULL)
		return 0;
	return strcasecmp(str1, str2) == 0;
}

static bool is_start_tag(const char *xmlstr, const char *viftag)
{
	const char *xmltag;

	if (strncasecmp(xmlstr, VIF_, sizeof(VIF_) - 1) != 0)
		return 0;

	xmltag = xmlstr + sizeof(VIF_) - 1;
	return strcasecmp(xmltag, viftag) == 0;
}

static bool is_end_tag(const char *xmlstr, const char *viftag)
{
	const char *xmltag;

	if (xmlstr[0] != '/')
		return 0;

	if (strncasecmp(xmlstr + 1, VIF_, sizeof(VIF_) - 1) != 0)
		return 0;

	xmltag = xmlstr + 1 + sizeof(VIF_) - 1;
	return strcasecmp(xmltag, viftag) == 0;
}

/*****************************************************************************
 * VIF Structure Override Value Retrieve Functions
 */
/** Number **/
static bool get_vif_field_tag_number(struct vif_field_t *vif_field, int *value)
{
	if (vif_field->tag_value == NULL)
		return false;

	*value = atoi(vif_field->tag_value);
	return true;
}
static bool get_vif_field_str_number(struct vif_field_t *vif_field, int *value)
{
	if (vif_field->str_value == NULL)
		return false;

	*value = atoi(vif_field->str_value);
	return true;
}
static bool get_vif_field_number(struct vif_field_t *vif_field, int *value)
{
	bool rv;

	rv = get_vif_field_tag_number(vif_field, value);
	if (!rv)
		rv = get_vif_field_str_number(vif_field, value);

	return rv;
}
__maybe_unused static int get_vif_number(struct vif_field_t *vif_field,
					 int default_value)
{
	int ret_value;

	if (!get_vif_field_number(vif_field, &ret_value))
		ret_value = default_value;

	return ret_value;
}

/** Boolean **/
static bool get_vif_field_tag_bool(struct vif_field_t *vif_field, bool *value)
{
	if (vif_field->tag_value == NULL)
		return false;

	*value = streq(vif_field->tag_value, "true");
	return true;
}
static bool get_vif_field_str_bool(struct vif_field_t *vif_field, bool *value)
{
	if (vif_field->str_value == NULL)
		return false;

	*value = streq(vif_field->str_value, "YES");
	return true;
}
static bool get_vif_field_bool(struct vif_field_t *vif_field, bool *value)
{
	bool rv;

	rv = get_vif_field_tag_bool(vif_field, value);
	if (!rv)
		rv = get_vif_field_str_bool(vif_field, value);

	return rv;
}
static bool get_vif_bool(struct vif_field_t *vif_field, bool default_value)
{
	bool ret_value;

	if (!get_vif_field_bool(vif_field, &ret_value))
		ret_value = default_value;

	return ret_value;
}

/** String **/
__maybe_unused static bool get_vif_field_tag_str(struct vif_field_t *vif_field,
						 char **value)
{
	if (vif_field->tag_value == NULL)
		return false;

	*value = vif_field->tag_value;
	return true;
}
__maybe_unused static bool get_vif_field_str_str(struct vif_field_t *vif_field,
						 char **value)
{
	if (vif_field->str_value == NULL)
		return false;

	*value = vif_field->str_value;
	return true;
}
/*
 * VIF Structure Override Value Retrieve Functions
 *****************************************************************************/

/*****************************************************************************
 * Generic Helper Functions
 */
static bool is_src(void)
{
	int override_value;
	bool was_overridden;

	/* Determine if we are DRP, SRC or SNK */
	was_overridden = get_vif_field_tag_number(
		&vif.Component[component_index].vif_field[Type_C_State_Machine],
		&override_value);
	if (was_overridden) {
		switch (override_value) {
		case SRC:
		case DRP:
			return true;
		case SNK:
			return false;
		default:
			was_overridden = false;
			break;
		}
	}
	if (!was_overridden) {
		was_overridden = get_vif_field_tag_number(
			&vif.Component[component_index].vif_field[PD_Port_Type],
			&override_value);
		if (was_overridden) {
			switch (override_value) {
			case PORT_PROVIDER_ONLY: /* SRC */
			case PORT_DRP: /* DRP */
				return true;
			case PORT_CONSUMER_ONLY: /* SNK */
				return false;
			default:
				was_overridden = false;
			}
		}
	}
	return src_pdo_cnt;
}
static bool is_snk(void)
{
	int override_value;
	bool was_overridden;

	/* Determine if we are DRP, SRC or SNK */
	was_overridden = get_vif_field_tag_number(
		&vif.Component[component_index].vif_field[Type_C_State_Machine],
		&override_value);
	if (was_overridden) {
		switch (override_value) {
		case SNK:
		case DRP:
			return true;
		case SRC:
			return false;
		default:
			was_overridden = false;
			break;
		}
	}
	if (!was_overridden) {
		was_overridden = get_vif_field_tag_number(
			&vif.Component[component_index].vif_field[PD_Port_Type],
			&override_value);
		if (was_overridden) {
			switch (override_value) {
			case PORT_CONSUMER_ONLY: /* SNK */
			case PORT_DRP: /* DRP */
				return true;
			case PORT_PROVIDER_ONLY: /* SRC */
				return false;
			default:
				was_overridden = false;
			}
		}
	}
	return (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE)) ? pd_snk_pdo_cnt : 0;
}
static bool is_drp(void)
{
	int override_value;
	bool was_overridden;

	/* Determine if we are DRP, SRC or SNK */
	was_overridden = get_vif_field_tag_number(
		&vif.Component[component_index].vif_field[Type_C_State_Machine],
		&override_value);
	if (was_overridden) {
		switch (override_value) {
		case DRP:
			return true;
		case SNK:
			return false;
		case SRC:
		default:
			was_overridden = false;
			break;
		}
	}
	if (!was_overridden) {
		was_overridden = get_vif_field_tag_number(
			&vif.Component[component_index].vif_field[PD_Port_Type],
			&override_value);
		if (was_overridden) {
			switch (override_value) {
			case PORT_DRP: /* DRP */
				return true;
			case PORT_CONSUMER_ONLY: /* SNK */
				return false;
			case PORT_PROVIDER_ONLY: /* SRC */
			default:
				was_overridden = false;
			}
		}
	}
	if (is_src())
		return !!(src_pdo[0] & PDO_FIXED_DUAL_ROLE);
	return false;
}

static bool can_act_as_device(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Type_C_Can_Act_As_Device],
#if defined(USB_DEV_CLASS) && defined(USB_CLASS_BILLBOARD)
			    USB_DEV_CLASS == USB_CLASS_BILLBOARD
#else
			    false
#endif
	);
}

static bool can_act_as_host(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Type_C_Can_Act_As_Host],
			    (!(IS_ENABLED(CONFIG_USB_CTVPD) ||
			       IS_ENABLED(CONFIG_USB_VPD))));
}

static bool is_usb4_supported(void)
{
	return get_vif_bool(
		&vif.Component[component_index].vif_field[USB4_Supported],
		IS_ENABLED(CONFIG_USB_PD_USB4));
}

static bool is_usb4_tbt3_compatible(void)
{
	return get_vif_bool(
		&vif.Component[component_index]
			 .vif_field[USB4_TBT3_Compatibility_Supported],
		IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE));
}

static bool is_usb4_pcie_tunneling_supported(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[USB4_PCIe_Tunneling_Supported],
			    IS_ENABLED(CONFIG_USB_PD_PCIE_TUNNELING));
}

static bool is_usb_pd_supported(void)
{
	return get_vif_bool(
		&vif.Component[component_index].vif_field[USB_PD_Support],
		(is_usb4_supported() || IS_ENABLED(CONFIG_USB_PRL_SM) ||
		 IS_ENABLED(CONFIG_USB_POWER_DELIVERY)));
}

static bool is_usb_comms_capable(void)
{
	return get_vif_bool(
		&vif.Component[component_index].vif_field[USB_Comms_Capable],
		is_usb4_supported() || (!(IS_ENABLED(CONFIG_USB_VPD) ||
					  IS_ENABLED(CONFIG_USB_CTVPD))));
}

static bool is_alt_mode_controller(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Type_C_Is_Alt_Mode_Controller],
			    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP));
}

static bool is_alt_mode_adapter(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Type_C_Is_Alt_Mode_Adapter],
			    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP));
}

static bool does_respond_to_discov_sop_ufp(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Responds_To_Discov_SOP_UFP],
			    (is_usb4_supported() ||
			     IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)));
}

static bool does_respond_to_discov_sop_dfp(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Responds_To_Discov_SOP_DFP],
			    (is_usb4_supported() ||
			     IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE)));
}

static bool does_support_device_usb_data(void)
{
	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Device_Supports_USB_Data],
			    (is_usb4_supported() || can_act_as_device()));
}

static bool does_support_host_usb_data(void)
{
	int type_c_state_machine;

	if (!get_vif_field_tag_number(&vif.Component[component_index]
					       .vif_field[Type_C_State_Machine],
				      &type_c_state_machine))
		return false;

	return get_vif_bool(&vif.Component[component_index]
				     .vif_field[Host_Supports_USB_Data],
			    can_act_as_host());
}

static int vif_get_max_tbt_speed(void)
{
	struct vif_Component_t *c;

	c = &vif.Component[component_index];
	return get_vif_number(&c->vif_field[USB4_Max_Speed], -1);
}

static void init_src_pdos(void)
{
	if (IS_ENABLED(CONFIG_USB_PD_DYNAMIC_SRC_CAP)) {
		src_pdo_cnt = charge_manager_get_source_pdo(&src_pdo, 0);
	} else {
		if (IS_ENABLED(CONFIG_USB_PD_CUSTOM_PDO)) {
			src_pdo_cnt = pd_src_pdo_cnt;
			src_pdo = pd_src_pdo;
		} else {
			src_pdo_cnt = pd_src_pdo_max_cnt;
			src_pdo = pd_src_pdo_max;
		}
	}
}

static bool vif_fields_present(const struct vif_field_t *vif_fields, int count)
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
/*
 * Generic Helper Functions
 *****************************************************************************/

/*****************************************************************************
 * VIF XML Output Functions
 */
static void vif_out_str(FILE *vif_file, int level, const char *str)
{
	while (level-- > 0)
		fprintf(vif_file, "  ");
	fprintf(vif_file, "%s\r\n", str);
}

static void vif_out_start(FILE *vif_file, int level, const char *str)
{
	while (level-- > 0)
		fprintf(vif_file, "  ");

	fprintf(vif_file, "<" VIF_ "%s>\r\n", str);
}

static void vif_out_end(FILE *vif_file, int level, const char *str)
{
	while (level-- > 0)
		fprintf(vif_file, "  ");

	fprintf(vif_file, "</" VIF_ "%s>\r\n", str);
}

static void vif_out_comment(FILE *vif_file, int level, const char *fmt, ...)
{
	va_list args;

	while (level-- > 0)
		fprintf(vif_file, "  ");

	fprintf(vif_file, "<!--");

	va_start(args, fmt);
	vfprintf(vif_file, fmt, args);
	va_end(args);

	fprintf(vif_file, "-->\r\n");
}

static const char vif_separator[] = ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;"
				    ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;";

static void vif_out_field(FILE *vif_file, int level,
			  const struct vif_field_t *vif_field)
{
	if (vif_field->name == NULL && vif_field->tag_value) {
		int indent;

		vif_out_comment(vif_file, level, vif_separator);
		for (indent = level; indent-- > 0;)
			fprintf(vif_file, "  ");
		fprintf(vif_file, "<!--;%s-->\r\n", vif_field->tag_value);
		vif_out_comment(vif_file, level, vif_separator);
		return;
	}

	if (vif_field->str_value || vif_field->tag_value) {
		while (level-- > 0)
			fprintf(vif_file, "  ");

		fprintf(vif_file, "<%s", vif_field->name);
		if (vif_field->tag_value)
			fprintf(vif_file, " value=\"%s\"",
				vif_field->tag_value);
		if (vif_field->str_value)
			fprintf(vif_file, ">%s</%s>\r\n", vif_field->str_value,
				vif_field->name);
		else
			fprintf(vif_file, " />\r\n");
	}
}

static void vif_out_fields_range(FILE *vif_file, int level,
				 const struct vif_field_t *vif_fields,
				 int start, int count)
{
	int index;

	for (index = start; index < count; ++index)
		vif_out_field(vif_file, level, &vif_fields[index]);
}

static void vif_out_fields(FILE *vif_file, int level,
			   const struct vif_field_t *vif_fields, int count)
{
	vif_out_fields_range(vif_file, level, vif_fields, 0, count);
}

static void vif_output_vif_component_cable_svid_mode_list(
	FILE *vif_file, const struct vif_cableSVIDList_t *svid_list, int level)
{
	int index;

	if (!vif_fields_present(svid_list->CableSVIDModeList[0].vif_field,
				CableSVID_Mode_Indexes))
		return;

	vif_out_start(vif_file, level++, "CableSVIDModeList");
	for (index = 0; index < MAX_NUM_CABLE_SVID_MODES; ++index) {
		const struct vif_cableSVIDModeList_t *mode_list =
			&svid_list->CableSVIDModeList[index];

		if (!vif_fields_present(mode_list->vif_field,
					CableSVID_Mode_Indexes))
			break;

		vif_out_start(vif_file, level++, "SOPSVIDMode");
		vif_out_fields(vif_file, level, mode_list->vif_field,
			       CableSVID_Mode_Indexes);
		vif_out_end(vif_file, --level, "SOPSVIDMode");
	}
	vif_out_end(vif_file, --level, "CableSVIDModeList");
}

static void vif_output_vif_component_cable_svid_list(
	FILE *vif_file, const struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->CableSVIDList[0].vif_field,
				CableSVID_Indexes))
		return;

	vif_out_start(vif_file, level++, "CableSVIDList");
	for (index = 0; index < MAX_NUM_CABLE_SVIDS; ++index) {
		const struct vif_cableSVIDList_t *svid_list =
			&component->CableSVIDList[index];

		if (!vif_fields_present(svid_list->vif_field,
					CableSVID_Indexes))
			break;

		vif_out_start(vif_file, level++, "CableSVID");
		vif_out_fields(vif_file, level, svid_list->vif_field,
			       CableSVID_Indexes);
		vif_output_vif_component_cable_svid_mode_list(vif_file,
							      svid_list, level);
		vif_out_end(vif_file, --level, "CableSVID");
	}
	vif_out_end(vif_file, --level, "CableSVIDList");
}

static void vif_output_vif_component_sop_svid_mode_list(
	FILE *vif_file, const struct vif_sopSVIDList_t *svid_list, int level)
{
	int index;

	if (!vif_fields_present(svid_list->SOPSVIDModeList[0].vif_field,
				SopSVID_Mode_Indexes))
		return;

	vif_out_start(vif_file, level++, "SOPSVIDModeList");
	for (index = 0; index < MAX_NUM_SOP_SVID_MODES; ++index) {
		const struct vif_sopSVIDModeList_t *mode_list =
			&svid_list->SOPSVIDModeList[index];

		if (!vif_fields_present(mode_list->vif_field,
					SopSVID_Mode_Indexes))
			break;

		vif_out_start(vif_file, level++, "SOPSVIDMode");
		vif_out_fields(vif_file, level, mode_list->vif_field,
			       SopSVID_Mode_Indexes);
		vif_out_end(vif_file, --level, "SOPSVIDMode");
	}
	vif_out_end(vif_file, --level, "SOPSVIDModeList");
}

static void vif_output_vif_component_sop_svid_list(
	FILE *vif_file, const struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SOPSVIDList[0].vif_field,
				SopSVID_Indexes))
		return;

	vif_out_start(vif_file, level++, "SOPSVIDList");
	for (index = 0; index < MAX_NUM_SOP_SVIDS; ++index) {
		const struct vif_sopSVIDList_t *svid_list =
			&component->SOPSVIDList[index];

		if (!vif_fields_present(svid_list->vif_field, SopSVID_Indexes))
			break;

		vif_out_start(vif_file, level++, "SOPSVID");
		vif_out_fields(vif_file, level, svid_list->vif_field,
			       SopSVID_Indexes);
		vif_output_vif_component_sop_svid_mode_list(vif_file, svid_list,
							    level);
		vif_out_end(vif_file, --level, "SOPSVID");
	}
	vif_out_end(vif_file, --level, "SOPSVIDList");
}

static void vif_output_vif_component_snk_pdo_list(
	FILE *vif_file, const struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SnkPdoList[0].vif_field,
				Snk_PDO_Indexes))
		return;

	vif_out_comment(vif_file, level, "Bundle: SnkPdoList");
	vif_out_start(vif_file, level++, "SnkPdoList");
	for (index = 0; index < MAX_NUM_SNK_PDOS; ++index) {
		const struct vif_snkPdoList_t *pdo_list =
			&component->SnkPdoList[index];

		if (!vif_fields_present(pdo_list->vif_field, Snk_PDO_Indexes))
			break;

		vif_out_start(vif_file, level++, "SnkPDO");
		vif_out_comment(vif_file, level, "Sink PDO %d", index + 1);
		vif_out_fields(vif_file, level, pdo_list->vif_field,
			       Snk_PDO_Indexes);
		vif_out_end(vif_file, --level, "SnkPDO");
	}
	vif_out_end(vif_file, --level, "SnkPdoList");
}

static void vif_output_vif_component_src_pdo_list(
	FILE *vif_file, const struct vif_Component_t *component, int level)
{
	int index;

	if (!vif_fields_present(component->SrcPdoList[0].vif_field,
				Src_PDO_Indexes))
		return;

	vif_out_comment(vif_file, level, "Bundle: SrcPdoList");
	vif_out_start(vif_file, level++, "SrcPdoList");
	for (index = 0; index < MAX_NUM_SRC_PDOS; ++index) {
		const struct vif_srcPdoList_t *pdo_list =
			&component->SrcPdoList[index];

		if (!vif_fields_present(pdo_list->vif_field, Src_PDO_Indexes))
			break;

		vif_out_start(vif_file, level++, "SrcPDO");
		vif_out_comment(vif_file, level, "Source PDO %d", index + 1);
		vif_out_fields(vif_file, level, pdo_list->vif_field,
			       Src_PDO_Indexes);
		vif_out_end(vif_file, --level, "SrcPDO");
	}
	vif_out_end(vif_file, --level, "SrcPdoList");
}

static void vif_output_vif_component(FILE *vif_file, const struct vif_t *vif,
				     int level)
{
	int index;

	for (index = 0; index < MAX_NUM_COMPONENTS; ++index) {
		const struct vif_Component_t *component =
			&vif->Component[index];

		if (!vif_fields_present(component->vif_field,
					Component_Indexes))
			return;

		vif_out_start(vif_file, level++, "Component");
		vif_out_comment(vif_file, level, "Component %d", index);
		vif_out_fields(vif_file, level, component->vif_field,
			       Component_Indexes);
		vif_output_vif_component_snk_pdo_list(vif_file, component,
						      level);
		vif_output_vif_component_src_pdo_list(vif_file, component,
						      level);
		vif_output_vif_component_sop_svid_list(vif_file, component,
						       level);
		vif_output_vif_component_cable_svid_list(vif_file, component,
							 level);
		vif_out_end(vif_file, --level, "Component");
	}
}

static void vif_output_vif_product_usb4router_endpoint(
	FILE *vif_file, const struct vif_Usb4RouterListType_t *router,
	int level)
{
	int index;

	if (!vif_fields_present(router->PCIeEndpointList[0].vif_field,
				PCIe_Endpoint_Indexes))
		return;

	vif_out_start(vif_file, level++, "PCIeEndpointList");
	for (index = 0; index < MAX_NUM_PCIE_ENDPOINTS; ++index) {
		const struct vif_PCIeEndpointListType_t *endpont =
			&router->PCIeEndpointList[index];

		if (!vif_fields_present(endpont->vif_field,
					PCIe_Endpoint_Indexes))
			break;

		vif_out_start(vif_file, level++, "PCIeEndpoint");
		vif_out_fields(vif_file, level, endpont->vif_field,
			       PCIe_Endpoint_Indexes);
		vif_out_end(vif_file, --level, "PCIeEndpoint");
	}
	vif_out_end(vif_file, --level, "PCIeEndpointList");
}

static void vif_output_vif_product_usb4router(FILE *vif_file,
					      const struct vif_t *vif,
					      int level)
{
	int index;

	if (!vif_fields_present(vif->Product.USB4RouterList[0].vif_field,
				USB4_Router_Indexes))
		return;

	vif_out_comment(vif_file, level, "Bundle: USB4RouterList");
	vif_out_start(vif_file, level++, "USB4RouterList");
	for (index = 0; index < MAX_NUM_USB4_ROUTERS; ++index) {
		const struct vif_Usb4RouterListType_t *router =
			&vif->Product.USB4RouterList[index];

		if (!vif_fields_present(router->vif_field, USB4_Router_Indexes))
			break;

		vif_out_start(vif_file, level++, "Usb4Router");
		vif_out_comment(vif_file, level, "USB4 Router %d", index);
		vif_out_fields(vif_file, level, router->vif_field,
			       USB4_Router_Indexes);
		vif_output_vif_product_usb4router_endpoint(vif_file, router,
							   level);
		vif_out_end(vif_file, --level, "Usb4Router");
	}
	vif_out_end(vif_file, --level, "USB4RouterList");
}

static void vif_output_vif_product(FILE *vif_file, const struct vif_t *vif,
				   int level)
{
	if (!vif_fields_present(vif->Product.vif_field, Product_Indexes))
		return;

	vif_out_start(vif_file, level++, "Product");
	vif_out_comment(vif_file, level, "Product Level Content:");
	vif_out_fields(vif_file, level, vif->Product.vif_field,
		       Product_Indexes);
	vif_output_vif_product_usb4router(vif_file, vif, level);
	vif_out_end(vif_file, --level, "Product");
}

static void vif_output_vif_xml(FILE *vif_file, struct vif_t *vif, int level)
{
	vif_out_field(vif_file, level, &vif->vif_field[VIF_Specification]);

	vif_out_start(vif_file, level++, "VIF_App");
	vif_out_fields(vif_file, level, vif->vif_app_field, VIF_App_Indexes);
	vif_out_end(vif_file, --level, "VIF_App");

	vif_out_fields_range(vif_file, level, vif->vif_field, Vendor_Name,
			     VIF_Indexes);
}

static int vif_output_xml(const char *name, struct vif_t *vif)
{
	int level = 0;
	FILE *vif_file;

	vif_file = fopen(name, "w+");
	if (vif_file == NULL) {
		fprintf(stderr, "Output file '%s' could not be created\n",
			name);
		return 1;
	}

	vif_out_str(vif_file, level,
		    "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	vif_out_start(
		vif_file, level++,
		"VIF "
		"xmlns:opt=\"http://usb.org/VendorInfoFileOptionalContent.xsd\" "
		"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		"xmlns:vif=\"http://usb.org/VendorInfoFile.xsd\"");

	vif_output_vif_xml(vif_file, vif, level);
	vif_output_vif_product(vif_file, vif, level);
	vif_output_vif_component(vif_file, vif, level);

	vif_out_end(vif_file, --level, "VIF");

	fclose(vif_file);
	return 0;
}
/*
 * VIF XML Output Functions
 *****************************************************************************/

/*****************************************************************************
 * VIF Structure Override from XML file functions
 */
FILE *override_file;

int pushback_cnt;
char pushback_stack[20];

static bool ov_open(const char *over_name)
{
	override_file = fopen(over_name, "r");

	pushback_cnt = 0;
	return override_file != NULL;
}
static int ov_getc(void)
{
	if (!override_file)
		return EOF;

	if (pushback_cnt > 0)
		return pushback_stack[--pushback_cnt];
	return getc(override_file);
}
static void ovpre_getc(int cnt)
{
	if (pushback_cnt < cnt) {
		int new_pushback_cnt = cnt;

		while (cnt > 0)
			pushback_stack[--cnt] = ov_getc();
		pushback_cnt = new_pushback_cnt;
	}
}
static void ovpre_drop(int cnt)
{
	pushback_cnt -= cnt;
	if (pushback_cnt < 0)
		pushback_cnt = 0;
}
static int ovpre_peek(int index)
{
	return pushback_stack[pushback_cnt - index - 1];
}
static void ov_pushback(int ch)
{
	pushback_stack[pushback_cnt++] = ch;
}
static void ov_close(void)
{
	if (override_file)
		fclose(override_file);

	pushback_cnt = 0;
	override_file = NULL;
}

static void set_override_vif_field(struct vif_field_t *vif_field,
				   const char *name, const char *tag_value,
				   const char *str_value)
{
	char *ptr;

	if (vif_field->str_value) {
		free(vif_field->str_value);
		vif_field->str_value = NULL;
	}
	if (vif_field->tag_value) {
		free(vif_field->tag_value);
		vif_field->tag_value = NULL;
	}

	vif_field->name = name;
	if (tag_value && tag_value[0]) {
		ptr = malloc(strlen(tag_value) + 1);
		strcpy(ptr, tag_value);
		vif_field->tag_value = ptr;
	}
	if (str_value && str_value[0]) {
		ptr = malloc(strlen(str_value) + 1);
		strcpy(ptr, str_value);
		vif_field->str_value = ptr;
	}
}

static void ignore_xml_version_tag(void)
{
	int ch;

	while ((ch = ov_getc()) != EOF) {
		if (ch == '?') {
			ch = ov_getc();
			if (ch == '>')
				break;
			ov_pushback(ch);
		}
	}
}
static void ignore_comment_tag(void)
{
	int ch;

	while ((ch = ov_getc()) != EOF) {
		if (ch == '-') {
			ovpre_getc(2);
			if (ovpre_peek(0) == '-' && ovpre_peek(1) == '>') {
				/* --> */
				ovpre_drop(2);
				break;
			}
		}
	}
}
static void ignore_white_space(void)
{
	int ch;

	while ((ch = ov_getc()) != EOF) {
		if (!isspace(ch)) {
			ov_pushback(ch);
			break;
		}
	}
}
static void ignore_to_end_tag(void)
{
	int ch;

	while ((ch = ov_getc()) != EOF) {
		if (ch == '>')
			break;
	}
}

/*
 * get_next_tag() consumes the entire element when there
 * is no nested tag. there is no way to know if the end
 * tag has been consumed:
 *
 * <tag></tag>                       next call returns </end>
 * </end>
 *
 * <tag><nested value=x /></tag>     next call returns <nested>
 */
static bool get_next_tag(char *name, char *tag_value, char *str_value)
{
	int ch;
	int name_index = 0;
	int tag_index = 0;
	int str_index = 0;

	name[0] = '\0';
	tag_value[0] = '\0';
	str_value[0] = '\0';

	/*
	 * Ignore <? .... ?>
	 * Ignore <!-- ... -->
	 * Find tags <X/>, <X> and </X>
	 */
	while ((ch = ov_getc()) != EOF) {
		if (ch == '<') {
			/*
			 * Ignore XML version <? ... ?>
			 */
			ovpre_getc(1);
			if (ovpre_peek(0) == '?') {
				ovpre_drop(1);
				ignore_xml_version_tag();
				continue;
			}

			/*
			 * Ignore XML comment <!-- ... -->
			 */
			ovpre_getc(3);
			if (ovpre_peek(0) == '!' && ovpre_peek(1) == '-' &&
			    ovpre_peek(2) == '-') {
				ovpre_drop(3);
				ignore_comment_tag();
				continue;
			}

			/* Looking for terminating tag */
			ovpre_getc(1);
			if (ovpre_peek(0) == '/') {
				while ((ch = ov_getc()) != EOF) {
					if (ch == '>')
						break;
					name[name_index++] = ch;
				}
				name[name_index] = '\0';
				return true;
			}

			/* Looking for a tag name */
			while ((ch = ov_getc()) != EOF) {
				if (ch == '_' || ch == ':' || isalpha(ch) ||
				    isdigit(ch)) {
					name[name_index++] = ch;
				} else {
					ov_pushback(ch);
					break;
				}
			}
			name[name_index] = '\0';

			/* Consume any whitespace */
			ignore_white_space();

			/* See if there is a tag_string value */
			ovpre_getc(7);
			if (ovpre_peek(0) == 'v' && ovpre_peek(1) == 'a' &&
			    ovpre_peek(2) == 'l' && ovpre_peek(3) == 'u' &&
			    ovpre_peek(4) == 'e' && ovpre_peek(5) == '=' &&
			    ovpre_peek(6) == '"') {
				ovpre_drop(7);
				while ((ch = ov_getc()) != EOF) {
					if (ch == '"')
						break;
					tag_value[tag_index++] = ch;
				}
				tag_value[tag_index] = '\0';
			}

			/* Consume any whitespace */
			ignore_white_space();

			/* /> ending the tag will conclude this tag */
			ovpre_getc(2);
			if (ovpre_peek(0) == '/' && ovpre_peek(1) == '>') {
				ovpre_drop(2);
				return true;
			}
			if (ovpre_peek(0) == '>') {
				ovpre_drop(1);
				while ((ch = ov_getc()) != EOF) {
					if (ch == '<') {
						ov_pushback(ch);
						break;
					}
					str_value[str_index++] = ch;
				}
				str_value[str_index] = '\0';

				ovpre_getc(2);
				if (ovpre_peek(0) == '<' &&
				    ovpre_peek(1) == '/') {
					ovpre_drop(2);
					ignore_to_end_tag();
				}
			}
			return true;
		}
	}
	return false;
}

static void override_vif_product_pcie_endpoint_field(
	struct vif_PCIeEndpointListType_t *endpoint)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "PCIeEndpoint"))
			break;

		for (i = 0; i < PCIe_Endpoint_Indexes; i++)
			if (streq(name, vif_product_pcie_endpoint_name[i]))
				break;
		if (i != PCIe_Endpoint_Indexes)
			set_override_vif_field(
				&endpoint->vif_field[i],
				vif_product_pcie_endpoint_name[i], tag_value,
				str_value);
		else
			fprintf(stderr,
				"VIF/Component/Usb4Router/PCIeEndpoint:"
				" Unknown tag '%s'\n",
				name);
	}
}
static void override_vif_product_pcie_endpoint_list_field(
	struct vif_PCIeEndpointListType_t *endpoint_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int endpoint_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "PCIeEndpointList"))
			break;

		if (is_start_tag(name, "PCIeEndpoint"))
			override_vif_product_pcie_endpoint_field(
				&endpoint_list[endpoint_index++]);
		else
			fprintf(stderr,
				"VIF/Product/Usb4Router/PCIeEndpointList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void
override_vif_product_usb4router_fields(struct vif_Usb4RouterListType_t *router)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int endpoint_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "Usb4Router"))
			break;

		if (is_start_tag(name, "PCIeEndpointList"))
			override_vif_product_pcie_endpoint_list_field(
				&router->PCIeEndpointList[endpoint_index++]);
		else {
			int i;

			for (i = 0; i < USB4_Router_Indexes; i++)
				if (streq(name,
					  vif_product_usb4_router_name[i]))
					break;
			if (i != USB4_Router_Indexes)
				set_override_vif_field(
					&router->vif_field[i],
					vif_product_usb4_router_name[i],
					tag_value, str_value);
			else
				fprintf(stderr,
					"VIF/Component/Usb4Router:"
					" Unknown tag '%s'\n",
					name);
		}
	}
}
static void override_vif_product_usb4routerlist_fields(
	struct vif_Usb4RouterListType_t *router_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int router_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "USB4RouterList"))
			break;

		if (is_start_tag(name, "Usb4Router"))
			override_vif_product_usb4router_fields(
				&router_list[router_index++]);
		else
			fprintf(stderr,
				"VIF/Product/USB4RouterList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_product_fields(struct vif_Product_t *vif_product)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	set_vif_field_c(&vif_product->vif_field[USB4_Product_Header],
			"USB4\u2122 Product");

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "Product"))
			break;

		if (is_start_tag(name, "USB4RouterList"))
			override_vif_product_usb4routerlist_fields(
				vif_product->USB4RouterList);
		else {
			int i;

			for (i = 0; i < Product_Indexes; i++)
				if (streq(name, vif_product_name[i]))
					break;
			if (i != Product_Indexes)
				set_override_vif_field(
					&vif_product->vif_field[i],
					vif_product_name[i], tag_value,
					str_value);
			else
				fprintf(stderr,
					"VIF/Product:"
					" Unknown tag '%s'\n",
					name);
		}
	}
}

static void
override_vif_component_src_pdo_fields(struct vif_srcPdoList_t *vif_src_pdo)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "SrcPdo"))
			break;

		for (i = 0; i < Src_PDO_Indexes; i++)
			if (streq(name, vif_component_src_pdo_name[i]))
				break;
		if (i != Src_PDO_Indexes)
			set_override_vif_field(&vif_src_pdo->vif_field[i],
					       vif_component_src_pdo_name[i],
					       tag_value, str_value);
		else
			fprintf(stderr,
				"VIF/Component/SrcPdo:"
				" Unknown tag '%s'\n",
				name);
	}
}
static void override_vif_component_src_pdo_list_fields(
	struct vif_srcPdoList_t *vif_src_pdo_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int src_pdo_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "SrcPdoList"))
			break;

		if (is_start_tag(name, "SrcPdo"))
			override_vif_component_src_pdo_fields(
				&vif_src_pdo_list[src_pdo_index++]);
		else
			fprintf(stderr,
				"VIF/Component/SrcPdoList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void
override_vif_component_snk_pdo_fields(struct vif_snkPdoList_t *vif_snk_pdo)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "SnkPdo"))
			break;

		for (i = 0; i < Snk_PDO_Indexes; i++)
			if (streq(name, vif_component_snk_pdo_name[i]))
				break;
		if (i != Snk_PDO_Indexes)
			set_override_vif_field(&vif_snk_pdo->vif_field[i],
					       vif_component_snk_pdo_name[i],
					       tag_value, str_value);
		else
			fprintf(stderr,
				"VIF/Component/SnkPdo:"
				" Unknown tag '%s'\n",
				name);
	}
}
static void override_vif_component_snk_pdo_list_fields(
	struct vif_snkPdoList_t *vif_snk_pdo_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int snk_pdo_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "SnkPdoList"))
			break;

		if (is_start_tag(name, "SnkPdo"))
			override_vif_component_snk_pdo_fields(
				&vif_snk_pdo_list[snk_pdo_index++]);
		else
			fprintf(stderr,
				"VIF/Component/SnkPdoList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_component_sop_svid_mode_fields(
	struct vif_sopSVIDModeList_t *svid_mode)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "SOPSVIDMode"))
			break;

		for (i = 0; i < SopSVID_Indexes; i++)
			if (streq(name, vif_component_sop_svid_mode_name[i]))
				break;
		if (i != SopSVID_Indexes)
			set_override_vif_field(
				&svid_mode->vif_field[i],
				vif_component_sop_svid_mode_name[i], tag_value,
				str_value);
		else
			fprintf(stderr,
				"VIF/Component/SOPSVIDMode:"
				" Unknown tag '%s'\n",
				name);
	}
}
static void override_vif_component_sop_svid_mode_list_fields(
	struct vif_sopSVIDModeList_t *svid_mode_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int mode_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "SOPSVIDModeList"))
			break;

		if (is_start_tag(name, "SOPSVIDMode"))
			override_vif_component_sop_svid_mode_fields(
				&svid_mode_list[mode_index++]);
		else
			fprintf(stderr,
				"VIF/Component/SOPSVIDModeList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void
override_vif_component_sop_svid_fields(struct vif_sopSVIDList_t *vif_sop_svid)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "SOPSVID"))
			break;

		if (is_start_tag(name, "SOPSVIDModeList"))
			override_vif_component_sop_svid_mode_list_fields(
				vif_sop_svid->SOPSVIDModeList);
		else {
			int i;

			for (i = 0; i < SopSVID_Indexes; i++)
				if (streq(name, vif_component_sop_svid_name[i]))
					break;
			if (i != SopSVID_Indexes)
				set_override_vif_field(
					&vif_sop_svid->vif_field[i],
					vif_component_sop_svid_name[i],
					tag_value, str_value);
			else
				fprintf(stderr,
					"VIF/Component/SOPSVID:"
					" Unknown tag '%s'\n",
					name);
		}
	}
}
static void override_vif_component_sop_svid_list_fields(
	struct vif_sopSVIDList_t *vif_sop_svid_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int sop_svid_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "SOPSVIDList"))
			break;

		if (is_start_tag(name, "SOPSVID"))
			override_vif_component_sop_svid_fields(
				&vif_sop_svid_list[sop_svid_index++]);
		else
			fprintf(stderr,
				"VIF/Component/SOPSVIDList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_component_cable_svid_mode_fields(
	struct vif_cableSVIDModeList_t *vif_cable_mode)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "CableSVIDMode"))
			break;

		for (i = 0; i < CableSVID_Mode_Indexes; i++)
			if (streq(name, vif_cable_mode_name[i]))
				break;
		if (i != CableSVID_Mode_Indexes)
			set_override_vif_field(&vif_cable_mode->vif_field[i],
					       vif_cable_mode_name[i],
					       tag_value, str_value);
		else
			fprintf(stderr,
				"VIF/Component/CableSVIDMode:"
				" Unknown tag '%s'\n",
				name);
	}
}
static void override_vif_component_cable_svid_mode_list_fields(
	struct vif_cableSVIDModeList_t *vif_cable_mode_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int mode_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "CableSVIDModeList"))
			break;

		if (is_start_tag(name, "CableSVIDMode"))
			override_vif_component_cable_svid_mode_fields(
				&vif_cable_mode_list[mode_index++]);
		else
			fprintf(stderr,
				"VIF/Component/CableSVIDModeList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_component_cable_svid_fields(
	struct vif_cableSVIDList_t *vif_cable_svid)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int mode_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "CableSVID"))
			break;

		if (is_start_tag(name, "CableSVIDModeList"))
			override_vif_component_cable_svid_mode_list_fields(
				&vif_cable_svid
					 ->CableSVIDModeList[mode_index++]);
		else {
			int i;

			for (i = 0; i < CableSVID_Indexes; i++)
				if (streq(name, vif_cable_svid_name[i]))
					break;
			if (i != CableSVID_Indexes)
				set_override_vif_field(
					&vif_cable_svid->vif_field[i],
					vif_cable_svid_name[i], tag_value,
					str_value);
			else
				fprintf(stderr,
					"VIF/Component/CableSVID:"
					" Unknown tag '%s'\n",
					name);
		}
	}
}
static void override_vif_component_cable_svid_list_fields(
	struct vif_cableSVIDList_t *vif_cable_svid_list)
{
	char name[80];
	char tag_value[80];
	char str_value[80];
	int cable_svid_index = 0;

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "CableSVIDList"))
			break;

		if (is_start_tag(name, "CableSVID"))
			override_vif_component_cable_svid_fields(
				&vif_cable_svid_list[cable_svid_index++]);
		else
			fprintf(stderr,
				"VIF/Component/CableSVIDList:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_component_fields(struct vif_Component_t *vif_component)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "Component"))
			break;

		if (is_start_tag(name, "SrcPdoList"))
			override_vif_component_src_pdo_list_fields(
				vif_component->SrcPdoList);
		else if (is_start_tag(name, "SnkPdoList"))
			override_vif_component_snk_pdo_list_fields(
				vif_component->SnkPdoList);
		else if (is_start_tag(name, "SOPSVIDList"))
			override_vif_component_sop_svid_list_fields(
				vif_component->SOPSVIDList);
		else if (is_start_tag(name, "CableSVIDList"))
			override_vif_component_cable_svid_list_fields(
				vif_component->CableSVIDList);
		else {
			int i;

			for (i = 0; i < Component_Indexes; i++)
				if (streq(name, vif_component_name[i]))
					break;
			if (i != Component_Indexes)
				set_override_vif_field(
					&vif_component->vif_field[i],
					vif_component_name[i], tag_value,
					str_value);
			else
				fprintf(stderr,
					"VIF/Component:"
					" Unknown tag '%s'\n",
					name);
		}
	}
}

static void override_vif_app_fields(struct vif_t *vif)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	while (get_next_tag(name, tag_value, str_value)) {
		int i;

		if (is_end_tag(name, "VIF_App"))
			break;

		for (i = 0; i < VIF_App_Indexes; i++)
			if (streq(name, vif_app_name[i]))
				break;
		if (i == VIF_App_Indexes)
			fprintf(stderr,
				"VIF/VIF_App:"
				" Unknown tag '%s'\n",
				name);
	}
}

static void override_vif_fields(struct vif_t *vif)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	component_index = 0;
	while (get_next_tag(name, tag_value, str_value)) {
		if (is_end_tag(name, "VIF"))
			break;

		if (is_start_tag(name, "VIF_App"))
			override_vif_app_fields(vif);
		else if (is_start_tag(name, "Component"))
			override_vif_component_fields(
				&vif->Component[component_index++]);
		else if (is_start_tag(name, "Product"))
			override_vif_product_fields(&vif->Product);
		else {
			int i;

			for (i = 0; i < VIF_Indexes; i++)
				if (streq(name, vif_name[i]))
					break;
			if (i != VIF_Indexes)
				set_override_vif_field(&vif->vif_field[i],
						       vif_name[i], tag_value,
						       str_value);
			else
				fprintf(stderr,
					"VIF:"
					" Unknown tag '%s'\n",
					name);
		}
	}

	/*
	 * Don't care what they requested, I am making the file and that
	 * means VIF/VIF_App is to be set by me.
	 */
	set_override_vif_field(&vif->vif_app_field[Vendor],
			       vif_app_name[Vendor], NULL,
			       VIF_APP_VENDOR_VALUE);

	set_override_vif_field(&vif->vif_app_field[Name], vif_app_name[Name],
			       NULL, VIF_APP_NAME_VALUE);

	set_override_vif_field(&vif->vif_app_field[Version],
			       vif_app_name[Version], NULL,
			       VIF_APP_VERSION_VALUE);
}

static int override_gen_vif(char *over_name, struct vif_t *vif)
{
	char name[80];
	char tag_value[80];
	char str_value[80];

	if (!ov_open(over_name)) {
		fprintf(stderr, "Override file '%s' could not be opened\n",
			over_name);
		return 1;
	}

	while (get_next_tag(name, tag_value, str_value)) {
		if (is_start_tag(name, "VIF"))
			override_vif_fields(vif);
		else
			fprintf(stderr, "Unknown tag '%s'\n", name);
	}

	ov_close();
	return 0;
}
/*
 * VIF Structure Override from XML file functions
 *****************************************************************************/

/*****************************************************************************
 * VIF Structure Initialization Helper Functions
 */
static void set_vif_field(struct vif_field_t *vif_field, const char *name,
			  const char *tag_value, const char *str_value)
{
	char *ptr;

	/*
	 * Override already set or trying to set to nothing should do
	 * nothing. Just return
	 */
	if ((vif_field->name || vif_field->str_value || vif_field->tag_value) ||
	    (str_value == NULL && tag_value == NULL))
		return;

	vif_field->name = name;
	if (tag_value) {
		ptr = malloc(strlen(tag_value) + 1);
		strcpy(ptr, tag_value);
		vif_field->tag_value = ptr;
	}
	if (str_value) {
		ptr = malloc(strlen(str_value) + 1);
		strcpy(ptr, str_value);
		vif_field->str_value = ptr;
	}
}
__maybe_unused static void set_vif_field_b(struct vif_field_t *vif_field,
					   const char *name, const bool val)
{
	if (val)
		set_vif_field(vif_field, name, "true", NULL);
	else
		set_vif_field(vif_field, name, "false", NULL);
}
__maybe_unused static void set_vif_field_stis(struct vif_field_t *vif_field,
					      const char *name,
					      const char *tag_value,
					      const int str_value)
{
	char str_str[20];

	sprintf(str_str, "%d", str_value);
	set_vif_field(vif_field, name, tag_value, str_str);
}
__maybe_unused static void set_vif_field_itss(struct vif_field_t *vif_field,
					      const char *name,
					      const int tag_value,
					      const char *str_value)
{
	char str_tag[20];

	sprintf(str_tag, "%d", tag_value);
	set_vif_field(vif_field, name, str_tag, str_value);
}
__maybe_unused static void set_vif_field_itis(struct vif_field_t *vif_field,
					      const char *name,
					      const int tag_value,
					      const int str_value)
{
	char str_tag[20];
	char str_str[20];

	sprintf(str_tag, "%d", tag_value);
	sprintf(str_str, "%d", str_value);
	set_vif_field(vif_field, name, str_tag, str_str);
}

static void set_vif_field_c(struct vif_field_t *vif_field, const char *comment)
{
	set_vif_field(vif_field, NULL, comment, NULL);
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
 * vif_Component USB4 Port Fields
 *	USB4_Lane_0_Adapter			numericFieldType
 *	USB4_DFP_Supported			booleanFieldType
 *	USB4_UFP_Supported			booleanFieldType
 *	USB4_USB3_Tunneling_Supported		booleanFieldType
 *	USB4_DP_Tunneling_Supported		booleanFieldType
 *	USB4_CL1_State_Supported		booleanFieldType
 *	USB4_CL2_State_Supported		booleanFieldType
 *	USB4_Num_Retimers			numericFieldType
 *	USB4_DP_Bit_Rate			numericFieldType
 *	USB4_Num_DP_Lanes			numericFieldType
 *
 * vif_Component USB4 Product Fields
 *	USB4_Dock				booleanFieldType
 *	USB4_Num_Internal_Host_Controllers	numericFieldType
 *	USB4_Num_PCIe_DN_Bridges		numericFieldType
 *	USB4_Device_HiFi_Bi_TMU_Mode_Required	booleanFieldType
 *
 * vif_Component USB4 Device Class Fallback Support
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
 *
 * vif_Usb4RouterListType USB4 Router Fields
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
 * vif_PCIeEndpointListType PCIe Endpoint Fields
 *	USB4_PCIe_Endpoint_Vendor_ID		numericFieldType
 *	USB4_PCIe_Endpoint_Device_ID		numericFieldType
 *	USB4_PCIe_Endpoint_Class_Code		numericFieldType
 *
 * vif_sopSVIDList SOP SVIDs
 *	SVID_SOP				numericFieldType
 *	SVID_Modes_Fixed_SOP			booleanFieldType
 *	SVID_Num_Modes_Min_SOP			numericFieldType
 *	SVID_Num_Modes_Max_SOP			numericFieldType
 *
 * vif_sopSVIDModeList SOP SVID Modes
 *	SVID_Mode_Enter_SOP			booleanFieldType
 *	SVID_Mode_Recog_Mask_SOP		numericFieldType
 *	SVID_Mode_Recog_Value_SOP		numericFieldType
 *
 * vif_Component Alternate Mode Adapter Fields
 *	AMA_HW_Vers				numericFieldType
 *	AMA_FW_Vers				numericFieldType
 *	AMA_VCONN_Power				booleanFieldType
 *	AMA_VCONN_Reqd				booleanFieldType
 *	AMA_VBUS_Reqd				booleanFieldType
 *	AMA_Superspeed_Support			numericFieldType
 *
 * vif_Component Cable/eMarker Fields
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
 *	Cable_VBUS_Current			numericFieldType
 *	VBUS_Through_Cable			booleanFieldType
 *	Cable_Superspeed_Support		numericFieldType
 *	Cable_USB_Highest_Speed			numericFieldType
 *	Max_VBUS_Voltage_Vdm_V2			numericFieldType
 *	Manufacturer_Info_Supported,		booleanFieldType
 *	Manufacturer_Info_VID,			numericFieldType
 *	Manufacturer_Info_PID,			numericFieldType
 *	Chunking_Implemented			booleanFieldType
 *	Security_Msgs_Supported			booleanFieldType
 *	ID_Header_Connector_Type		numericFieldType
 *	Cable_Num_SVIDs_Min			numericFieldType
 *	Cable_Num_SVIDs_Max			numericFieldType
 *	SVID_Fixed				booleanFieldType
 *
 * vif_cableSVIDList Cable SVIDs
 *	SVID					numericFieldType
 *	SVID_Modes_Fixed			booleanFieldType
 *	SVID_Num_Modes_Min			numericFieldType
 *	SVID_Num_Modes_Max			numericFieldType
 *
 * vif_cableSVIDModeList Cable SVID Modes
 *	SVID_Mode_Enter				booleanFieldType
 *	SVID_Mode_Recog_Mask			numericFieldType
 *	SVID_Mode_Recog_Value			numericFieldType
 *
 * vif_Component Active Cable Fields
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
 *	Active_Cable_USB2_Supported		booleanFieldType
 *	Active_Cable_USB2_Hub_Hops_Consumed	numericFieldType
 *	Active_Cable_USB32_Supported		booleanFieldType
 *	Active_Cable_USB_Lanes			numericFieldType
 *	Active_Cable_Optically_Isolated		booleanFieldType
 *	Active_Cable_USB_Gen			numericFieldType
 *
 * vif_Component VCONN Powered Device
 *	VPD_HW_Vers				numericFieldType
 *	VPD_FW_Vers				numericFieldType
 *	VPD_Max_VBUS_Voltage			numericFieldType
 *	VPD_Charge_Through_Support		booleanFieldType
 *	VPD_Charge_Through_Current		numericFieldType
 *	VPD_VBUS_Impedance			numericFieldType
 *	VPD_Ground_Impedance			numericFieldType
 *
 * vif_Component Repeater Fields
 *	Repeater_One_Type			numericFieldType
 *	Repeater_Two_Type			numericFieldType
 */

__maybe_unused static int32_t init_vif_snk_pdo(struct vif_snkPdoList_t *snkPdo,
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
			      vif_component_snk_pdo_name[Snk_PDO_Supply_Type],
			      "0", "Fixed");
		sprintf(str, "%d mV", voltage_mv);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Voltage],
				   vif_component_snk_pdo_name[Snk_PDO_Voltage],
				   voltage, str);
		sprintf(str, "%d mA", current_ma);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Op_Current],
			vif_component_snk_pdo_name[Snk_PDO_Op_Current], current,
			str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		int32_t power;

		power = pdo & 0x3ff;
		power_mw = power * 250;

		set_vif_field(&snkPdo->vif_field[Snk_PDO_Supply_Type],
			      vif_component_snk_pdo_name[Snk_PDO_Supply_Type],
			      "1", "Battery");
		sprintf(str, "%d mV", min_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Min_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Min_Voltage],
			min_voltage, str);
		sprintf(str, "%d mV", max_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Max_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Max_Voltage],
			max_voltage, str);
		sprintf(str, "%d mW", power_mw);
		set_vif_field_itss(&snkPdo->vif_field[Snk_PDO_Op_Power],
				   vif_component_snk_pdo_name[Snk_PDO_Op_Power],
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
			      vif_component_snk_pdo_name[Snk_PDO_Supply_Type],
			      "2", "Variable");
		sprintf(str, "%d mV", min_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Min_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Min_Voltage],
			min_voltage, str);
		sprintf(str, "%d mV", max_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Max_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Max_Voltage],
			max_voltage, str);
		sprintf(str, "%d mA", current_ma);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Op_Current],
			vif_component_snk_pdo_name[Snk_PDO_Op_Current], current,
			str);

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
			      vif_component_snk_pdo_name[Snk_PDO_Supply_Type],
			      "3", "PPS");
		sprintf(str, "%d mA", pps_current_ma);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Op_Current],
			vif_component_snk_pdo_name[Snk_PDO_Op_Current],
			pps_current, str);
		sprintf(str, "%d mV", pps_min_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Min_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Min_Voltage],
			pps_min_voltage, str);
		sprintf(str, "%d mV", pps_max_voltage_mv);
		set_vif_field_itss(
			&snkPdo->vif_field[Snk_PDO_Max_Voltage],
			vif_component_snk_pdo_name[Snk_PDO_Max_Voltage],
			pps_max_voltage, str);
	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power_mw;
}

__maybe_unused static int32_t init_vif_src_pdo(struct vif_srcPdoList_t *srcPdo,
					       uint32_t pdo)
{
	int32_t power_mw;
	char str[40];

	/*********************************************************************
	 * Source PDOs
	 *
	 * TODO: Generic todo to fill in additional fields as the need presents
	 * itself
	 *
	 * Fields that are not currently being initialized
	 *
	 * vif_srcPdoList
	 *	Src_PD_OCP_OC_Debounce			numericFieldType
	 *	Src_PD_OCP_OC_Threshold			numericFieldType
	 *	Src_PD_OCP_UV_Debounce			numericFieldType
	 *	Src_PD_OCP_UV_Threshold_Type		numericFieldType
	 *	Src_PD_OCP_UV_Threshold			numericFieldType
	 */
	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_FIXED) {
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;
		uint32_t voltage = (pdo >> 10) & 0x3ff;
		uint32_t voltage_mv = voltage * 50;

		power_mw = (current_ma * voltage_mv) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
			      vif_component_src_pdo_name[Src_PDO_Supply_Type],
			      "0", "Fixed");
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
			      vif_component_src_pdo_name[Src_PDO_Peak_Current],
			      "0", "100% IOC");
		sprintf(str, "%d mV", voltage_mv);
		set_vif_field_itss(&srcPdo->vif_field[Src_PDO_Voltage],
				   vif_component_src_pdo_name[Src_PDO_Voltage],
				   voltage, str);
		sprintf(str, "%d mA", current_ma);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Current],
			vif_component_src_pdo_name[Src_PDO_Max_Current],
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
			      vif_component_src_pdo_name[Src_PDO_Supply_Type],
			      "1", "Battery");
		sprintf(str, "%d mV", min_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Min_Voltage],
			vif_component_src_pdo_name[Src_PDO_Min_Voltage],
			min_voltage, str);
		sprintf(str, "%d mV", max_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Voltage],
			vif_component_src_pdo_name[Src_PDO_Max_Voltage],
			max_voltage, str);
		sprintf(str, "%d mW", power_mw);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Power],
			vif_component_src_pdo_name[Src_PDO_Max_Power], power,
			str);

	} else if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_VARIABLE) {
		uint32_t max_voltage = (pdo >> 20) & 0x3ff;
		uint32_t max_voltage_mv = max_voltage * 50;
		uint32_t min_voltage = (pdo >> 10) & 0x3ff;
		uint32_t min_voltage_mv = min_voltage * 50;
		uint32_t current = pdo & 0x3ff;
		uint32_t current_ma = current * 10;

		power_mw = (current_ma * max_voltage_mv) / 1000;

		set_vif_field(&srcPdo->vif_field[Src_PDO_Supply_Type],
			      vif_component_src_pdo_name[Src_PDO_Supply_Type],
			      "2", "Variable");
		set_vif_field(&srcPdo->vif_field[Src_PDO_Peak_Current],
			      vif_component_src_pdo_name[Src_PDO_Peak_Current],
			      "0", "100% IOC");
		sprintf(str, "%d mV", min_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Min_Voltage],
			vif_component_src_pdo_name[Src_PDO_Min_Voltage],
			min_voltage, str);
		sprintf(str, "%d mV", max_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Voltage],
			vif_component_src_pdo_name[Src_PDO_Max_Voltage],
			max_voltage, str);
		sprintf(str, "%d mA", current_ma);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Current],
			vif_component_src_pdo_name[Src_PDO_Max_Current],
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
			      vif_component_src_pdo_name[Src_PDO_Supply_Type],
			      "3", "PPS");
		sprintf(str, "%d mA", pps_current_ma);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Current],
			vif_component_src_pdo_name[Src_PDO_Max_Current],
			pps_current, str);
		sprintf(str, "%d mV", pps_min_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Min_Voltage],
			vif_component_src_pdo_name[Src_PDO_Min_Voltage],
			pps_min_voltage, str);
		sprintf(str, "%d mV", pps_max_voltage_mv);
		set_vif_field_itss(
			&srcPdo->vif_field[Src_PDO_Max_Voltage],
			vif_component_src_pdo_name[Src_PDO_Max_Voltage],
			pps_max_voltage, str);

	} else {
		fprintf(stderr, "ERROR: Invalid PDO_TYPE %d.\n", pdo);
		return -1;
	}

	return power_mw;
}

/*********************************************************************
 * Init VIF Fields
 */
static void init_vif_fields(struct vif_field_t *vif_fields,
			    struct vif_field_t *vif_app_fields,
			    const char *board)
{
	set_vif_field(&vif_fields[VIF_Specification],
		      vif_name[VIF_Specification], NULL, "3.18");

	set_vif_field(&vif_app_fields[Vendor], vif_app_name[Vendor], NULL,
		      VIF_APP_VENDOR_VALUE);

	set_vif_field(&vif_app_fields[Name], vif_app_name[Name], NULL,
		      VIF_APP_NAME_VALUE);

	set_vif_field(&vif_app_fields[Version], vif_app_name[Version], NULL,
		      VIF_APP_VERSION_VALUE);

	set_vif_field(&vif_fields[Vendor_Name], vif_name[Vendor_Name], NULL,
		      VENDOR_NAME_VALUE);

#if defined(CONFIG_USB_PD_MODEL_PART_NUMBER)
	set_vif_field(&vif_fields[Model_Part_Number],
		      vif_name[Model_Part_Number], NULL,
		      CONFIG_USB_PD_MODEL_PART_NUMBER);
#else
	if (board && strlen(board) > 0)
		set_vif_field(&vif_fields[Model_Part_Number],
			      vif_name[Model_Part_Number], NULL, board);
	else
		set_vif_field(&vif_fields[Model_Part_Number],
			      vif_name[Model_Part_Number], NULL, "FIX-ME");
#endif

#if defined(CONFIG_USB_PD_PRODUCT_REVISION)
	set_vif_field(&vif_fields[Product_Revision], vif_name[Product_Revision],
		      NULL, CONFIG_USB_PD_PRODUCT_REVISION);
#else
	set_vif_field(&vif_fields[Product_Revision], vif_name[Product_Revision],
		      NULL, "FIX-ME");
#endif

#if defined(CONFIG_USB_PD_TID)
	set_vif_field_stis(&vif_fields[TID], vif_name[TID], NULL,
			   CONFIG_USB_PD_TID);
#else
	set_vif_field_stis(&vif_fields[TID], vif_name[TID], NULL,
			   DEFAULT_MISSING_TID);
#endif

	set_vif_field(&vif_fields[VIF_Product_Type], vif_name[VIF_Product_Type],
		      "0", "Port Product");

	set_vif_field(&vif_fields[Certification_Type],
		      vif_name[Certification_Type], "0", "End Product");
}

/*********************************************************************
 * Init VIF/Component[] Fields
 */
static void init_vif_component_fields(struct vif_field_t *vif_fields,
				      enum bc_1_2_support *bc_support,
				      enum dtype type)
{
#if defined(CONFIG_USB_PD_PORT_LABEL)
	set_vif_field_stis(&vif_fields[Port_Label],
			   vif_component_name[Port_Label], NULL,
			   CONFIG_USB_PD_PORT_LABEL);
#else
	set_vif_field_stis(&vif_fields[Port_Label],
			   vif_component_name[Port_Label], NULL,
			   component_index);
#endif

	set_vif_field(&vif_fields[Connector_Type],
		      vif_component_name[Connector_Type], "2", "Type-C®");

	if (is_usb4_supported()) {
		int router_index;

		set_vif_field_b(&vif_fields[USB4_Supported],
				vif_component_name[USB4_Supported], true);

		if (!get_vif_field_tag_number(
			    &vif.Product.USB4RouterList[0]
				     .vif_field[USB4_Router_ID],
			    &router_index)) {
			router_index = 0;
		}
		set_vif_field_itss(&vif_fields[USB4_Router_Index],
				   vif_component_name[USB4_Router_Index],
				   router_index, NULL);
	} else {
		set_vif_field_b(&vif_fields[USB4_Supported],
				vif_component_name[USB4_Supported], false);
	}

	set_vif_field_b(&vif_fields[USB_PD_Support],
			vif_component_name[USB_PD_Support],
			is_usb_pd_supported());

	if (is_usb_pd_supported()) {
		switch (type) {
		case SNK:
			set_vif_field(&vif_fields[PD_Port_Type],
				      vif_component_name[PD_Port_Type], "0",
				      "Consumer Only");
			break;
		case SRC:
			set_vif_field(&vif_fields[PD_Port_Type],
				      vif_component_name[PD_Port_Type], "3",
				      "Provider Only");
			break;
		case DRP:
			set_vif_field(&vif_fields[PD_Port_Type],
				      vif_component_name[PD_Port_Type], "4",
				      "DRP");
			break;
		}
	}

	switch (type) {
	case SNK:
		set_vif_field(&vif_fields[Type_C_State_Machine],
			      vif_component_name[Type_C_State_Machine], "1",
			      "SNK");
		break;
	case SRC:
		set_vif_field(&vif_fields[Type_C_State_Machine],
			      vif_component_name[Type_C_State_Machine], "0",
			      "SRC");
		break;
	case DRP:
		set_vif_field(&vif_fields[Type_C_State_Machine],
			      vif_component_name[Type_C_State_Machine], "2",
			      "DRP");
		break;
	}

	set_vif_field_b(&vif_fields[Captive_Cable],
			vif_component_name[Captive_Cable], false);

	set_vif_field_b(&vif_fields[Port_Battery_Powered],
			vif_component_name[Port_Battery_Powered],
			IS_ENABLED(CONFIG_BATTERY));

	*bc_support = BC_1_2_SUPPORT_NONE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_MAX14637))
		*bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_MT6360))
		*bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;
	if (IS_ENABLED(CONFIG_BC12_DETECT_PI3USB9201))
		*bc_support = BC_1_2_SUPPORT_BOTH;
	if (IS_ENABLED(CONFIG_BC12_DETECT_PI3USB9281))
		*bc_support = BC_1_2_SUPPORT_PORTABLE_DEVICE;

	switch (*bc_support) {
	case BC_1_2_SUPPORT_NONE:
		set_vif_field(&vif_fields[BC_1_2_Support],
			      vif_component_name[BC_1_2_Support], "0", "None");
		break;
	case BC_1_2_SUPPORT_PORTABLE_DEVICE:
		set_vif_field(&vif_fields[BC_1_2_Support],
			      vif_component_name[BC_1_2_Support], "1",
			      "Portable Device");
		break;
	case BC_1_2_SUPPORT_CHARGING_PORT:
		set_vif_field(&vif_fields[BC_1_2_Support],
			      vif_component_name[BC_1_2_Support], "2",
			      "Charging Port");
		break;
	case BC_1_2_SUPPORT_BOTH:
		set_vif_field(&vif_fields[BC_1_2_Support],
			      vif_component_name[BC_1_2_Support], "3", "Both");
		break;
	}
}

/*********************************************************************
 * Init VIF/Component[] General PD Fields
 */
static void init_vif_component_general_pd_fields(struct vif_field_t *vif_fields,
						 enum dtype type)
{
	if (IS_ENABLED(CONFIG_USB_PD_REV30) || IS_ENABLED(CONFIG_USB_PRL_SM)) {
		set_vif_field(&vif_fields[PD_Spec_Revision_Major],
			      vif_component_name[PD_Spec_Revision_Major], "3",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Revision_Minor],
			      vif_component_name[PD_Spec_Revision_Minor], "1",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Major],
			      vif_component_name[PD_Spec_Version_Major], "1",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Minor],
			      vif_component_name[PD_Spec_Version_Minor], "3",
			      NULL);

		set_vif_field(&vif_fields[PD_Specification_Revision],
			      vif_component_name[PD_Specification_Revision],
			      "2", "Revision 3");
	} else {
		set_vif_field(&vif_fields[PD_Spec_Revision_Major],
			      vif_component_name[PD_Spec_Revision_Major], "2",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Revision_Minor],
			      vif_component_name[PD_Spec_Revision_Minor], "0",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Major],
			      vif_component_name[PD_Spec_Version_Major], "1",
			      NULL);
		set_vif_field(&vif_fields[PD_Spec_Version_Minor],
			      vif_component_name[PD_Spec_Version_Minor], "3",
			      NULL);

		set_vif_field(&vif_fields[PD_Specification_Revision],
			      vif_component_name[PD_Specification_Revision],
			      "1", "Revision 2");
	}

	set_vif_field_b(&vif_fields[USB_Comms_Capable],
			vif_component_name[USB_Comms_Capable],
			is_usb_comms_capable());

	/*
	 * DR_Swap_To_DFP_Supported
	 *
	 * Set to YES if Qualifying Product can respond with an Accept to a
	 * DR_Swap request to switch from a UFP to a DFP.
	 *
	 * If Type_C_State_Machine is set to DRP and Type_C_Can_Act_As_Host
	 * is set to YES and Type_C_Can_Act_As_Device is set to NO then this
	 * field shall be set to YES.
	 *
	 * If Type_C_State_Machine is set to SNK and either
	 * Type_C_Can_Act_As_Host or Type_C_Is_Alt_Mode_Controller is set to
	 * YES, then this field shall be set to YES.
	 *
	 * If Type_C_State_Machine is set to SRC and Type_C_Can_Act_As_Device
	 * is set to YES, then this field shall be set to YES.
	 *
	 * If VIF_Product_Type is set to 1 (Cable) or PD_Port_Type is set to
	 * 5 (eMarker) then this field shall be ignored by Testers.
	 *
	 * TODO(b/172437046): USB4 has not been added and this last statement
	 * needs to be handled when it is:
	 * If USB4_DFP_Supported is set to YES and Type_C_Port_On_Hub is set
	 * to NO, then this field shall be set to YES.
	 */
	{
		bool supports_to_dfp = false;

		switch (type) {
		case SRC:
			supports_to_dfp = can_act_as_device();
			break;
		case SNK:
			supports_to_dfp =
				(can_act_as_host() || is_alt_mode_controller());
			break;
		case DRP:
			supports_to_dfp =
				(can_act_as_host() && !can_act_as_device());
			break;
		}

		set_vif_field_b(&vif_fields[DR_Swap_To_DFP_Supported],
				vif_component_name[DR_Swap_To_DFP_Supported],
				supports_to_dfp);
	}

	/*
	 * DR_Swap_To_UFP_Supported
	 *
	 * Set to YES if Qualifying Product can respond with an Accept to a
	 * DR_Swap request to switch from a DFP to a UFP.
	 *
	 * If Type_C_State_Machine is set to DRP and Type_C_Can_Act_As_Device
	 * is set to YES and Type_C_Can_Act_As_Host is set to NO then this
	 * field shall be set to YES.
	 *
	 * If Type_C_State_Machine is set to SNK and either
	 * Type_C_Can_Act_As_Host or Type_C_Is_Alt_Mode_Controller is set to
	 * YES, then this field shall be set to YES.
	 *
	 * If Type_C_State_Machine is set to SRC and Type_C_Can_Act_As_Device
	 * is set to YES, then this field shall be set to YES.
	 *
	 * If VIF_Product_Type is set to 1 (Cable) or PD_Port_Type is set to
	 * 5 (eMarker) then this field shall be ignored by Testers.
	 */
	{
		bool supports_to_ufp = false;

		switch (type) {
		case SRC:
			supports_to_ufp = can_act_as_device();
			break;
		case SNK:
			supports_to_ufp =
				(can_act_as_host() || is_alt_mode_controller());
			break;
		case DRP:
			supports_to_ufp =
				(can_act_as_device() && !can_act_as_host());
			break;
		}

		set_vif_field_b(&vif_fields[DR_Swap_To_UFP_Supported],
				vif_component_name[DR_Swap_To_UFP_Supported],
				supports_to_ufp);
	}

	if (is_src()) {
		/* SRC capable */
		if (IS_ENABLED(CONFIG_CHARGER))
			/* USB-C UP bit set */
			set_vif_field_b(&vif_fields[Unconstrained_Power],
					vif_component_name[Unconstrained_Power],
					(src_pdo[0] & PDO_FIXED_UNCONSTRAINED));
		else {
			/* Barrel charger being used */
			int32_t dedicated_charge_port_count = 0;

#ifdef CONFIG_DEDICATED_CHARGE_PORT_COUNT
			dedicated_charge_port_count =
				CONFIG_DEDICATED_CHARGE_PORT_COUNT;
#endif

			set_vif_field_b(&vif_fields[Unconstrained_Power],
					vif_component_name[Unconstrained_Power],
					(dedicated_charge_port_count > 0));
		}
	} else {
		/* Not SRC capable */
		set_vif_field_b(&vif_fields[Unconstrained_Power],
				vif_component_name[Unconstrained_Power], false);
	}

	set_vif_field_b(&vif_fields[VCONN_Swap_To_On_Supported],
			vif_component_name[VCONN_Swap_To_On_Supported],
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP));

	set_vif_field_b(&vif_fields[VCONN_Swap_To_Off_Supported],
			vif_component_name[VCONN_Swap_To_Off_Supported],
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP));

	set_vif_field_b(&vif_fields[Responds_To_Discov_SOP_UFP],
			vif_component_name[Responds_To_Discov_SOP_UFP],
			does_respond_to_discov_sop_ufp());

	set_vif_field_b(&vif_fields[Responds_To_Discov_SOP_DFP],
			vif_component_name[Responds_To_Discov_SOP_DFP],
			does_respond_to_discov_sop_dfp());

	set_vif_field_b(&vif_fields[Attempts_Discov_SOP],
			vif_component_name[Attempts_Discov_SOP],
			((!IS_ENABLED(CONFIG_USB_PD_SIMPLE_DFP)) ||
			 (type != SRC)));

	set_vif_field(&vif_fields[Power_Interruption_Available],
		      vif_component_name[Power_Interruption_Available], "0",
		      "No Interruption Possible");

	set_vif_field_b(&vif_fields[Data_Reset_Supported],
			vif_component_name[Data_Reset_Supported],
			IS_ENABLED(CONFIG_USB_PD_USB4));

	set_vif_field_b(&vif_fields[Enter_USB_Supported],
			vif_component_name[Enter_USB_Supported],
			IS_ENABLED(CONFIG_USB_PD_USB4));

	set_vif_field_b(&vif_fields[Chunking_Implemented_SOP],
			vif_component_name[Chunking_Implemented_SOP],
			(IS_ENABLED(CONFIG_USB_PD_REV30) &&
			 IS_ENABLED(CONFIG_USB_PRL_SM)));

	set_vif_field_b(
		&vif_fields[Unchunked_Extended_Messages_Supported],
		vif_component_name[Unchunked_Extended_Messages_Supported],
		false);

	if (IS_ENABLED(CONFIG_USB_PD_MANUFACTURER_INFO)) {
		char hex_str[10];

		set_vif_field_b(
			&vif_fields[Manufacturer_Info_Supported_Port],
			vif_component_name[Manufacturer_Info_Supported_Port],
			true);

		sprintf(hex_str, "%04X", USB_VID_GOOGLE);
		set_vif_field_itss(
			&vif_fields[Manufacturer_Info_VID_Port],
			vif_component_name[Manufacturer_Info_VID_Port],
			USB_VID_GOOGLE, hex_str);

#if defined(CONFIG_USB_PID)
		sprintf(hex_str, "%04X", CONFIG_USB_PID);
		set_vif_field_itss(
			&vif_fields[Manufacturer_Info_PID_Port],
			vif_component_name[Manufacturer_Info_PID_Port],
			CONFIG_USB_PID, hex_str);
#else
		sprintf(hex_str, "%04X", DEFAULT_MISSING_PID);
		set_vif_field_itss(
			&vif_fields[Manufacturer_Info_PID_Port],
			vif_component_name[Manufacturer_Info_PID_Port],
			DEFAULT_MISSING_PID, hex_str);
#endif
	} else {
		set_vif_field_b(
			&vif_fields[Manufacturer_Info_Supported_Port],
			vif_component_name[Manufacturer_Info_Supported_Port],
			false);
	}

	set_vif_field_b(&vif_fields[Security_Msgs_Supported_SOP],
			vif_component_name[Security_Msgs_Supported_SOP],
			IS_ENABLED(CONFIG_USB_PD_SECURITY_MSGS));

#if defined(CONFIG_NUM_FIXED_BATTERIES)
	set_vif_field_itss(&vif_fields[Num_Fixed_Batteries],
			   vif_component_name[Num_Fixed_Batteries],
			   CONFIG_NUM_FIXED_BATTERIES, NULL);
#elif defined(CONFIG_USB_CTVPD) || defined(CONFIG_USB_VPD)
	set_vif_field(&vif_fields[Num_Fixed_Batteries],
		      vif_component_name[Num_Fixed_Batteries], "0", NULL);
#else
	set_vif_field(&vif_fields[Num_Fixed_Batteries],
		      vif_component_name[Num_Fixed_Batteries], "1", NULL);
#endif

	set_vif_field(&vif_fields[Num_Swappable_Battery_Slots],
		      vif_component_name[Num_Swappable_Battery_Slots], "0",
		      NULL);

	set_vif_field(&vif_fields[ID_Header_Connector_Type_SOP],
		      vif_component_name[ID_Header_Connector_Type_SOP], "2",
		      "USB Type-C\u00ae Receptacle");
}

/*********************************************************************
 * Init VIF/Component[] SOP* Capabilities Fields
 */
static void
init_vif_component_sop_capabilities_fields(struct vif_field_t *vif_fields)
{
	set_vif_field_b(&vif_fields[SOP_Capable],
			vif_component_name[SOP_Capable], can_act_as_host());

	set_vif_field_b(&vif_fields[SOP_P_Capable],
			vif_component_name[SOP_P_Capable],
			IS_ENABLED(CONFIG_USB_PD_DECODE_SOP));

	set_vif_field_b(&vif_fields[SOP_PP_Capable],
			vif_component_name[SOP_PP_Capable],
			IS_ENABLED(CONFIG_USB_PD_DECODE_SOP));

	set_vif_field_b(&vif_fields[SOP_P_Debug_Capable],
			vif_component_name[SOP_P_Debug_Capable], false);

	set_vif_field_b(&vif_fields[SOP_PP_Debug_Capable],
			vif_component_name[SOP_PP_Debug_Capable], false);
}

/*********************************************************************
 * Init VIF/Component[] USB Type-C Fields
 */
static void init_vif_component_usb_type_c_fields(struct vif_field_t *vif_fields,
						 enum dtype type)
{
	set_vif_field_b(&vif_fields[Type_C_Implements_Try_SRC],
			vif_component_name[Type_C_Implements_Try_SRC],
			IS_ENABLED(CONFIG_USB_PD_TRY_SRC));

	set_vif_field_b(&vif_fields[Type_C_Implements_Try_SNK],
			vif_component_name[Type_C_Implements_Try_SNK], false);

	{
		int rp = CONFIG_USB_PD_PULLUP;

#if defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
		rp = CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
#endif

		switch (rp) {
		case 0:
			set_vif_field(&vif_fields[RP_Value],
				      vif_component_name[RP_Value], "0",
				      "Default");
			break;
		case 1:
			set_vif_field(&vif_fields[RP_Value],
				      vif_component_name[RP_Value], "1",
				      "1.5A");
			break;
		case 2:
			set_vif_field(&vif_fields[RP_Value],
				      vif_component_name[RP_Value], "2", "3A");
			break;
		default:
			set_vif_field_itss(&vif_fields[RP_Value],
					   vif_component_name[RP_Value], rp,
					   NULL);
		}
	}

	if (type == SNK)
		set_vif_field_b(
			&vif_fields[Type_C_Supports_VCONN_Powered_Accessory],
			vif_component_name
				[Type_C_Supports_VCONN_Powered_Accessory],
			false);

	set_vif_field_b(&vif_fields[Type_C_Is_VCONN_Powered_Accessory],
			vif_component_name[Type_C_Is_VCONN_Powered_Accessory],
			false);

	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SRC],
			vif_component_name[Type_C_Is_Debug_Target_SRC], true);

	set_vif_field_b(&vif_fields[Type_C_Is_Debug_Target_SNK],
			vif_component_name[Type_C_Is_Debug_Target_SNK], true);

	set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Host],
			vif_component_name[Type_C_Can_Act_As_Host],
			can_act_as_host());

	set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Controller],
			vif_component_name[Type_C_Is_Alt_Mode_Controller],
			is_alt_mode_controller());

	if (can_act_as_device()) {
		set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Device],
				vif_component_name[Type_C_Can_Act_As_Device],
				true);

		if (is_usb_pd_supported() && does_respond_to_discov_sop_ufp())
			set_vif_field_b(
				&vif_fields[Type_C_Is_Alt_Mode_Adapter],
				vif_component_name[Type_C_Is_Alt_Mode_Adapter],
				IS_ENABLED(CONFIG_USB_ALT_MODE_ADAPTER));
	} else {
		set_vif_field_b(&vif_fields[Type_C_Can_Act_As_Device],
				vif_component_name[Type_C_Can_Act_As_Device],
				false);
		set_vif_field_b(&vif_fields[Type_C_Is_Alt_Mode_Adapter],
				vif_component_name[Type_C_Is_Alt_Mode_Adapter],
				false);
	}

	set_vif_field_b(&vif_fields[Modal_Operation_Supported_SOP],
			vif_component_name[Modal_Operation_Supported_SOP],
			is_alt_mode_adapter());

	{
		int ps = POWER_UFP;

		if (IS_ENABLED(CONFIG_BATTERY))
			ps = POWER_BOTH;
#if defined(CONFIG_DEDICATED_CHARGE_PORT_COUNT)
		else if (CONFIG_DEDICATED_CHARGE_PORT_COUNT == 1)
			ps = POWER_EXTERNAL;
		else if (CONFIG_DEDICATED_CHARGE_PORT_COUNT > 1)
			ps = POWER_BOTH;
#endif

		switch (ps) {
		case POWER_EXTERNAL:
			set_vif_field(&vif_fields[Type_C_Power_Source],
				      vif_component_name[Type_C_Power_Source],
				      "0", "Externally Powered");
			break;
		case POWER_UFP:
			set_vif_field(&vif_fields[Type_C_Power_Source],
				      vif_component_name[Type_C_Power_Source],
				      "1", "UFP-powered");
			break;
		case POWER_BOTH:
			set_vif_field(&vif_fields[Type_C_Power_Source],
				      vif_component_name[Type_C_Power_Source],
				      "2", "Both");
			break;
		default:
			set_vif_field_itss(
				&vif_fields[Type_C_Power_Source],
				vif_component_name[Type_C_Power_Source], ps,
				NULL);
		}
	}

	set_vif_field_b(&vif_fields[Type_C_Port_On_Hub],
			vif_component_name[Type_C_Port_On_Hub], false);

	set_vif_field_b(&vif_fields[Type_C_Supports_Audio_Accessory],
			vif_component_name[Type_C_Supports_Audio_Accessory],
			false);

	set_vif_field_b(&vif_fields[Type_C_Sources_VCONN],
			vif_component_name[Type_C_Sources_VCONN],
			IS_ENABLED(CONFIG_USBC_VCONN));
}

static void init_vif_component_usb4_port_fields(struct vif_field_t *vif_fields)
{
	int vi;
	const char *vs;

	if (!is_usb4_supported())
		return;

	set_vif_field_c(&vif_fields[USB4_Port_Header], "USB4\u2122 Port");

	vi = vif_get_max_tbt_speed();
	switch (vi) {
	case 0:
		vs = "Gen 2 (20Gb)";
		break;
	case 1:
		vs = "Gen 3 (40Gb)";
		break;
	default:
		vs = "Undefined";
	}

	set_vif_field_itss(&vif_fields[USB4_Max_Speed],
			   vif_component_name[USB4_Max_Speed], vi, vs);

	set_vif_field_b(&vif_fields[USB4_TBT3_Compatibility_Supported],
			vif_component_name[USB4_TBT3_Compatibility_Supported],
			is_usb4_tbt3_compatible());

	set_vif_field_b(&vif_fields[USB4_PCIe_Tunneling_Supported],
			vif_component_name[USB4_PCIe_Tunneling_Supported],
			is_usb4_pcie_tunneling_supported());
}

/*********************************************************************
 * Init VIF/Component[] USB Data - Upstream Facing Port Fields
 *
 * TODO: Generic todo to fill in additional fields as the need presents
 * itself
 *
 * Fields that are not currently being initialized
 *
 * vif_Component
 *	Device_Contains_Captive_Retimer		booleanFieldType
 *	Device_Truncates_DP_For_tDHPResponse	booleanFieldType
 *	Device_Gen1x1_tLinkTurnaround		numericFieldType
 *	Device_Gen2x1_tLinkTurnaround		numericFieldType
 */
static void
init_vif_component_usb_data_ufp_fields(struct vif_field_t *vif_fields)
{
	/*
	 * TOTO(b:172441959) Adjust the speed based on CONFIG_
	 */
	enum usb_speed ds = USB_GEN11;
	bool supports_usb_data;

	/*
	 * The fields in this section shall be ignored by Testers unless
	 * Connector_Type is set to 1 (Type-B) or 3 (Micro A/B), or
	 * Connector_Type is set to 2 (Type-C) and Type_C_Can_Act_As_Device
	 * is set to YES.
	 *
	 * NOTE: We currently are always a Connector_Type of 2 (Type-C)
	 */
	if (!can_act_as_device())
		return;

	supports_usb_data = does_support_device_usb_data();
	set_vif_field_b(&vif_fields[Device_Supports_USB_Data],
			vif_component_name[Device_Supports_USB_Data],
			supports_usb_data);

	if (supports_usb_data) {
		switch (ds) {
		case USB_2:
			set_vif_field_itss(&vif_fields[Device_Speed],
					   vif_component_name[Device_Speed],
					   USB_2, "USB 2");
			break;
		case USB_GEN11:
			set_vif_field_itss(&vif_fields[Device_Speed],
					   vif_component_name[Device_Speed],
					   USB_GEN11, "USB 3.2 Gen 1x1");
			break;
		case USB_GEN21:
			set_vif_field_itss(&vif_fields[Device_Speed],
					   vif_component_name[Device_Speed],
					   USB_GEN21, "USB 3.2 Gen 2x1");
			break;
		case USB_GEN12:
			set_vif_field_itss(&vif_fields[Device_Speed],
					   vif_component_name[Device_Speed],
					   USB_GEN12, "USB 3.2 Gen 1x2");
			break;
		case USB_GEN22:
			set_vif_field_itss(&vif_fields[Device_Speed],
					   vif_component_name[Device_Speed],
					   USB_GEN22, "USB 3.2 Gen 2x2");
			break;
		}
	}
}

/*********************************************************************
 * Init VIF/Component[] USB Data - Downstream Facing Port Fields
 *
 * TODO: Generic todo to fill in additional fields as the need presents
 * itself
 *
 * Fields that are not currently being initialized
 *
 * vif_Component
 *	Hub_Port_Number				numericFieldType
 *	Host_Truncates_DP_For_tDHPResponse	booleanFieldType
 *	Host_Gen1x1_tLinkTurnaround		numericFieldType
 *	Host_Gen2x1_tLinkTurnaround		numericFieldType
 *	Host_Suspend_Supported			booleanFieldType
 */
static void
init_vif_component_usb_data_dfp_fields(struct vif_field_t *vif_fields)
{
	/*
	 * TOTO(b:172438944) Adjust the speed based on CONFIG_
	 */
	enum usb_speed ds = USB_GEN11;
	bool supports_usb_data;
	bool is_dfp_on_hub;

	/*
	 * The fields in this section shall be ignored by Testers unless
	 * Connector_Type is set to 0 (Type-A), or
	 * COnnector Type is set to 3 (Micro A/B); or
	 * Connector_Type is set to 2 (Type-C) and Type_C_Can_Act_As_Host
	 * is set to YES
	 *
	 * NOTE: We currently are always a Connector_Type of 2 (Type-C)
	 */
	if (!can_act_as_host())
		return;

	supports_usb_data = does_support_host_usb_data();
	set_vif_field_b(&vif_fields[Host_Supports_USB_Data],
			vif_component_name[Host_Supports_USB_Data],
			supports_usb_data);

	if (supports_usb_data) {
		switch (ds) {
		case USB_2:
			set_vif_field_itss(&vif_fields[Host_Speed],
					   vif_component_name[Host_Speed],
					   USB_2, "USB 2");
			break;
		case USB_GEN11:
			set_vif_field_itss(&vif_fields[Host_Speed],
					   vif_component_name[Host_Speed],
					   USB_GEN11, "USB 3.2 Gen 1x1");
			break;
		case USB_GEN21:
			set_vif_field_itss(&vif_fields[Host_Speed],
					   vif_component_name[Host_Speed],
					   USB_GEN21, "USB 3.2 Gen 2x1");
			break;
		case USB_GEN12:
			set_vif_field_itss(&vif_fields[Host_Speed],
					   vif_component_name[Host_Speed],
					   USB_GEN12, "USB 3.2 Gen 1x2");
			break;
		case USB_GEN22:
			set_vif_field_itss(&vif_fields[Host_Speed],
					   vif_component_name[Host_Speed],
					   USB_GEN22, "USB 3.2 Gen 2x2");
			break;
		}

		if (!get_vif_field_tag_bool(&vif_fields[Type_C_Port_On_Hub],
					    &is_dfp_on_hub))
			is_dfp_on_hub = false;

		set_vif_field_b(&vif_fields[Is_DFP_On_Hub],
				vif_component_name[Is_DFP_On_Hub],
				is_dfp_on_hub);

		set_vif_field_b(
			&vif_fields[Host_Contains_Captive_Retimer],
			vif_component_name[Host_Contains_Captive_Retimer],
			false);

		set_vif_field_b(&vif_fields[Host_Is_Embedded],
				vif_component_name[Host_Is_Embedded], false);
	}
}

/*********************************************************************
 * Init VIF/Component[] PD Source Fields
 */
static int
init_vif_component_pd_source_fields(struct vif_field_t *vif_fields,
				    struct vif_srcPdoList_t *comp_src_pdo_list,
				    int32_t *src_max_power, enum dtype type)
{
	if (type == DRP || type == SRC) {
		int i;
		char str[40];

		set_vif_field_b(&vif_fields[EPR_Supported_As_Src],
				vif_component_name[EPR_Supported_As_Src],
				false);

		/* Source PDOs */
		for (i = 0; i < src_pdo_cnt; i++) {
			int32_t pwr;

			pwr = init_vif_src_pdo(&comp_src_pdo_list[i],
					       src_pdo[i]);
			if (pwr < 0) {
				fprintf(stderr, "ERROR: Setting SRC PDO.\n");
				return 1;
			}

			if (pwr > *src_max_power)
				*src_max_power = pwr;
		}

		sprintf(str, "%d mW", *src_max_power);
		set_vif_field_itss(&vif_fields[PD_Power_As_Source],
				   vif_component_name[PD_Power_As_Source],
				   *src_max_power, str);
	}

	if (type == DRP || type == SRC)
		set_vif_field_b(&vif_fields[USB_Suspend_May_Be_Cleared],
				vif_component_name[USB_Suspend_May_Be_Cleared],
				false);

	if (type == DRP || type == SRC)
		set_vif_field_b(&vif_fields[Sends_Pings],
				vif_component_name[Sends_Pings], false);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) && type == DRP &&
	    IS_ENABLED(CONFIG_USB_PD_FRS))
		set_vif_field(
			&vif_fields
				[FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			vif_component_name
				[FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			"3", "3A @ 5V");
	else
		set_vif_field(
			&vif_fields
				[FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			vif_component_name
				[FR_Swap_Type_C_Current_Capability_As_Initial_Sink],
			"0", "FR_Swap not supported");

	if (IS_ENABLED(CONFIG_USB_PD_REV30) || IS_ENABLED(CONFIG_USB_PRL_SM))
		set_vif_field_b(&vif_fields[Master_Port],
				vif_component_name[Master_Port], false);

	if (type == DRP || type == SRC)
		set_vif_field_itss(&vif_fields[Num_Src_PDOs],
				   vif_component_name[Num_Src_PDOs],
				   src_pdo_cnt, NULL);

	if (type == DRP || type == SRC) {
		if (IS_ENABLED(CONFIG_USBC_OCP)) {
			int resp = 0;

			set_vif_field_b(&vif_fields[PD_OC_Protection],
					vif_component_name[PD_OC_Protection],
					true);

			switch (resp) {
			case 0:
				set_vif_field(&vif_fields[PD_OCP_Method],
					      vif_component_name[PD_OCP_Method],
					      "0", "Over-Current Response");
				break;
			case 1:
				set_vif_field(&vif_fields[PD_OCP_Method],
					      vif_component_name[PD_OCP_Method],
					      "1", "Under-Voltage Response");
				break;
			case 2:
				set_vif_field(&vif_fields[PD_OCP_Method],
					      vif_component_name[PD_OCP_Method],
					      "2", "Both");
				break;
			default:
				set_vif_field_itss(
					&vif_fields[PD_OCP_Method],
					vif_component_name[PD_OCP_Method], resp,
					NULL);
			}
		} else {
			set_vif_field_b(&vif_fields[PD_OC_Protection],
					vif_component_name[PD_OC_Protection],
					false);
		}
	}

	return 0;
}

/*********************************************************************
 * Init VIF/Component[] PD Sink Fields
 */
static int
init_vif_component_pd_sink_fields(struct vif_field_t *vif_fields,
				  struct vif_snkPdoList_t *comp_snk_pdo_list,
				  enum dtype type)
{
	int i;
	int32_t snk_max_power = 0;
	char str[40];

	if (!IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) || type == SRC)
		return 0;

	set_vif_field_c(&vif_fields[PD_Sink_Header], "PD Sink");

	set_vif_field_b(&vif_fields[EPR_Supported_As_Snk],
			vif_component_name[EPR_Supported_As_Snk], false);

	/* Sink PDOs */
	for (i = 0; i < pd_snk_pdo_cnt; i++) {
		int32_t pwr;

		pwr = init_vif_snk_pdo(&comp_snk_pdo_list[i], pd_snk_pdo[i]);
		if (pwr < 0) {
			fprintf(stderr, "ERROR: Setting SNK PDO.\n");
			return 1;
		}

		if (pwr > snk_max_power)
			snk_max_power = pwr;
	}

	sprintf(str, "%d mW", snk_max_power);
	set_vif_field_itss(&vif_fields[PD_Power_As_Sink],
			   vif_component_name[PD_Power_As_Sink], snk_max_power,
			   str);

	set_vif_field_b(&vif_fields[No_USB_Suspend_May_Be_Set],
			vif_component_name[No_USB_Suspend_May_Be_Set], true);

	set_vif_field_b(&vif_fields[GiveBack_May_Be_Set],
			vif_component_name[GiveBack_May_Be_Set],
			IS_ENABLED(CONFIG_USB_PD_GIVE_BACK));

	set_vif_field_b(&vif_fields[Higher_Capability_Set],
			vif_component_name[Higher_Capability_Set], false);

	set_vif_field(
		&vif_fields[FR_Swap_Reqd_Type_C_Current_As_Initial_Source],
		vif_component_name[FR_Swap_Reqd_Type_C_Current_As_Initial_Source],
		"0", "FR_Swap not supported");

	set_vif_field_itss(&vif_fields[Num_Snk_PDOs],
			   vif_component_name[Num_Snk_PDOs], pd_snk_pdo_cnt,
			   NULL);

	return 0;
}

/*********************************************************************
 * Init VIF/Component[] PD Dual Role Fields
 */
static void
init_vif_component_pd_dual_role_fields(struct vif_field_t *vif_fields,
				       enum dtype type)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) || type != DRP)
		return;

	set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Src],
			vif_component_name[Accepts_PR_Swap_As_Src], true);

	set_vif_field_b(&vif_fields[Accepts_PR_Swap_As_Snk],
			vif_component_name[Accepts_PR_Swap_As_Snk], true);

	set_vif_field_b(&vif_fields[Requests_PR_Swap_As_Src],
			vif_component_name[Requests_PR_Swap_As_Src], true);

	set_vif_field_b(&vif_fields[Requests_PR_Swap_As_Snk],
			vif_component_name[Requests_PR_Swap_As_Snk], true);

	set_vif_field_b(&vif_fields[FR_Swap_Supported_As_Initial_Sink],
			vif_component_name[FR_Swap_Supported_As_Initial_Sink],
			IS_ENABLED(CONFIG_USB_PD_FRS));
}

/*********************************************************************
 * Init VIF/Component[] SOP Discovery Fields
 *
 * TODO: Generic todo to fill in additional fields as the need presents
 * itself
 *
 * Fields that are not currently being initialized
 *
 * vif_Component
 *	Product_Type_UFP_SOP			numericFieldType
 *	Product_Type_DFP_SOP			numericFieldType
 *	Modal_Operation_Supported_SOP		booleanFieldType
 *	Num_SVIDs_Min_SOP			numericFieldType
 *	Num_SVIDs_Max_SOP			numericFieldType
 *	SVID_Fixed_SOP				booleanFieldType
 */
static void
init_vif_component_sop_discovery_fields(struct vif_field_t *vif_fields)
{
	char hex_str[10];

	/*
	 * The fields in this section shall be ignored by Testers unless at
	 * least one of Responds_To_Discov_SOP_UFP and
	 * Responds_To_Discov_SOP_DFP is set to YES.
	 */
	if (!does_respond_to_discov_sop_ufp() &&
	    !does_respond_to_discov_sop_dfp())
		return;

	set_vif_field(&vif_fields[XID_SOP], vif_component_name[XID_SOP], "0",
		      NULL);

	set_vif_field_b(&vif_fields[Data_Capable_As_USB_Host_SOP],
			vif_component_name[Data_Capable_As_USB_Host_SOP],
			can_act_as_host());

	set_vif_field_b(&vif_fields[Data_Capable_As_USB_Device_SOP],
			vif_component_name[Data_Capable_As_USB_Device_SOP],
			can_act_as_device());

	if (does_respond_to_discov_sop_dfp() &&
	    IS_ENABLED(CONFIG_USB_PD_REV30)) {
#if defined(CONFIG_USB_PD_PORT_LABEL)
		set_vif_field_stis(&vif_fields[DFP_VDO_Port_Number],
				   vif_component_name[DFP_VDO_Port_Number],
				   NULL, CONFIG_USB_PD_PORT_LABEL);
#else
		set_vif_field_itss(&vif_fields[DFP_VDO_Port_Number],
				   vif_component_name[DFP_VDO_Port_Number],
				   component_index, NULL);
#endif
	}

	sprintf(hex_str, "%04X", USB_VID_GOOGLE);
	set_vif_field_itss(&vif_fields[USB_VID_SOP],
			   vif_component_name[USB_VID_SOP], USB_VID_GOOGLE,
			   hex_str);

#if defined(CONFIG_USB_PID)
	sprintf(hex_str, "%04X", CONFIG_USB_PID);
	set_vif_field_itss(&vif_fields[PID_SOP], vif_component_name[PID_SOP],
			   CONFIG_USB_PID, hex_str);
#else
	sprintf(hex_str, "%04X", DEFAULT_MISSING_PID);
	set_vif_field_itss(&vif_fields[PID_SOP], vif_component_name[PID_SOP],
			   DEFAULT_MISSING_PID, hex_str);
#endif

#if defined(CONFIG_USB_BCD_DEV)
	sprintf(hex_str, "%04X", CONFIG_USB_BCD_DEV);
	set_vif_field_itss(&vif_fields[bcdDevice_SOP],
			   vif_component_name[bcdDevice_SOP],
			   CONFIG_USB_BCD_DEV, hex_str);
#else
	sprintf(hex_str, "%04X", DEFAULT_MISSING_BCD_DEV);
	set_vif_field_itss(&vif_fields[bcdDevice_SOP],
			   vif_component_name[bcdDevice_SOP],
			   DEFAULT_MISSING_BCD_DEV, hex_str);
#endif
}

/*********************************************************************
 * Init VIF/Component[] Battery Charging 1.2 Fields
 */
static void init_vif_component_bc_1_2_fields(struct vif_field_t *vif_fields,
					     enum bc_1_2_support bc_support)
{
	if (bc_support == BC_1_2_SUPPORT_CHARGING_PORT ||
	    bc_support == BC_1_2_SUPPORT_BOTH)
		set_vif_field(&vif_fields[BC_1_2_Charging_Port_Type],
			      vif_component_name[BC_1_2_Charging_Port_Type],
			      "1", "CDP");
}

/*********************************************************************
 * Init VIF/Component[] Product Power Fields
 *
 * TODO: Generic todo to fill in additional fields as the need presents
 * itself
 *
 * Fields that are not currently being initialized
 *
 * vif_Component
 *	Port_Source_Power_Gang			nonEmptyString
 *	Port_Source_Power_Gang_Max_Power	numericFieldType
 */
static void
init_vif_component_product_power_fields(struct vif_field_t *vif_fields,
					int32_t src_max_power, enum dtype type)
{
	if (type == DRP || type == SRC) {
		char str[14];

		sprintf(str, "%d mW", src_max_power);
		set_vif_field_itss(
			&vif_fields[Product_Total_Source_Power_mW],
			vif_component_name[Product_Total_Source_Power_mW],
			src_max_power, str);
	}

	if (type == DRP || type == SRC)
		set_vif_field(&vif_fields[Port_Source_Power_Type],
			      vif_component_name[Port_Source_Power_Type], "0",
			      "Assured");
}

static void init_remarks(struct vif_t *vif)
{
	struct vif_field_t *vif_fields;
	int max_component_index = board_get_usb_pd_port_count();

	for (int c = 0; c < max_component_index; ++c) {
		vif_fields = vif->Component[c].vif_field;

		set_vif_field_c(&vif_fields[Component_Header], "Component");
		set_vif_field_c(&vif_fields[General_PD_Header], "General PD");
		set_vif_field_c(&vif_fields[PD_Capabilities_Header],
				"PD Capabilities");

		set_vif_field_c(&vif_fields[USB_Type_C_Header],
				"USB Type-C\u00ae");

		set_vif_field_c(&vif_fields[Product_Power_Header],
				"Product Power");

		set_vif_field_c(&vif_fields[USB_Host_Header], "USB Host");

		set_vif_field_c(&vif_fields[BC_1_2_Header],
				"Battery Charging 1.2");

		set_vif_field_c(&vif_fields[PD_Source_Header], "PD Source");

		set_vif_field_c(&vif_fields[Dual_Role_Header], "Dual Role");

		set_vif_field_c(&vif_fields[SOP_Discover_ID_Header],
				"SOP Discover ID");
	}
}

static int gen_vif(const char *board, struct vif_t *vif)
{
	int max_component_index = board_get_usb_pd_port_count();

	/*********************************************************************
	 * Initialize the vif structure
	 */
	init_vif_fields(vif->vif_field, vif->vif_app_field, board);

	for (component_index = 0; component_index < max_component_index;
	     component_index++) {
		int override_value;
		bool was_overridden;
		enum dtype type;
		int32_t src_max_power = 0;
		enum bc_1_2_support bc_support = BC_1_2_SUPPORT_NONE;

		/* Determine if we are DRP, SRC or SNK */
		was_overridden = get_vif_field_tag_number(
			&vif->Component[component_index]
				 .vif_field[Type_C_State_Machine],
			&override_value);
		if (was_overridden) {
			switch (override_value) {
			case SRC:
			case SNK:
			case DRP:
				type = (enum dtype)override_value;
				break;
			default:
				was_overridden = false;
			}
		}
		if (!was_overridden) {
			was_overridden = get_vif_field_tag_number(
				&vif->Component[component_index]
					 .vif_field[PD_Port_Type],
				&override_value);
			if (was_overridden) {
				switch (override_value) {
				case PORT_CONSUMER_ONLY: /* SNK */
					type = SNK;
					break;
				case PORT_PROVIDER_ONLY: /* SRC */
					type = SRC;
					break;
				case PORT_DRP: /* DRP */
					type = DRP;
					break;
				default:
					was_overridden = false;
				}
			}
		}
		if (!was_overridden) {
			if (is_drp())
				type = DRP;
			else if (is_src() && is_snk())
				/*
				 * No DRP with SRC and SNK PDOs detected. So
				 * ignore.  ie. Twinkie
				 */
				return 0;
			else if (is_src())
				type = SRC;
			else if (is_snk())
				type = SNK;
			else
				return 1;
		}

		init_vif_component_fields(
			vif->Component[component_index].vif_field, &bc_support,
			type);

		init_vif_component_general_pd_fields(
			vif->Component[component_index].vif_field, type);

		init_vif_component_sop_capabilities_fields(
			vif->Component[component_index].vif_field);

		init_vif_component_usb_type_c_fields(
			vif->Component[component_index].vif_field, type);

		init_vif_component_usb4_port_fields(
			vif->Component[component_index].vif_field);

		init_vif_component_usb_data_ufp_fields(
			vif->Component[component_index].vif_field);

		init_vif_component_usb_data_dfp_fields(
			vif->Component[component_index].vif_field);

		if (init_vif_component_pd_source_fields(
			    vif->Component[component_index].vif_field,
			    vif->Component[component_index].SrcPdoList,
			    &src_max_power, type))
			return 1;

		if (init_vif_component_pd_sink_fields(
			    vif->Component[component_index].vif_field,
			    vif->Component[component_index].SnkPdoList, type))
			return 1;

		init_vif_component_pd_dual_role_fields(
			vif->Component[component_index].vif_field, type);

		init_vif_component_sop_discovery_fields(
			vif->Component[component_index].vif_field);

		init_vif_component_bc_1_2_fields(
			vif->Component[component_index].vif_field, bc_support);

		init_vif_component_product_power_fields(
			vif->Component[component_index].vif_field,
			src_max_power, type);
	}

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
	bool do_config_init = true;
	DIR *vifdir;
	char *name;
	int name_size;
	const char *const short_opt = "hb:o:nv:";
	const struct option long_opts[] = {
		{ "help", 0, NULL, 'h' }, { "board", 1, NULL, 'b' },
		{ "out", 1, NULL, 'o' },  { "no-config", 0, NULL, 'n' },
		{ "over", 1, NULL, 'v' }, { NULL }
	};

	/* Clear the VIF structure */
	memset(&vif, 0, sizeof(struct vif_t));

	do {
		nopt = getopt_long(argc, argv, short_opt, long_opts, NULL);
		switch (nopt) {
		case 'h': /* -h or --help */
			printf("USAGE: genvif -b|--board <board name>\n"
			       "              -o|--out <out directory>\n"
			       "              [-n|--no-config]\n"
			       "              [-v|--over <override XML file>]\n");
			return 1;

		case 'b': /* -b or --board */
			board = optarg;
			break;

		case 'o': /* -o or --out */
			out = optarg;
			break;

		case 'n': /* -n or --no-config */
			do_config_init = false;
			break;

		case 'v': /* -v or --over */
			/* Handle overrides */
			if (override_gen_vif(optarg, &vif))
				return 1;
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

	init_remarks(&vif);

	/* Finish CONFIG initialization file */
	if (do_config_init) {
		ret = gen_vif(board, &vif);
		if (ret)
			return 1;
	}

	name_size = asprintf(&name, "%s/%s_vif.xml", out, board);
	if (name_size < 0) {
		fprintf(stderr, "ERROR: Out of memory.\n");
		return 1;
	}

	/* Format the structure in XML and output it to file */
	ret = vif_output_xml(name, &vif);

	free(name);
	return ret;
}
