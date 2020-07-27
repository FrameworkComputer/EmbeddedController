/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "btle_hci_int.h"
#include "btle_hci2.h"
#include "bluetooth_le_ll.h"
#include "console.h"

#ifdef CONFIG_BLUETOOTH_HCI_DEBUG

#define CPUTS(outstr) cputs(CC_BLUETOOTH_HCI, outstr)
#define CPRINTS(format, args...) cprints(CC_BLUETOOTH_HCI, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_BLUETOOTH_HCI, format, ## args)

#else /* CONFIG_BLUETOOTH_HCI_DEBUG */

#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)

#endif /* CONFIG_BLUETOOTH_HCI_DEBUG */

static uint64_t hci_event_mask;
static uint64_t hci_le_event_mask;

#define MAX_MESSAGE 24

#define STATUS  (return_params[0])
#define RPARAMS (&(return_params[1]))

void hci_cmd(uint8_t *hciCmdbuf)
{
	static struct hciCmdHdr *hdr;
	static uint8_t *params;
	static uint8_t return_params[32];

	uint8_t rparam_count = 1; /* Just status */
	uint16_t event = HCI_EVT_Command_Complete; /* default */

	STATUS = 0xff;

	hdr = (struct hciCmdHdr *)hciCmdbuf;
	params = hciCmdbuf + sizeof(struct hciCmdHdr);

	CPRINTF("opcode %x OGF %d OCF %d\n", hdr->opcode,
		CMD_GET_OGF(hdr->opcode), CMD_GET_OCF(hdr->opcode));
	if (hdr->paramLen) {
		int i;

		CPRINTF("paramLen %d\n", hdr->paramLen);
		for (i = 0; i < hdr->paramLen; i++)
			CPRINTF("%x ", params[i]);
		CPRINTF("\n");
	}

	switch (hdr->opcode) {
	case CMD_MAKE_OPCODE(HCI_OGF_Controller_and_Baseband,
				HCI_CMD_Reset):
		STATUS = ll_reset();
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_Controller_and_Baseband,
				HCI_CMD_Set_Event_Mask):
		if (hdr->paramLen != sizeof(hci_event_mask))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = HCI_SUCCESS;
		memcpy(&hci_event_mask, params, sizeof(hci_event_mask));
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_Controller_and_Baseband,
				HCI_CMD_Read_Transmit_Power_Level):
	case CMD_MAKE_OPCODE(HCI_OGF_Informational,
				HCI_CMD_Read_Local_Supported_Features):
	case CMD_MAKE_OPCODE(HCI_OGF_Informational,
				HCI_CMD_Read_Local_Supported_Commands):
	case CMD_MAKE_OPCODE(HCI_OGF_Informational,
				HCI_CMD_Read_Local_Version_Information):
	case CMD_MAKE_OPCODE(HCI_OGF_Informational,
				HCI_CMD_Read_BD_ADDR):
	case CMD_MAKE_OPCODE(HCI_OGF_Link_Control,
				HCI_CMD_Read_Remote_Version_Information):
	case CMD_MAKE_OPCODE(HCI_OGF_Status,
				HCI_CMD_Read_RSSI):
		event = 0;
	break;

	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Event_Mask):
		if (hdr->paramLen != sizeof(hci_le_event_mask))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = HCI_SUCCESS;
		memcpy(&hci_le_event_mask, params, sizeof(hci_le_event_mask));
	break;

	/* LE Information */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Buffer_Size):
		if (hdr->paramLen != 0)
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_read_buffer_size(RPARAMS);
		rparam_count = sizeof(struct hciCmplLeReadBufferSize);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Local_Supported_Features):
		if (hdr->paramLen != 0)
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_read_local_supported_features(RPARAMS);
		rparam_count =
			sizeof(struct hciCmplLeReadLocalSupportedFeatures);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Supported_States):
		if (hdr->paramLen != 0)
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_read_supported_states(RPARAMS);
		rparam_count = sizeof(struct hciCmplLeReadSupportedStates);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Host_Channel_Classification):
		if (hdr->paramLen !=
		    sizeof(struct hciLeSetHostChannelClassification))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_set_host_channel_classification(params);
	break;

	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Random_Address):
		if (hdr->paramLen != sizeof(struct hciLeSetRandomAddress))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_set_random_address(params);
	break;

	/* Advertising */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Advertise_Enable):
		STATUS = ll_set_advertising_enable(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Advertising_Data):
		STATUS = ll_set_adv_data(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Adv_Params):
		if (hdr->paramLen != sizeof(struct hciLeSetAdvParams))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_set_advertising_params(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Adv_Channel_TX_Power):
		STATUS = ll_read_tx_power();
		rparam_count = sizeof(struct hciCmplLeReadAdvChannelTxPower);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Scan_Response_Data):
		STATUS = ll_set_scan_response_data(params);
	break;

	/* Connections */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Remote_Used_Features):
		if (hdr->paramLen != sizeof(struct hciLeReadRemoteUsedFeatures))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_read_remote_used_features(params);
		event = HCI_EVT_Command_Status;
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_Link_Control,
				HCI_CMD_Disconnect):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Connection_Update):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Create_Connection):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Create_Connection_Cancel):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Channel_Map):
		event = 0;
	break;

	/* Encryption */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Encrypt):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_LTK_Request_Reply):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_LTK_Request_Negative_Reply):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Rand):
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Start_Encryption):
		event = 0;
	break;

	/* Scanning */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Scan_Enable):
		if (hdr->paramLen != sizeof(struct hciLeSetScanEnable))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_set_scan_enable(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Set_Scan_Parameters):
		if (hdr->paramLen != sizeof(struct hciLeSetScanParams))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_set_scan_params(params);
	break;

	/* Allow List */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Clear_Allow_List):
		if (hdr->paramLen != 0)
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_clear_allow_list();
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Read_Allow_List_Size):
		if (hdr->paramLen != 0)
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_read_allow_list_size(RPARAMS);
		rparam_count = sizeof(struct hciCmplLeReadAllowListSize);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Add_Device_To_Allow_List):
		if (hdr->paramLen != sizeof(struct hciLeAddDeviceToAllowList))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_add_device_to_allow_list(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Remove_Device_From_Allow_List):
		if (hdr->paramLen !=
				sizeof(struct hciLeRemoveDeviceFromAllowList))
			STATUS = HCI_ERR_Invalid_HCI_Command_Parameters;
		else
			STATUS = ll_remove_device_from_allow_list(params);
	break;

	/* RFPHY Testing Support */
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Receiver_Test):
		STATUS = ll_receiver_test(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Transmitter_Test):
		STATUS = ll_transmitter_test(params);
	break;
	case CMD_MAKE_OPCODE(HCI_OGF_LE,
				HCI_CMD_LE_Test_End):
		STATUS = ll_test_end(RPARAMS);
		rparam_count = sizeof(struct hciCmplLeTestEnd);
	break;

	default:
		STATUS = HCI_ERR_Unknown_HCI_Command;
	break;
	}

	hci_event(event, rparam_count, return_params);
}

