/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC keyboard common code.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "keyboard.h"
#include "i8042.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "registers.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "x86_power.h"


#define KEYBOARD_DEBUG 1

#undef ASSERT
#define ASSERT(expr) do { \
      if (!(expr)) { \
        uart_printf("ASSERT(%s) failed at %s:%d.\n", #expr, __FUNCTION__, __LINE__); \
        while (1) usleep(1000000); \
      } \
    } while (0)

/*
 * i8042 global settings.
 */
static int keyboard_enabled = 0;  /* default the keyboard is disabled. */
static uint8_t resend_command[MAX_SCAN_CODE_LEN];
static uint8_t resend_command_len = 0;
static uint8_t controller_ram_address;
static uint8_t controller_ram[0x20] = {
  /* the so called "command byte" */
  I8042_XLATE | I8042_AUX_DIS | I8042_KBD_DIS,
  /* 0x01 - 0x1f are controller RAM */
};
static int power_button_pressed = 0;

/*
 * Scancode settings
 */
static enum scancode_set_list scancode_set = SCANCODE_SET_2;

/*
 * Typematic delay, rate and counter variables.
 *
 *    7     6     5     4     3     2     1     0
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |un-  |   delay   |     B     |        D        |
 * | used|  0     1  |  0     1  |  0     1     1  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * Formula:
 *   the inter-char delay = (2 ** B) * (D + 8) / 240 (sec)
 * Default: 500ms delay, 10.9 chars/sec.
 */
#define DEFAULT_TYPEMATIC_VALUE ((1 << 5) || (1 << 3) || (3 << 0))
#define DEFAULT_FIRST_DELAY 500
#define DEFAULT_INTER_DELAY 91
static uint8_t typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
static int refill_first_delay = DEFAULT_FIRST_DELAY;  /* unit: ms */
static int counter_first_delay;
static int refill_inter_delay = DEFAULT_INTER_DELAY;  /* unit: ms */
static int counter_inter_delay;


/* The standard Chrome OS keyboard matrix table. */
#define CROS_ROW_NUM 8  /* TODO: +1 for power button. */
#define CROS_COL_NUM 13
static uint16_t scancode_set1[CROS_ROW_NUM][CROS_COL_NUM] = {
  {0x0000, 0xe05b, 0x003b, 0x0030, 0x0044, 0x0073, 0x0031, 0x0000, 0x000d,
                                           0x0000, 0xe038, 0x0000, 0x0000},
  {0x0000, 0x0001, 0x003e, 0x0022, 0x0041, 0x0000, 0x0023, 0x0000, 0x0028,
                                           0x0043, 0x0000, 0x000e, 0x0078},
  {0x001d, 0x000f, 0x003d, 0x0014, 0x0040, 0x001b, 0x0015, 0x0056, 0x001a,
                                           0x0042, 0x0073, 0x0000, 0x0000},
  {0x0000, 0x0029, 0x003c, 0x0006, 0x003f, 0x0000, 0x0007, 0x0000, 0x000c,
                                           0x0000, 0x0000, 0x002b, 0x0079},
  {0xe01d, 0x001e, 0x0020, 0x0021, 0x001f, 0x0025, 0x0024, 0x0000, 0x0027,
                                           0x0026, 0x002b, 0x001c, 0x0000},
  {0x0000, 0x002c, 0x002e, 0x002f, 0x002d, 0x0033, 0x0032, 0x002a, 0x0035,
                                           0x0034, 0x0000, 0x0039, 0x0000},
  {0x0000, 0x0002, 0x0004, 0x0005, 0x0003, 0x0009, 0x0008, 0x0000, 0x000b,
                                           0x000a, 0x0038, 0xe050, 0xe04d},
  {0x0000, 0x0010, 0x0012, 0x0013, 0x0011, 0x0017, 0x0016, 0x0036, 0x0019,
                                           0x0018, 0x0000, 0xe048, 0xe04b},
};

