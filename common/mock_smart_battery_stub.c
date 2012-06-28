#include "console.h"
#include "smart_battery.h"
#include "smart_battery_stub.h"
#include "uart.h"
#include "util.h"

static int mock_temperature = 2981;
static int mock_desire_voltage = 7000;
static int mock_desire_current = 3000;
static int mock_voltage = 6000;
static int mock_current = 3000;

int sb_read(int cmd, int *param)
{
	switch (cmd)
	{
	case SB_TEMPERATURE:
		*param = mock_temperature;
		break;
	case SB_VOLTAGE:
		*param = mock_voltage;
		break;
	case SB_CURRENT:
		*param = mock_current;
		break;
	case SB_RELATIVE_STATE_OF_CHARGE:
	case SB_ABSOLUTE_STATE_OF_CHARGE:
		*param = 70; /* 70% charged */
		break;
	case SB_REMAINING_CAPACITY:
		*param = 7000; /* 7000 mAh */
		break;
	case SB_FULL_CHARGE_CAPACITY:
	case SB_DESIGN_CAPACITY:
		*param = 10000; /* 10000 mAh */
		break;
	case SB_AVERAGE_TIME_TO_EMPTY:
	case SB_RUN_TIME_TO_EMPTY:
		*param = 60; /* 60 min to empty */
		break;
	case SB_AVERAGE_TIME_TO_FULL:
		*param = 30; /* 30 min to full */
		break;
	case SB_CHARGING_CURRENT:
		*param = mock_desire_current;
		break;
	case SB_CHARGING_VOLTAGE:
		*param = mock_desire_voltage;
		break;
	case SB_CYCLE_COUNT:
		*param = 10;
		break;
	case SB_DESIGN_VOLTAGE:
		*param = 7400; /* 7.4 V */
		break;
	case SB_SERIAL_NUMBER:
		*param = 112233;
		break;
	default:
		*param = 0;
		break;
	}

	return EC_SUCCESS;
}


int sb_write(int cmd, int param)
{
	uart_printf("sb_write: cmd = %d, param = %d\n", cmd, param);
	return EC_SUCCESS;
}


static int command_sb_mock(int argc, char **argv)
{
	char *e;
	int v;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[1], "temperature"))
		mock_temperature = v;
	else if (!strcasecmp(argv[1], "desire_voltage"))
		mock_desire_voltage = v;
	else if (!strcasecmp(argv[1], "desire_current"))
		mock_desire_current = v;
	else if (!strcasecmp(argv[1], "voltage"))
		mock_voltage = v;
	else if (!strcasecmp(argv[1], "current"))
		mock_current = v;
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sbmock, command_sb_mock,
			"name value",
			"Mock smart battery attribute",
			NULL);