void hci_acl_to_host(uint8_t *data, uint16_t hdr, uint16_t len)
{
	int i;

	/* Enqueue hdr, len, len bytes of data */
	CPRINTF("Sending %d bytes of data from handle %d with PB=%x.\n",
		len, hdr & ACL_HDR_MASK_CONN_ID,
		hdr & ACL_HDR_MASK_PB);
		for (i = 0; i < len; i++)
			CPRINTF("0x%x, ", data[i]);
		CPRINTF("\n");
}

void hci_acl_from_host(uint8_t *hciAclbuf)
{
	struct hciAclHdr *hdr = (struct hciAclHdr *)hciAclbuf;
	uint8_t *data = hciAclbuf + sizeof(struct hciAclHdr);
	int i;

	/* Send the data to the link layer */
	CPRINTF("Sending %d bytes of data to handle %d with PB=%x.\n",
		hdr->len, hdr->hdr & ACL_HDR_MASK_CONN_ID,
		hdr->hdr & ACL_HDR_MASK_PB);
		for (i = 0; i < hdr->len; i++)
			CPRINTF("0x%x, ", data[i]);
		CPRINTF("\n");
}

/*
 * Required Events
 *
 * HCI_EVT_Command_Complete
 * HCI_EVT_Command_Status
 * HCI_EVTLE_Advertising_Report
 * HCI_EVT_Disconnection_Complete
 * HCI_EVTLE_Connection_Complete
 * HCI_EVTLE_Connection_Update_Complete
 * HCI_EVTLE_Read_Remote_Used_Features_Complete
 * HCI_EVT_Number_Of_Completed_Packets
 * HCI_EVT_Read_Remote_Version_Complete
 * HCI_EVT_Encryption_Change
 * HCI_EVT_Encryption_Key_Refresh_Complete
 * HCI_EVTLE_Long_Term_Key_Request
 */