static uint16_t scancode_set2[CROS_ROW_NUM][CROS_COL_NUM] = {
  {0x0000, 0xe01f, 0x0005, 0x0032, 0x0009, 0x0051, 0x0031, 0x0000, 0x0055,
                                           0x0000, 0xe011, 0x0000, 0x0000},
  {0x0000, 0x0076, 0x000c, 0x0034, 0x0083, 0x0000, 0x0033, 0x0000, 0x0052,
                                           0x0001, 0x0000, 0x0066, 0x0067},
  {0x0014, 0x000d, 0x0004, 0x002c, 0x000b, 0x005b, 0x0035, 0x0061, 0x0054,
                                           0x000a, 0x0051, 0x0000, 0x0000},
  {0x0000, 0x000e, 0x0006, 0x002e, 0x0003, 0x0000, 0x0036, 0x0000, 0x004e,
                                           0x0000, 0x0000, 0x005d, 0x0064},
  {0xe014, 0x001c, 0x0023, 0x002b, 0x001b, 0x0042, 0x003b, 0x0000, 0x004c,
                                           0x004b, 0x005d, 0x005a, 0x0000},
  {0x0000, 0x001a, 0x0021, 0x002a, 0x0022, 0x0041, 0x003a, 0x0012, 0x004a,
                                           0x0049, 0x0000, 0x0029, 0x0000},
  {0x0000, 0x0016, 0x0026, 0x0025, 0x001e, 0x003e, 0x003d, 0x0000, 0x0045,
                                           0x0046, 0x0011, 0xe072, 0xe074},
  {0x0000, 0x0015, 0x0024, 0x002d, 0x001d, 0x0043, 0x003c, 0x0059, 0x004d,
                                           0x0044, 0x0000, 0xe075, 0xe06b},
};


/* Recording which key is being simulated pressed. */
static uint8_t simulated_key[CROS_COL_NUM];


/* change to set 1 if the I8042_XLATE flag is set. */
static enum scancode_set_list acting_code_set(enum scancode_set_list set) {
  if (controller_ram[0] & I8042_XLATE) {
    /* If the keyboard translation is enabled,
     * then always generates set 1. */
    return SCANCODE_SET_1;
  }
  return set;
}


static enum ec_error_list matrix_callback(
    int8_t row, int8_t col, int8_t pressed,
    enum scancode_set_list code_set, uint8_t *scan_code, int32_t* len) {

  uint16_t make_code;

  ASSERT(scan_code);
  ASSERT(len);

  if (row > CROS_ROW_NUM ||
      col > CROS_COL_NUM) {
    return EC_ERROR_INVAL;
  }

  *len = 0;

  code_set = acting_code_set(code_set);

  switch (code_set) {
  case SCANCODE_SET_1:
    make_code = scancode_set1[row][col];
    break;

  case SCANCODE_SET_2:
    make_code = scancode_set2[row][col];
    break;

  default:
#if KEYBOARD_DEBUG >= 1
    uart_printf("Not supported scan code set: %d\n", code_set);
#endif
    return EC_ERROR_UNIMPLEMENTED;
  }
  if (!make_code) {
#if KEYBOARD_DEBUG >= 1
    uart_printf("No scancode for [row:col]=[%d:%d].\n", row, col);
#endif
    return EC_ERROR_UNIMPLEMENTED;
  }

  /* Output the make code (from table) */
  if (make_code >= 0x0100) {
    *len += 2;
    scan_code[0] = make_code >> 8;
    scan_code[1] = make_code & 0xff;
  } else {
    *len += 1;
    scan_code[0] = make_code & 0xff;
  }

  switch (code_set) {
  case SCANCODE_SET_1:
    /* OR 0x80 for the last byte. */
    if (!pressed) {
      ASSERT(*len >= 1);
      scan_code[*len - 1] |= 0x80;
    }
    break;

  case SCANCODE_SET_2:
    /* insert the break byte, move back the last byte and insert a 0xf0 byte
     * before that. */
    if (!pressed) {
      ASSERT(*len >= 1);
      scan_code[*len] = scan_code[*len - 1];
      scan_code[*len - 1] = 0xF0;
      *len += 1;
    }
    break;
  default:
    break;
  }

  return EC_SUCCESS;
}


