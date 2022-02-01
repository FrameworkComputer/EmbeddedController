/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __GENVIF_H__
#define __GENVIF_H__

#define MAX_NUM_CABLE_SVIDS 10
#define MAX_NUM_CABLE_SVID_MODES 10
#define MAX_NUM_COMPONENTS 10
#define MAX_NUM_PCIE_ENDPOINTS 10
#define MAX_NUM_SNK_PDOS 10
#define MAX_NUM_SRC_PDOS 10
#define MAX_NUM_SOP_SVIDS 10
#define MAX_NUM_SOP_SVID_MODES 10
#define MAX_NUM_USB4_ROUTERS 16

struct vif_field_t {
	const char *name;
	char *tag_value;
	char *str_value;
};

/* 3.2.15.2 Cable SVID Modes */

enum vif_cableSVIDModeList_indexes {
	SVID_Mode_Enter,			/* booleanFieldType */
	SVID_Mode_Recog_Mask,			/* numericFieldType */
	SVID_Mode_Recog_Value,			/* numericFieldType */
	CableSVID_Mode_Indexes
};

struct vif_cableSVIDModeList_t {
	struct vif_field_t		vif_field[CableSVID_Mode_Indexes];
};

/* 3.2.15.1 Cable SVIDs */

enum vif_cableSVIDList_indexes {
	SVID,					/* numericFieldType */
	SVID_Num_Modes_Min,			/* numericFieldType */
	SVID_Num_Modes_Max,			/* numericFieldType */
	SVID_Modes_Fixed,			/* booleanFieldType */
	CableSVID_Indexes
};

struct vif_cableSVIDList_t {
	struct vif_field_t		vif_field[CableSVID_Indexes];

	struct vif_cableSVIDModeList_t
			CableSVIDModeList[MAX_NUM_CABLE_SVID_MODES];
};

/* 3.2.12.2 SOP SVID Modes */

enum vif_sopSVIDModeList_indexes {
	SVID_Mode_Enter_SOP,			/* booleanFieldType */
	SVID_Mode_Recog_Mask_SOP,		/* numericFieldType */
	SVID_Mode_Recog_Value_SOP,		/* numericFieldType */
	SopSVID_Mode_Indexes
};

struct vif_sopSVIDModeList_t {
	struct vif_field_t		vif_field[SopSVID_Mode_Indexes];
};

/* 3.2.12.1 SOP SVIDs */

enum vif_sopSVIDList_indexes {
	SVID_SOP,				/* numericFieldType */
	SVID_Num_Modes_Min_SOP,			/* numericFieldType */
	SVID_Num_Modes_Max_SOP,			/* numericFieldType */
	SVID_Modes_Fixed_SOP,			/* booleanFieldType */
	SopSVID_Indexes
};

struct vif_sopSVIDList_t {
	struct vif_field_t		vif_field[SopSVID_Indexes];

	struct vif_sopSVIDModeList_t
			SOPSVIDModeList[MAX_NUM_SOP_SVID_MODES];
};

/* 3.2.10.1 Sink PDOs */

enum vif_snkPdoList_indexes {
	Snk_PDO_Supply_Type,			/* numericFieldType */
	Snk_PDO_APDO_Type,			/* numericFieldType */
	Snk_PDO_Voltage,			/* numericFieldType */
	Snk_PDO_PDP_Rating,			/* numericFieldType */
	Snk_PDO_Op_Power,			/* numericFieldType */
	Snk_PDO_Min_Voltage,			/* numericFieldType */
	Snk_PDO_Max_Voltage,			/* numericFieldType */
	Snk_PDO_Op_Current,			/* numericFieldType */
	Snk_PDO_Indexes
};

struct vif_snkPdoList_t {
	struct vif_field_t		vif_field[Snk_PDO_Indexes];
};

/* 3.2.9.1 Source PDOs */