void hci_event(uint8_t event_code, uint8_t len, uint8_t *params)
{
	int i;

	/* Copy it to the queue. */
	CPRINTF("Event 0x%x len %d\n", event_code, len);
	for (i = 0; i < len; i++)
		CPRINTF("%x ", params[i]);
	CPRINTF("\n");
}

#ifdef CONFIG_BLUETOOTH_HCI_DEBUG

/*
 * LE_Set_Advertising_Data
 * hcitool lcmd 0x2008 19 0x42410907 0x46454443 0x3c11903 0x3050102 0x181203
 * hcitool cmd 8 8 7 9 41 42 43 44 45 46 3 19 c1 3 2 1 5 3 3 12 18
 *
 * hcitool lcmd 0x2008 18 0x42410906 0x03454443 0x203c119 0x3030501 0x1812
 * hcitool cmd 8 8 6 9 41 42 43 44 45 3 19 c1 3 2 1 5 3 3 12 18
 */
uint8_t adv0[19] = {0x07, 0x09, 'A', 'B', 'C', 'D', 'E', 'F', /* Name */
		    0x03, 0x19, 0xc1, 0x03,                   /* Keyboard */
		    0x02, 0x01, 0x05,                         /* Flags */
		    0x03, 0x03, 0x12, 0x18};                  /* UUID */

uint8_t adv1[18] = {0x06, 0x09, 'A', 'B', 'C', 'D', 'E',      /* Name */
		    0x02, 0x01, 0x05,                         /* Flags */
		    0x03, 0x19, 0xc1, 0x03,                   /* Keyboard */
		    0x03, 0x03, 0x12, 0x18};                  /* UUID */

uint8_t *adverts[] = {adv0, adv1};
uint8_t adv_lengths[] = {sizeof(adv0), sizeof(adv1)};

uint8_t scan0[4] = {0x03, 0x08, 'A', 'B'}; /* Short Name */

uint8_t scan1[] = {};                         /* Empty */

uint8_t *scans[] = {scan0, scan1};
uint8_t scan_lengths[] = {sizeof(scan0), sizeof(scan1)};

/*
 * LE_Set_Adv_Params
 * hcitool lcmd 0x2006 15 0x010000f0 0xb0010100 0xb4b3b2b1 0x0007c5
 * hcitool cmd 8 6 f0 0 0 1 0 1 1 b0 b1 b2 b3 b4 c5 7 0
 */
uint8_t adv_param0[15] = {
		0xf0, 0x00,                               /* IntervalMin */
		0x00, 0x01,                               /* IntervalMax */
		0x00,                                     /* Adv Type */
		0x01,                                     /* Use Random Addr */
		0x01,                                     /* Direct Random */
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xc5,       /* Direct Addr */
		0x07,                                     /* Channel Map */
		0x00};                                    /* Filter Policy */

uint8_t adv_param1[15] = {
		0xf0, 0x00,                               /* IntervalMin */
		0x00, 0x01,                               /* IntervalMax */
		0x02,                                     /* Adv Type */
		0x01,                                     /* Use Random Addr */
		0x01,                                     /* Direct Random */
		0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xc5,       /* Direct Addr */
		0x07,                                     /* Channel Map */
		0x00};                                    /* Filter Policy */

uint8_t *adv_params[] = {adv_param0, adv_param1};

/*
 * LE Information
 *
 * LE Read Buffer Size
 * hcitool cmd 8 2
 *
 * LE_Read_Local_Supported_Features
 * hcitool cmd 8 3
 *
 * LE_Read_Supported_States
 * hcitool cmd 8 1c
 *
 * LE_Set_Host_Channel_Classification
 * hcitool cmd 8 14 0 1 2 3 4
 * hcitool cmd 8 14 ff ff 02 ff 1f
 */