static void reset_rate_and_delay(void) {
  typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
  refill_first_delay = DEFAULT_FIRST_DELAY;
  refill_inter_delay = DEFAULT_INTER_DELAY;
}


static void clean_underlying_buffer(void) {
  i8042_init();
}


/* TODO: Move this implementation to platform-dependent files.
 *       We don't do it now because not every board implement x86_power.c
 *         bds: no CONFIG_LPC and no CONFIG_TASK_X86POWER
 *         daisy(variants): no CONFIG_LPC and no CONFIG_TASK_X86POWER
 *       crosbug.com/p/8523
 */
static void keyboard_wakeup(void) {
#ifdef CONFIG_LPC
  lpc_set_host_events(EC_LPC_HOST_EVENT_MASK(EC_LPC_HOST_EVENT_KEY_PRESSED));
#endif
}


void keyboard_state_changed(int row, int col, int is_pressed) {
  uint8_t scan_code[MAX_SCAN_CODE_LEN];
  int32_t len;
  enum ec_error_list ret;

#if KEYBOARD_DEBUG >= 5
  uart_printf("File %s:%s(): row=%d col=%d is_pressed=%d\n",
      __FILE__, __FUNCTION__, row, col, is_pressed);
#endif

  ret = matrix_callback(row, col, is_pressed, scancode_set, scan_code, &len);
  if (ret == EC_SUCCESS) {
    ASSERT(len > 0);

    i8042_send_to_host(len, scan_code);
  } else {
    /* FIXME: long-term solution is to ignore this key. However, keep
     *        assertion in the debug stage. */
    ASSERT(ret == EC_SUCCESS);
  }

  if (is_pressed) {
    keyboard_wakeup();
  }
}


void keyboard_enable(int enable) {
  if (!keyboard_enabled && enable) {
    /* enable */
  } else if (keyboard_enabled && !enable) {
    /* disable */
    reset_rate_and_delay();
    clean_underlying_buffer();
  }
  keyboard_enabled = enable;
}


uint8_t read_ctl_ram(uint8_t addr) {
  ASSERT(addr < 0x20);  // Controller RAM is only 32 bytes.

  return controller_ram[addr];
}


/* Manipulates the controller_ram[]. Some bits change may trigger internal
 * state change.
 */
void update_ctl_ram(uint8_t addr, uint8_t data) {
  uint8_t orig;

  ASSERT(addr < 0x20);  // Controller RAM is only 32 bytes.
  orig = controller_ram[addr];
  controller_ram[addr] = data;
#if KEYBOARD_DEBUG >= 5
  uart_printf("Set CTR_RAM[0x%02x]=0x%02x (old:0x%02x)\n",
              addr, data, orig);
#endif

  if (addr == 0x00) {  /* the controller RAM */
    /* Handle the I8042_KBD_DIS bit */
    keyboard_enable(!(data & I8042_KBD_DIS));

    /* Handle the I8042_ENIRQ1 bit */
    if (!(orig & I8042_ENIRQ1) && (data & I8042_ENIRQ1)) {
      i8042_enable_keyboard_irq();
    } else if ((orig & I8042_ENIRQ1) && !(data & I8042_ENIRQ1)) {
      i8042_disable_keyboard_irq();
    }
  }
}


enum {
  STATE_NORMAL = 0,
  STATE_SCANCODE,
  STATE_SETLEDS,
  STATE_WRITE_CMD_BYTE,
  STATE_ECHO_MOUSE,
  STATE_SEND_TO_MOUSE,
} data_port_state = STATE_NORMAL;