enum vif_srcPdoList_indexes {
	Src_PDO_Supply_Type,			/* numericFieldType */
	Src_PDO_APDO_Type,			/* numericFieldType */
	Src_PDO_Peak_Current,			/* numericFieldType */
	Src_PDO_Voltage,			/* numericFieldType */
	Src_PDO_Max_Current,			/* numericFieldType */
	Src_PDO_Min_Voltage,			/* numericFieldType */
	Src_PDO_Max_Voltage,			/* numericFieldType */
	Src_PDO_Max_Power,			/* numericFieldType */
	Src_PD_OCP_OC_Debounce,			/* numericFieldType */
	Src_PD_OCP_OC_Threshold,		/* numericFieldType */
	Src_PD_OCP_UV_Debounce,			/* numericFieldType */
	Src_PD_OCP_UV_Threshold_Type,		/* numericFieldType */
	Src_PD_OCP_UV_Threshold,		/* numericFieldType */
	Src_PDO_Indexes
};

struct vif_srcPdoList_t {
	struct vif_field_t		vif_field[Src_PDO_Indexes];
};

/* 3.2.2.1.3 PCIe Endpoint Fields */

enum vif_PCIeEndpointListType_indexes {
	USB4_PCIe_Endpoint_Vendor_ID,		/* numericFieldType */
	USB4_PCIe_Endpoint_Device_ID,		/* numericFieldType */
	USB4_PCIe_Endpoint_Class_Code,		/* numericFieldType */
	PCIe_Endpoint_Indexes
};

struct vif_PCIeEndpointListType_t {
	struct vif_field_t		vif_field[PCIe_Endpoint_Indexes];
};

/* 3.2.2.1.2 USB4 Router Fields */

enum vif_Usb4RouterListType_indexes {
	USB4_Router_ID,				/* numericFieldType */
	USB4_Silicon_VID,			/* numericFieldType */
	USB4_Num_Lane_Adapters,			/* numericFieldType */
	USB4_Num_USB3_DN_Adapters,		/* numericFieldType */
	USB4_Num_DP_IN_Adapters,		/* numericFieldType */
	USB4_Num_DP_OUT_Adapters,		/* numericFieldType */
	USB4_Num_PCIe_DN_Adapters,		/* numericFieldType */
	USB4_TBT3_Not_Supported,		/* numericFieldType */
	USB4_PCIe_Wake_Supported,		/* booleanFieldType */
	USB4_USB3_Wake_Supported,		/* booleanFieldType */
	USB4_Num_Unused_Adapters,		/* numericFieldType */
	USB4_TBT3_VID,				/* numericFieldType */
	USB4_PCIe_Switch_Vendor_ID,		/* numericFieldType */
	USB4_PCIe_Switch_Device_ID,		/* numericFieldType */
	USB4_Num_PCIe_Endpoints,		/* numericFieldType */
	USB4_Router_Indexes
};

struct vif_Usb4RouterListType_t {
	struct vif_field_t		vif_field[USB4_Router_Indexes];

	struct vif_PCIeEndpointListType_t
			PCIeEndpointList[MAX_NUM_PCIE_ENDPOINTS];
};

/* 3.2.3 Component Fields */

enum vif_Component_indexes {
	Component_Header,			/* comment */
	Port_Label,				/* nonEmptyString */
	Connector_Type,				/* numericFieldType */
	USB4_Supported,				/* booleanFieldType */
	USB4_Router_Index,			/* numericFieldType */
	USB_PD_Support,				/* booleanFieldType */
	PD_Port_Type,				/* numericFieldType */
	Type_C_State_Machine,			/* numericFieldType */
	Port_Battery_Powered,			/* booleanFieldType */
	BC_1_2_Support,				/* numericFieldType */
	Captive_Cable,				/* booleanFieldType */
	Captive_Cable_Is_eMarked,		/* booleanFieldType */

	/* 3.2.4 General PD Fields */
	General_PD_Header,			/* comment */
	PD_Spec_Revision_Major,			/* numericFieldType */
	PD_Spec_Revision_Minor,			/* numericFieldType */
	PD_Spec_Version_Major,			/* numericFieldType */
	PD_Spec_Version_Minor,			/* numericFieldType */
	PD_Specification_Revision,		/* numericFieldType */

