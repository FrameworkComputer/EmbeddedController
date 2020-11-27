/* Registers definitions */
#define GL3590_INT_REG			0x1
#define GL3590_INT_PENDING		0x1
#define GL3590_INT_CLEAR		0x1
#define GL3590_RESPONSE_REG		0x2
#define GL3590_RESPONSE_REG_SYNC_MASK	0x80

#define GL3590_I2C_ADDR0 0x50

int gl3590_read(int hub, uint8_t reg, uint8_t *data, int count);
int gl3590_write(int hub, uint8_t reg, uint8_t *data, int count);
void gl3590_irq_handler(int hub);

/* Generic USB HUB I2C interface */
struct uhub_i2c_iface_t {
	int i2c_host_port;
	int i2c_addr;
};
extern struct uhub_i2c_iface_t uhub_config[];