int handle_keyboard_data(uint8_t data, uint8_t *output) {
  int out_len = 0;
  int save_for_resend = 1;
  int i;

#if KEYBOARD_DEBUG >= 5
  uart_printf("[%d] Recv data:[0x%02x]\n", get_time().le.lo, data);
#endif

  switch (data_port_state) {
  case STATE_SCANCODE:
#if KEYBOARD_DEBUG >= 5
    uart_printf("Eaten by STATE_SCANCODE: 0x%02x\n", data);
#endif
    if (data == SCANCODE_GET_SET) {
      output[out_len++] = I8042_RET_ACK;
      output[out_len++] = scancode_set;
    } else {
      scancode_set = data;
#if KEYBOARD_DEBUG >= 1
      uart_printf("Scancode set to %d\n", scancode_set);
#endif
      output[out_len++] = I8042_RET_ACK;
    }
    data_port_state = STATE_NORMAL;
    break;

  case STATE_SETLEDS:
#if KEYBOARD_DEBUG >= 5
    uart_printf("Eaten by STATE_SETLEDS\n");
#endif
    output[out_len++] = I8042_RET_ACK;
    data_port_state = STATE_NORMAL;
    break;

  case STATE_WRITE_CMD_BYTE:
#if KEYBOARD_DEBUG >= 5
    uart_printf("Eaten by STATE_WRITE_CMD_BYTE: 0x%02x\n", data);
#endif
    update_ctl_ram(controller_ram_address, data);
    output[out_len++] = I8042_RET_ACK;
    data_port_state = STATE_NORMAL;
    break;

  case STATE_ECHO_MOUSE:
#if KEYBOARD_DEBUG >= 5
    uart_printf("Eaten by STATE_ECHO_MOUSE: 0x%02x\n", data);
#endif
    output[out_len++] = I8042_RET_ACK;
    output[out_len++] = data;
    data_port_state = STATE_NORMAL;
    break;

  case STATE_SEND_TO_MOUSE:
#if KEYBOARD_DEBUG >= 5
    uart_printf("Eaten by STATE_SEND_TO_MOUSE: 0x%02x\n", data);
#endif
    data_port_state = STATE_NORMAL;
    break;

  default:  /* STATE_NORMAL */
    switch (data) {
      case I8042_CMD_GSCANSET:  /* also I8042_CMD_SSCANSET */
        output[out_len++] = I8042_RET_ACK;
        data_port_state = STATE_SCANCODE;
        break;

      case I8042_CMD_SETLEDS:  /* fall-thru */
      case I8042_CMD_EX_SETLEDS:
        /* We use screen indicator. Do thing in keyboard controller. */
        output[out_len++] = I8042_RET_ACK;
        data_port_state = STATE_SETLEDS;
        break;

      case I8042_CMD_DIAG_ECHO:
        output[out_len++] = I8042_RET_ACK;
        output[out_len++] = I8042_CMD_DIAG_ECHO;
        break;

      case I8042_CMD_GETID:    /* fall-thru */
      case I8042_CMD_OK_GETID:
        output[out_len++] = I8042_RET_ACK;
        output[out_len++] = 0xab;  /* Regular keyboards */
        output[out_len++] = 0x83;
        break;

      case I8042_CMD_SETREP:
        output[out_len++] = I8042_RET_ACK;
        typematic_value_from_host = data;
        refill_first_delay = counter_first_delay + counter_inter_delay;
        refill_first_delay = ((typematic_value_from_host & 0x60) >> 5) * 250;
        refill_inter_delay = 1000 *  /* ms */
                             (1 << ((typematic_value_from_host & 0x18) >> 3)) *
                             ((typematic_value_from_host & 0x7) + 8) /
                             240;
        break;

      case I8042_CMD_ENABLE:
        output[out_len++] = I8042_RET_ACK;
        keyboard_enable(1);
        break;

      case I8042_CMD_RESET_DIS:
        output[out_len++] = I8042_RET_ACK;
        keyboard_enable(0);
        reset_rate_and_delay();
        clean_underlying_buffer();
        break;

      case I8042_CMD_RESET_DEF:
        output[out_len++] = I8042_RET_ACK;
        reset_rate_and_delay();
        clean_underlying_buffer();
        break;

      case I8042_CMD_RESET_BAT:
        output[out_len++] = I8042_RET_ACK;
        keyboard_enable(0);
        output[out_len++] = I8042_RET_BAT;
        output[out_len++] = I8042_RET_BAT;
        break;

      case I8042_CMD_RESEND:
        output[out_len++] = I8042_RET_ACK;
        save_for_resend = 0;
        for (i = 0; i < resend_command_len; ++i) {
          output[out_len++] = resend_command[i];
        }
        break;

      /* u-boot hack */
      case 0x60:  /* see CONFIG_USE_CPCIDVI in */
      case 0x45:  /* third_party/u-boot/files/drivers/input/i8042.c */
        /* just ignore, don't reply anything. */
        break;

      case I8042_CMD_SETALL_MB:  /* fall-thru below */
      case I8042_CMD_SETALL_MBR:
      case I8042_CMD_EX_ENABLE:
      default:
        output[out_len++] = I8042_RET_NAK;
#if KEYBOARD_DEBUG >= 1
        uart_printf("Unsupported i8042 data 0x%02x.\n", data);
#endif
        break;
    }
  }

  /* For resend, keep output before leaving. */
  if (out_len && save_for_resend) {
    ASSERT(out_len <= MAX_SCAN_CODE_LEN);
    for (i = 0; i < out_len; ++i) {
      resend_command[i] = output[i];
    }
    resend_command_len = out_len;
  }

  ASSERT(out_len <= MAX_SCAN_CODE_LEN);
  return out_len;
}