	/* 3.2.4.1 SOP* Capabilities */
	SOP_Capable,				/* booleanFieldType */
	SOP_P_Capable,				/* booleanFieldType */
	SOP_PP_Capable,				/* booleanFieldType */
	SOP_P_Debug_Capable,			/* booleanFieldType */
	SOP_PP_Debug_Capable,			/* booleanFieldType */

	Manufacturer_Info_Supported_Port,	/* booleanFieldType */
	Manufacturer_Info_VID_Port,		/* numericFieldType */
	Manufacturer_Info_PID_Port,		/* numericFieldType */
	Chunking_Implemented_SOP,		/* booleanFieldType */
	Unchunked_Extended_Messages_Supported,	/* booleanFieldType */
	Security_Msgs_Supported_SOP,		/* booleanFieldType */
	Unconstrained_Power,			/* booleanFieldType */
	Num_Fixed_Batteries,			/* numericFieldType */
	Num_Swappable_Battery_Slots,		/* numericFieldType */
	ID_Header_Connector_Type_SOP,		/* numericFieldType */

	/* 3.2.4 General PD Fields */
	PD_Capabilities_Header,			/* comment */
	USB_Comms_Capable,			/* booleanFieldType */
	DR_Swap_To_DFP_Supported,		/* booleanFieldType */
	DR_Swap_To_UFP_Supported,		/* booleanFieldType */
	VCONN_Swap_To_On_Supported,		/* booleanFieldType */
	VCONN_Swap_To_Off_Supported,		/* booleanFieldType */
	Responds_To_Discov_SOP_UFP,		/* booleanFieldType */
	Responds_To_Discov_SOP_DFP,		/* booleanFieldType */
	Attempts_Discov_SOP,			/* booleanFieldType */
	Power_Interruption_Available,		/* numericFieldType */
	Data_Reset_Supported,			/* booleanFieldType */
	Enter_USB_Supported,			/* booleanFieldType */

	/* 3.2.5 USB Type-C Fields */
	USB_Type_C_Header,			/* comment */
	Type_C_Can_Act_As_Host,			/* booleanFieldType */
	Type_C_Can_Act_As_Device,		/* booleanFieldType */

	/* 3.2.5 USB Type-C Fields */
	Type_C_Implements_Try_SRC,		/* booleanFieldType */
	Type_C_Implements_Try_SNK,		/* booleanFieldType */
	Type_C_Supports_Audio_Accessory,	/* booleanFieldType */
	Type_C_Is_VCONN_Powered_Accessory,	/* booleanFieldType */
	Type_C_Is_Debug_Target_SRC,		/* booleanFieldType */
	Type_C_Is_Debug_Target_SNK,		/* booleanFieldType */
	RP_Value,				/* numericFieldType */
	Type_C_Supports_VCONN_Powered_Accessory,/* booleanFieldType */
	Type_C_Port_On_Hub,			/* booleanFieldType */
	Type_C_Power_Source,			/* numericFieldType */
	Type_C_Sources_VCONN,			/* booleanFieldType */
	Type_C_Is_Alt_Mode_Controller,		/* booleanFieldType */
	Type_C_Is_Alt_Mode_Adapter,		/* booleanFieldType */

	/* 3.2.6 USB4 Port Fields (missing from output) */
	USB4_Port_Header,			/* comment */
	USB4_Lane_0_Adapter,			/* numericFieldType */
	USB4_Max_Speed,				/* numericFieldType */
	USB4_DFP_Supported,			/* booleanFieldType */
	USB4_UFP_Supported,			/* booleanFieldType */
	USB4_USB3_Tunneling_Supported,		/* booleanFieldType */
	USB4_DP_Tunneling_Supported,		/* booleanFieldType */
	USB4_PCIe_Tunneling_Supported,		/* booleanFieldType */
	USB4_TBT3_Compatibility_Supported,	/* booleanFieldType */
	USB4_CL1_State_Supported,		/* booleanFieldType */
	USB4_CL2_State_Supported,		/* booleanFieldType */
	USB4_Num_Retimers,			/* numericFieldType */
	USB4_DP_Bit_Rate,			/* numericFieldType */
	USB4_Num_DP_Lanes,			/* numericFieldType */

