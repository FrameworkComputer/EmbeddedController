#include <console.h>
#include <i2c.h>
#include <system.h>
#include <util.h>

#include <gl3590.h>

/* GL3590 is unique in terms of i2c_read, since it doesn't support repeated
 * start sequence. One need to issue two separate transactions - first is write
 * with a register offset, then after a delay second transaction is actual read.
 */
int gl3590_read(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       &reg, 1,
			       NULL, 0,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	if (rv)
		return rv;

	/* GL3590 requires at least 300us before data is ready */
	udelay(400);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       NULL, 0,
			       data, count,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	return rv;
};

int gl3590_write(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	uint8_t buf[5];
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	/* GL3590 registers accept 4 bytes at max */
	if (count > (sizeof(buf) - 1)) {
		ccprintf("Too many bytes to write");
		return EC_ERROR_INVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], data, count);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       buf, count + 1,
			       NULL, 0,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	return rv;
}

void gl3590_irq_handler(int hub)
{
	uint8_t buf = 0;
	uint8_t res_reg[2];

	/* Verify that irq is pending */
	if (gl3590_read(hub, GL3590_INT_REG, &buf, sizeof(buf))) {
		ccprintf("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((buf & GL3590_INT_PENDING) == 0) {
		ccprintf("Invalid hub event\n");
		goto exit;
	}

	/* Get the hub event reason */
	if (gl3590_read(hub, GL3590_RESPONSE_REG, res_reg, sizeof(res_reg))) {
		ccprintf("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((res_reg[0] & GL3590_RESPONSE_REG_SYNC_MASK) == 0)
		ccprintf("Host hub response: ");
	else
		ccprintf("Host hub event! ");

	switch(res_reg[0]) {
	case 0x0:
		ccprintf("No response");
		break;
	case 0x1:
		ccprintf("Successful");
		break;
	case 0x2:
		ccprintf("Invalid command");
		break;
	case 0x3:
		ccprintf("Invalid arguments");
		break;
	case 0x4:
		ccprintf("Invalid port: %d", res_reg[1]);
		break;
	case 0x5:
		ccprintf("Command not completed");
		break;
	case 0x80:
		ccprintf("Reset complete");
		break;
	case 0x81:
		ccprintf("Power operation mode change");
		break;
	case 0x82:
		ccprintf("Connect change");
		break;
	case 0x83:
		ccprintf("Error on the specific port");
		break;
	case 0x84:
		ccprintf("Hub state change");
		break;
	case 0x85:
		ccprintf("SetFeature PORT_POWER failure");
		break;
	default:
		ccprintf("Unknown value: 0x%0x", res_reg[0]);
	}
	ccprintf("\n");

	if (res_reg[1])
		ccprintf("Affected port %d\n", res_reg[1]);

exit:
	/* Try to clear interrupt */
	buf = GL3590_INT_CLEAR;
	gl3590_write(hub, GL3590_INT_REG, &buf, sizeof(buf));
}