int handle_keyboard_command(uint8_t command, uint8_t *output) {
  int out_len = 0;

#if KEYBOARD_DEBUG >= 5
  uart_printf("[%d] Recv cmd:[0x%02x]\n", get_time().le.lo, command);
#endif
  switch (command) {
  case I8042_READ_CMD_BYTE:
    output[out_len++] = read_ctl_ram(0);
    break;

  case I8042_WRITE_CMD_BYTE:
    data_port_state = STATE_WRITE_CMD_BYTE;
    controller_ram_address = command - 0x60;
    break;

  case I8042_DIS_KB:
    keyboard_enable(0);
    break;

  case I8042_ENA_KB:
    keyboard_enable(1);
    break;

  case I8042_RESET_SELF_TEST:
    output[out_len++] = 0x55;  // Self test success.
    break;

  case I8042_DIS_MOUSE:
    update_ctl_ram(0, read_ctl_ram(0) | I8042_AUX_DIS);
    break;

  case I8042_ENA_MOUSE:
    update_ctl_ram(0, read_ctl_ram(0) & ~I8042_AUX_DIS);
    break;

  case I8042_TEST_MOUSE:
    output[out_len++] = 0;  // no error detected
    break;

  case I8042_ECHO_MOUSE:
    data_port_state = STATE_ECHO_MOUSE;
    break;

  case I8042_SEND_TO_MOUSE:
    data_port_state = STATE_SEND_TO_MOUSE;
    break;

#ifdef CONFIG_TASK_X86POWER
  case I8042_SYSTEM_RESET:
	  /* Trigger a cold reset, since that also resets the TPM.  If we just
	   * trigger a warm reset, that doesn't assert PLTRST# and the TPM is
	   * not reset.  See crosbug.com/p/8379. */
	  x86_power_reset(1);
	  break;
#endif

  default:
    if (command >= I8042_READ_CTL_RAM &&
        command <= I8042_READ_CTL_RAM_END) {
      output[out_len++] = read_ctl_ram(command - 0x20);
    } else if (command >= I8042_WRITE_CTL_RAM &&
               command <= I8042_WRITE_CTL_RAM_END) {
      data_port_state = STATE_WRITE_CMD_BYTE;
      controller_ram_address = command - 0x60;
    } else if (command >= I8042_PULSE_START &&
               command <= I8042_PULSE_END) {
      /* Pulse Output Bit. Not implemented. Ignore it. */
    } else {
#if KEYBOARD_DEBUG >= 1
      uart_printf("Unsupported cmd:[0x%02x]\n", command);
#endif
      reset_rate_and_delay();
      clean_underlying_buffer();
      output[out_len++] = I8042_RET_NAK;
      data_port_state = STATE_NORMAL;
    }
    break;
  }

  return out_len;
}