	/* 3.2.7 USB Data - Upstream Facing Port Fields */
	Device_Supports_USB_Data,		/* booleanFieldType */
	Device_Speed,				/* numericFieldType */
	Device_Contains_Captive_Retimer,	/* booleanFieldType */
	Device_Truncates_DP_For_tDHPResponse,	/* booleanFieldType */
	Device_Gen1x1_tLinkTurnaround,		/* numericFieldType */
	Device_Gen2x1_tLinkTurnaround,		/* numericFieldType */

	/* 3.2.19 Product Power Fields */
	Product_Power_Header,			/* comment */
	Product_Total_Source_Power_mW,		/* numericFieldType */
	Port_Source_Power_Type,			/* numericFieldType */
	Port_Source_Power_Gang,			/* nonEmptyString */
	Port_Source_Power_Gang_Max_Power,	/* numericFieldType */

	/* 3.2.8 USB Data - Downstream Facing Port Fields */
	USB_Host_Header,			/* comment */
	Host_Supports_USB_Data,			/* booleanFieldType */
	Host_Speed,				/* numericFieldType */
	Host_Contains_Captive_Retimer,		/* booleanFieldType */
	Host_Truncates_DP_For_tDHPResponse,	/* booleanFieldType */
	Host_Gen1x1_tLinkTurnaround,		/* numericFieldType */
	Host_Gen2x1_tLinkTurnaround,		/* numericFieldType */
	Host_Is_Embedded,			/* booleanFieldType */
	Host_Suspend_Supported,			/* booleanFieldType */
	Is_DFP_On_Hub,				/* booleanFieldType */
	Hub_Port_Number,			/* numericFieldType */

	/* 3.2.14 Battery Charging 1.2 Fields */
	BC_1_2_Header,				/* comment */
	BC_1_2_Charging_Port_Type,		/* numericFieldType */

	/* 3.2.9 PD Source Fields */
	PD_Source_Header,			/* comment */
	PD_Power_As_Source,			/* numericFieldType */
	EPR_Supported_As_Src,			/* booleanFieldType */
	USB_Suspend_May_Be_Cleared,		/* booleanFieldType */
	Sends_Pings,				/* booleanFieldType */
	FR_Swap_Type_C_Current_Capability_As_Initial_Sink,/* numericFieldType */
	Master_Port,				/* booleanFieldType */
	Num_Src_PDOs,				/* numericFieldType */
	PD_OC_Protection,			/* booleanFieldType */
	PD_OCP_Method,				/* numericFieldType */

	/* insert: SrcPdoList */

	/* 3.2.10 PD Sink Fields */
	PD_Sink_Header,				/* comment */
	PD_Power_As_Sink,			/* numericFieldType */
	EPR_Supported_As_Snk,			/* booleanFieldType */
	No_USB_Suspend_May_Be_Set,		/* booleanFieldType */
	GiveBack_May_Be_Set,			/* booleanFieldType */
	Higher_Capability_Set,			/* booleanFieldType */
	FR_Swap_Reqd_Type_C_Current_As_Initial_Source,/* numericFieldType */
	Num_Snk_PDOs,				/* numericFieldType */

	/* insert: SnkPdoList */

	/* 3.2.11 PD Dual Role Fields */
	Dual_Role_Header,			/* comment */
	Accepts_PR_Swap_As_Src,			/* booleanFieldType */
	Accepts_PR_Swap_As_Snk,			/* booleanFieldType */
	Requests_PR_Swap_As_Src,		/* booleanFieldType */
	Requests_PR_Swap_As_Snk,		/* booleanFieldType */
	FR_Swap_Supported_As_Initial_Sink,	/* booleanFieldType */