/*
 * Scan commands:
 *
 * Set Scan Parameters:
 * hcitool cmd 8 B 0 10 0 10 0 0 0 (passive 10 10 public all)
 * hcitool lcmd 0x200B 7 0x10001000 0x0000 (passive 10 10 public all)
 *
 * hcitool cmd 8 B 1 30 0 20 0 1 1 (active 30 20 rand white)
 * hcitool lcmd 0x200B 7 0x20003001 0x0101 (active 30 20 rand white)
 *
 * Set Scan Enable:
 * hcitool cmd 8 C 0 0 (disabled)
 * hcitool cmd 8 C 1 0 (enabled no_filtering)
 * hcitool cmd 8 C 1 1 (enabled filter_duplicates)
 *
 */

/* Allow list commands:
 *
 * Read allow list size
 * hcitool cmd 8 F
 *
 * Clear allow list
 * hcitool cmd 8 10
 *
 * Add device to allow list (Public C5A4A3A2A1A0)
 * hcitool cmd 8 11 0 a0 a1 a2 a3 a4 c5
 * hcitool lcmd 0x2011 7 0xA2A1A000 0xC5A4A3
 *
 * Add device to allow list (Random C5B4B3B2B1B0)
 * hcitool cmd 8 11 1 b0 b1 b2 b4 b5 c5
 * hcitool lcmd 0x2011 7 0xB2B1B001 0xC5B4B3
 *
 * Remove device from allow list (Public C5A4A3A2A1A0)
 * hcitool cmd 8 12 0 a0 a1 a2 a3 a4 c5
 * hcitool lcmd 0x2012 7 0xA2A1A000 0xC5A4A3
 *
 * Remove device from allow list (Random C5B4B3B2B1B0)
 * hcitool cmd 8 12 1 b0 b1 b2 b4 b5 c5
 * hcitool lcmd 0x2012 7 0xB2B1B001 0xC5B4B3
 *
 * Tested by checking dumping the allow list and checking its size when:
 * - adding devices
 * - removing devices
 * - removing non-existent devices
 * - adding more than 8 devices
 *
 */

/*
 * Test commands:
 *
 * Rx Test channel 37
 * hcitool cmd 8 1D 25
 *
 * Tx Test channel 37 20 bytes type 2
 * hcitool cmd 8 1e 25 14 2
 *
 * Test end
 * hcitool cmd 8 1f
 */

static uint8_t hci_buf[200];

#define MAX_BLE_HCI_PARAMS 8
static uint32_t param[MAX_BLE_HCI_PARAMS];

static int command_ble_hci_cmd(int argc, char **argv)
{
	static struct hciCmdHdr header;
	int length, opcode, i;
	char *e;

	if (argc < 3 || argc > MAX_BLE_HCI_PARAMS + 3)
		return EC_ERROR_PARAM_COUNT;

	opcode = strtoi(argv[1], &e, 0);
	if (*e || opcode < 0 || opcode > 0xffff)
		return EC_ERROR_PARAM1;

	length = strtoi(argv[2], &e, 0);
	if (*e || length < 0 || length > 32)
		return EC_ERROR_PARAM2;

	if ((length + 3) / 4 != argc - 3) {
		CPRINTF("Remember to pass HCI params in 32-bit chunks.\n");
		return EC_ERROR_PARAM_COUNT;
	}

	for (i = 3; i < argc; i++) {
		param[i-3] = strtoi(argv[i], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3 + i;
	}

	header.opcode = opcode;
	header.paramLen = length;

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	memcpy(hci_buf + sizeof(struct hciCmdHdr),
			param, length);

	hci_cmd(hci_buf);

	CPRINTS("hci cmd @%pP", hci_buf);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ble_hci_cmd, command_ble_hci_cmd,
			"opcode len uint32 uint32 uint32... (little endian)",
			"Send an hci command of length len");

static int command_hcitool(int argc, char **argv)
{
	static struct hciCmdHdr header;
	int i, ogf, ocf;
	char *e;

	if (argc < 4 || argc > MAX_BLE_HCI_PARAMS + 3)
		return EC_ERROR_PARAM_COUNT;

	if (argv[1][0] == 'l') /* strcmp lcmd */
		return command_ble_hci_cmd(argc-1, &argv[1]);

	ogf = strtoi(argv[2], &e, 16);
	if (*e)
		return EC_ERROR_PARAM2;

	ocf = strtoi(argv[3], &e, 16);
	if (*e)
		return EC_ERROR_PARAM3;

	header.opcode = CMD_MAKE_OPCODE(ogf, ocf);
	header.paramLen = argc-4;
	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));

	for (i = 4; i < argc; i++) {
		hci_buf[i - 4 + 3] = strtoi(argv[i], &e, 16);
		if (*e)
			return EC_ERROR_PARAM4 + i;
	}

	hci_cmd(hci_buf);

	CPRINTS("hci cmd @%pP", hci_buf);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcitool, command_hcitool,
			"cmd ogf ocf b0 b1 b2 b3... or lcmd opcode len uint32.. (little endian)",
			"Send an hci command of length len");