void keyboard_set_power_button(int pressed)
{
	enum scancode_set_list code_set;
	enum ec_error_list ret;
	uint8_t code[2][2][3] = {
		{  /* set 1 */
			{0xe0, 0xde},        /* break */
			{0xe0, 0x5e},        /* make */
		}, {  /* set 2 */
			{0xe0, 0xf0, 0x37},  /* break */
			{0xe0, 0x37},        /* make */
		}
	};

	power_button_pressed = pressed;

	/* Only send the scan code if main chipset is fully awake */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	code_set = acting_code_set(scancode_set);
	ret = i8042_send_to_host(
		 (code_set == SCANCODE_SET_2 && !pressed) ? 3 : 2,
		 code[code_set - SCANCODE_SET_1][pressed]);
	ASSERT(ret == EC_SUCCESS);
}


static int command_codeset(int argc, char **argv)
{
	int set;

	if (argc == 1) {
		uart_printf("Current scancode set: %d\n", scancode_set);
		uart_printf("I8042_XLATE: %d\n",
		            controller_ram[0] & I8042_XLATE ? 1 : 0);
	} else if (argc == 2) {
		set = strtoi(argv[1], NULL, 0);
		switch (set) {
		case SCANCODE_SET_1:  /* fall-thru */
		case SCANCODE_SET_2:  /* fall-thru */
			scancode_set = set;
			uart_printf("Set scancode set to %d\n", scancode_set);
			break;
		default:
			uart_printf("Scancode %d is NOT supported.\n", set);
			return EC_ERROR_UNKNOWN;
			break;
		}
	} else {
		uart_puts("Usage: codeset [<set>]\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_flush_output();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(codeset, command_codeset);


static int command_controller_ram(int argc, char **argv)
{
	int index;

	if (argc >= 2) {
		index = strtoi(argv[1], NULL, 0);
		uart_printf("Controller RAM index = %d\n", index);
		if (index >= 0x20) {
			uart_printf("Index is out of range (0x00-0x1f).\n");
			return EC_ERROR_UNKNOWN;
		}

		if (argc >= 3) {
			update_ctl_ram(index, strtoi(argv[2], NULL, 0));
			uart_printf("Write ctlram[%d] as 0x%02x.\n",
			            index, controller_ram[index]);
		} else {
			uart_printf("ctlram[%d] is 0x%02x.\n",
			            index, controller_ram[index]);
		}
	} else {
		uart_puts("Usage: ctrlram <index> [<write_value>]\n");
		uart_puts("\nGet/set controller RAM.\n\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_flush_output();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ctrlram, command_controller_ram);


static int command_keyboard_press(int argc, char **argv)
{
	int i, j;
	int r, c, p;
	char *e;
	if (argc == 1) {
		uart_puts("Simulated key:\n");
		for (i = 0; i < CROS_COL_NUM; ++i) {
			if (simulated_key[i] == 0)
				continue;
			for (j = 0; j < CROS_ROW_NUM; ++j)
				if (simulated_key[i] & (1 << j))
					uart_printf("\t%d %d\n", i, j);
		}
	} else if (argc == 4) {
		c = strtoi(argv[1], &e, 0);
		if ((e && *e) || c < 0 || c >= CROS_COL_NUM) {
			uart_puts("Bad column.\n");
			return EC_ERROR_UNKNOWN;
		}

		r = strtoi(argv[2], &e, 0);
		if ((e && *e) || r < 0 || r >= CROS_ROW_NUM) {
			uart_puts("Bad row.\n");
			return EC_ERROR_UNKNOWN;
		}

		p = strtoi(argv[3], &e, 0);
		if ((e && *e) || p < 0 || p > 1) {
			uart_puts("Bad pressed flag.\n");
			return EC_ERROR_UNKNOWN;
		}

		if ((simulated_key[c] & (1 << r)) == (p << r))
			return EC_SUCCESS;

		simulated_key[c] = (simulated_key[c] & ~(1 << r)) | (p << r);

		keyboard_state_changed(r, c, p);
	} else {
		uart_puts("Usage: kbpress [<col> <row> <pressed>]\n");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbpress, command_keyboard_press);