	/* 3.2.12 SOP Discover ID Fields */
	SOP_Discover_ID_Header,			/* comment */
	XID_SOP,				/* numericFieldType */
	Data_Capable_As_USB_Host_SOP,		/* booleanFieldType */
	Data_Capable_As_USB_Device_SOP,		/* booleanFieldType */
	Product_Type_UFP_SOP,			/* numericFieldType */
	Product_Type_DFP_SOP,			/* numericFieldType */
	DFP_VDO_Port_Number,			/* numericFieldType */
	Modal_Operation_Supported_SOP,		/* booleanFieldType */
	USB_VID_SOP,				/* numericFieldType */
	PID_SOP,				/* numericFieldType */
	bcdDevice_SOP,				/* numericFieldType */
	Num_SVIDs_Min_SOP,			/* numericFieldType */
	Num_SVIDs_Max_SOP,			/* numericFieldType */
	SVID_Fixed_SOP,				/* booleanFieldType */

	/* 3.2.13 Alternate Mode Adapter (AMA) Fields */
	AMA_HW_Vers,				/* numericFieldType */
	AMA_FW_Vers,				/* numericFieldType */
	AMA_VCONN_Power,			/* booleanFieldType */
	AMA_VCONN_Reqd,				/* booleanFieldType */
	AMA_VBUS_Reqd,				/* booleanFieldType */
	AMA_Superspeed_Support,			/* numericFieldType */

	/* 3.2.15 Cable/eMarker Fields */
	XID,					/* numericFieldType */
	Data_Capable_As_USB_Host,		/* booleanFieldType */
	Data_Capable_As_USB_Device,		/* booleanFieldType */
	Product_Type,				/* numericFieldType */
	Modal_Operation_Supported,		/* booleanFieldType */
	USB_VID,				/* numericFieldType */
	PID,					/* numericFieldType */
	bcdDevice,				/* numericFieldType */
	Cable_HW_Vers,				/* numericFieldType */
	Cable_FW_Vers,				/* numericFieldType */
	Type_C_To_Type_A_B_C,			/* numericFieldType */
	Type_C_To_Type_C_Capt_Vdm_V2,		/* numericFieldType */
	EPR_Mode_Capable,			/* booleanFieldType */
	Cable_Latency,				/* numericFieldType */
	Cable_Termination_Type,			/* numericFieldType */
	Cable_VBUS_Current,			/* numericFieldType */
	VBUS_Through_Cable,			/* booleanFieldType */
	Cable_Superspeed_Support,		/* numericFieldType */
	Cable_USB_Highest_Speed,		/* numericFieldType */
	Max_VBUS_Voltage_Vdm_V2,		/* numericFieldType */
	Manufacturer_Info_Supported,		/* booleanFieldType */
	Manufacturer_Info_VID,			/* numericFieldType */
	Manufacturer_Info_PID,			/* numericFieldType */
	Chunking_Implemented,			/* booleanFieldType */
	Security_Msgs_Supported,		/* booleanFieldType */
	ID_Header_Connector_Type,		/* numericFieldType */
	Cable_Num_SVIDs_Min,			/* numericFieldType */
	Cable_Num_SVIDs_Max,			/* numericFieldType */
	SVID_Fixed,				/* booleanFieldType */

	/* 3.2.16 Active Cable Fields */
	Cable_SOP_PP_Controller,		/* booleanFieldType */
	SBU_Supported,				/* booleanFieldType */
	SBU_Type,				/* numericFieldType */
	Active_Cable_Max_Operating_Temp,	/* numericFieldType */
	Active_Cable_Shutdown_Temp,		/* numericFieldType */
	Active_Cable_U3_CLd_Power,		/* numericFieldType */
	Active_Cable_U3_U0_Trans_Mode,		/* numericFieldType */
	Active_Cable_Physical_Connection,	/* numericFieldType */
	Active_Cable_Active_Element,		/* numericFieldType */
	Active_Cable_USB4_Support,		/* booleanFieldType */
	Active_Cable_USB2_Supported,		/* booleanFieldType */
	Active_Cable_USB2_Hub_Hops_Consumed,	/* numericFieldType */
	Active_Cable_USB32_Supported,		/* booleanFieldType */
	Active_Cable_USB_Lanes,			/* numericFieldType */
	Active_Cable_Optically_Isolated,	/* booleanFieldType */
	Active_Cable_USB_Gen,			/* numericFieldType */