static int command_ble_hci_acl(int argc, char **argv)
{
	static struct hciAclHdr header;
	int length, hdr, i;
	char *e;

	if (argc < 3 || argc > MAX_BLE_HCI_PARAMS + 3)
		return EC_ERROR_PARAM_COUNT;

	hdr = strtoi(argv[1], &e, 0);
	if (*e || hdr < 0 || hdr > 0xffff)
		return EC_ERROR_PARAM1;

	length = strtoi(argv[2], &e, 0);
	if (*e || length < 0 || length > 32)
		return EC_ERROR_PARAM2;

	if ((length + 3) / 4 != argc - 3) {
		CPRINTF("Remember to pass HCI params in 32-bit chunks.\n");
		return EC_ERROR_PARAM_COUNT;
	}

	for (i = 3; i < argc; i++) {
		param[i-3] = strtoi(argv[i], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3 + i;
	}

	header.hdr = hdr;
	header.len = length;

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	memcpy(hci_buf + sizeof(struct hciCmdHdr),
			param, length);

	hci_cmd(hci_buf);

	CPRINTS("hci acl @%pP", hci_buf);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ble_hci_acl, command_ble_hci_acl,
			"hdr len uint32 uint32 uint32... (little endian)",
			"Send hci acl data of length len");

static int command_ble_hci_adv(int argc, char **argv)
{
	static struct hciCmdHdr header;
	int adv, p = 0, scan_rsp = 0;
	char *e;

	if (argc < 2 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	adv = strtoi(argv[1], &e, 0);
	if (*e || adv < 0 || adv > sizeof(adverts))
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		p = strtoi(argv[2], &e, 0);
		if (*e || p < 0 || p > sizeof(adv_params))
			return EC_ERROR_PARAM2;
	}

	if (argc > 3) {
		scan_rsp = strtoi(argv[3], &e, 0);
		if (*e || scan_rsp < 0 || scan_rsp > sizeof(scans))
			return EC_ERROR_PARAM3;
	}

	header.opcode = CMD_MAKE_OPCODE(HCI_OGF_LE, HCI_CMD_LE_Set_Adv_Params);
	header.paramLen = sizeof(struct hciLeSetAdvParams);

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	memcpy(hci_buf + sizeof(struct hciCmdHdr),
			adv_params[p], header.paramLen);

	hci_cmd(hci_buf);

	header.opcode = CMD_MAKE_OPCODE(HCI_OGF_LE,
					HCI_CMD_LE_Set_Advertising_Data);
	header.paramLen = adv_lengths[adv];

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	memcpy(hci_buf + sizeof(struct hciCmdHdr),
			adverts[adv], header.paramLen);

	hci_cmd(hci_buf);

	header.opcode = CMD_MAKE_OPCODE(HCI_OGF_LE,
					HCI_CMD_LE_Set_Scan_Response_Data);
	header.paramLen = scan_lengths[scan_rsp];

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	memcpy(hci_buf + sizeof(struct hciCmdHdr),
			scans[scan_rsp], header.paramLen);

	hci_cmd(hci_buf);

	header.opcode = CMD_MAKE_OPCODE(HCI_OGF_LE,
					HCI_CMD_LE_Set_Advertise_Enable);
	header.paramLen = sizeof(struct hciLeSetAdvEnable);

	memcpy(hci_buf, &header, sizeof(struct hciCmdHdr));
	hci_buf[sizeof(struct hciCmdHdr)] = 1;

	hci_cmd(hci_buf);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ble_hci_adv, command_ble_hci_adv,
			"adv [params=0] [scan_rsp=0]",
			"Use pre-defined parameters to start advertising");

#endif /* CONFIG_BLUETOOTH_HCI_DEBUG */