	/* 3.2.17 VCONN Powered Devices */
	VPD_HW_Vers,				/* numericFieldType */
	VPD_FW_Vers,				/* numericFieldType */
	VPD_Max_VBUS_Voltage,			/* numericFieldType */
	VPD_Charge_Through_Support,		/* booleanFieldType */
	VPD_Charge_Through_Current,		/* numericFieldType */
	VPD_VBUS_Impedance,			/* numericFieldType */
	VPD_Ground_Impedance,			/* numericFieldType */

	/* 3.2.18 Repeater Fields */
	Repeater_One_Type,			/* numericFieldType */
	Repeater_Two_Type,			/* numericFieldType */

	Component_Indexes
};

struct vif_Component_t {
	struct vif_field_t		vif_field[Component_Indexes];

	struct vif_srcPdoList_t		SrcPdoList[MAX_NUM_SRC_PDOS];
	struct vif_snkPdoList_t		SnkPdoList[MAX_NUM_SNK_PDOS];
	struct vif_sopSVIDList_t	SOPSVIDList[MAX_NUM_SOP_SVIDS];
	struct vif_cableSVIDList_t	CableSVIDList[MAX_NUM_CABLE_SVIDS];

	/*
	 * The following fields are deprecated.  They should not be written
	 * to file in this version or any later version of the schema.
	 *
	 * Deprecated in VIF Version 3.10
	 *    vif_numericFieldType_t type_c_to_plug_receptacle;
	 *    vif_numericFieldType_t retimer_type;
	 * Deprecated in VIF Version 3.12
	 *    vif_numericFieldType_t active_cable_usb2_hub_hops_supported;
	 *    vif_booleanFieldType_t active_cable_optically_isololated;
	 */
};

/* 3.2.2 Product Fields */

enum vif_Product_indexes {
	USB4_Product_Header,			/* comment */
	USB4_DROM_Vendor_ID,			/* numericFieldType */
	USB4_Dock,				/* booleanFieldType */
	USB4_Num_Internal_Host_Controllers,	/* numericFieldType */
	USB4_Num_PCIe_DN_Bridges,		/* numericFieldType */
	USB4_Device_HiFi_Bi_TMU_Mode_Required,	/* booleanFieldType */
	USB4_Audio_Supported,			/* booleanFieldType */
	USB4_HID_Supported,			/* booleanFieldType */
	USB4_Printer_Supported,			/* booleanFieldType */
	USB4_Mass_Storage_Supported,		/* booleanFieldType */
	USB4_Video_Supported,			/* booleanFieldType */
	USB4_Comms_Networking_Supported,	/* booleanFieldType */
	USB4_Media_Transfer_Protocol_Supported,	/* booleanFieldType */
	USB4_Smart_Card_Supported,		/* booleanFieldType */
	USB4_Still_Image_Capture_Supported,	/* booleanFieldType */
	USB4_Monitor_Device_Supported,		/* booleanFieldType */
	Product_Indexes
};

struct vif_Product_t {
	struct vif_field_t		vif_field[Product_Indexes];

	struct vif_Usb4RouterListType_t USB4RouterList[MAX_NUM_USB4_ROUTERS];
};

enum vif_indexes {
	VIF_Specification,		/* version */
	Vendor_Name,			/* nonEmptyString */
	Model_Part_Number,		/* nonEmptyString */
	Product_Revision,		/* nonEmptyString */
	TID,				/* nonEmptyString */
	VIF_Product_Type,		/* numericFieldType */
	Certification_Type,		/* numericFieldType */
	VIF_Indexes
};

enum vif_app_indexes {
	Vendor,				/* nonEmptyString */
	Name,				/* nonEmptyString */
	Version,			/* version */
	VIF_App_Indexes
};

struct vif_t {
	struct vif_field_t		vif_field[VIF_Indexes];
	struct vif_field_t		vif_app_field[VIF_App_Indexes];

	struct vif_Product_t		Product;
	struct vif_Component_t		Component[MAX_NUM_COMPONENTS];
};

#endif /* __GENVIF_H__ */
