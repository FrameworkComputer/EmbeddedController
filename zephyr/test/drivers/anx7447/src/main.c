/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "driver/tcpm/anx7447.h"
#include "emul/tcpc/emul_anx7447.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/anx7447_public.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(pd_got_frs_signal, int);
FAKE_VOID_FUNC(tcpc_dump_std_registers, int);

#define FFF_FAKES_LIST(FAKE)    \
	FAKE(pd_got_frs_signal) \
	FAKE(tcpc_dump_std_registers)

#define ANX7447_NODE DT_NODELABEL(anx7447_emul)
#define PORT 0
static const struct emul *emul = EMUL_DT_GET(ANX7447_NODE);
const struct usb_mux *m;

struct anx7447_fixture {
	struct i2c_common_emul_data *common;
};

static void anx7447_reset(void *fixture)
{
	struct anx7447_fixture *f = fixture;

	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();

	tcpc_config[PORT].drv->init(PORT);

	anx7447_emul_reset(emul);
	i2c_common_emul_set_read_fail_reg(f->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(f->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	shell_execute_cmd(get_ec_shell(), "chan restore");
}

static void *anx7447_setup(void)
{
	static struct anx7447_fixture fix;

	m = usb_muxes[PORT].mux;

	fix.common = anx7447_emul_get_i2c_common_data(emul);

	return &fix;
}

ZTEST_SUITE(anx7447, drivers_predicate_post_main, anx7447_setup, anx7447_reset,
	    anx7447_reset, NULL);

ZTEST(anx7447, test_mux_init)
{
	mux_state_t ms;

	zassert_ok(tcpc_config[PORT].drv->init(PORT));
	zassert_ok(m->driver->init(m));
	zassert_equal(m->driver->get(m, &ms), EC_SUCCESS);
	zassert_equal(ms, USB_PD_MUX_NONE);
}

ZTEST(anx7447, test_mux)
{
	mux_state_t ms;
	bool ack;
	const uint32_t combs[] = { USB_PD_MUX_DP_ENABLED,
				   USB_PD_MUX_USB_ENABLED, USB_PD_MUX_DOCK };
	const uint32_t dirs[] = { 0, USB_PD_MUX_POLARITY_INVERTED };

	zassert_ok(tcpc_config[PORT].drv->init(PORT));
	zassert_ok(m->driver->init(m));

	/*set safe mode  */
	ms = USB_PD_MUX_SAFE_MODE;
	zassert_equal(m->driver->set(m, ms, &ack), EC_SUCCESS);
	zassert_equal(m->driver->get(m, &ms), EC_SUCCESS);
	zassert_equal(ms, USB_PD_MUX_NONE);

	/* test all other combinations */
	for (int i = 0; i < ARRAY_SIZE(dirs); i++) {
		for (int j = 0; j < ARRAY_SIZE(combs); j++) {
			ms = dirs[i] | combs[j];
			zassert_equal(m->driver->set(m, ms, &ack), EC_SUCCESS);
			ms = USB_PD_MUX_NONE;
			zassert_equal(m->driver->get(m, &ms), EC_SUCCESS);
			zassert_equal(ms, dirs[i] | combs[j]);
		}
	}
}

ZTEST(anx7447, test_tcpc_alert_frs)
{
	uint16_t reg;

	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, TCPC_REG_ALERT_VENDOR_DEF);
	tcpci_emul_get_reg(emul, TCPC_REG_ALERT, &reg);
	zassert_equal(reg, TCPC_REG_ALERT_VENDOR_DEF);

	anx7447_emul_set_tcpci_extra_reg(emul, ANX7447_REG_VD_ALERT,
					 ANX7447_FRSWAP_SIGNAL_DETECTED);

	zassert_equal(pd_got_frs_signal_fake.call_count, 0);
	tcpc_config[PORT].drv->tcpc_alert(PORT);
	zassert_equal(pd_got_frs_signal_fake.call_count, 1);
}

ZTEST(anx7447, test_tcpc_drp_toggle)
{
	uint16_t reg;

	anx7447_emul_set_tcpci_extra_reg(emul, ANX7447_REG_ANALOG_CTRL_10,
					 ANX7447_REG_CABLE_DET_DIG);

	tcpc_config[PORT].drv->drp_toggle(PORT);

	anx7447_emul_set_tcpci_extra_reg(emul, ANX7447_REG_VD_ALERT,
					 ANX7447_FRSWAP_SIGNAL_DETECTED);

	reg = anx7447_emul_peek_tcpci_extra_reg(emul,
						ANX7447_REG_ANALOG_CTRL_10);

	zassert_equal(reg & ANX7447_REG_CABLE_DET_DIG, 0);
}

ZTEST(anx7447, test_set_frs_enable)
{
	uint16_t reg;

	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_ADDR_GPIO_CTRL_1);
	zassert_equal(!!(reg & ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA), 0);
	tcpc_config[PORT].drv->set_frs_enable(PORT, 1);
	anx7447_emul_set_tcpci_extra_reg(emul, ANX7447_REG_VD_ALERT,
					 ANX7447_FRSWAP_SIGNAL_DETECTED);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_ADDR_GPIO_CTRL_1);
	zassert_equal(!!(reg & ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA), 1);

	tcpc_config[PORT].drv->set_frs_enable(PORT, 0);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_ADDR_GPIO_CTRL_1);
	zassert_equal(!!(reg & ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA), 1);

	/* deferred disable */
	k_sleep(K_SECONDS(1));
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_ADDR_GPIO_CTRL_1);
	zassert_equal(reg & ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA, 0);
}

ZTEST(anx7447, test_tcpc_update_hpd_status)
{
	uint16_t reg;
	mux_state_t ms;
	bool ack = true;

	m->driver->init(m);

	ms = USB_PD_MUX_HPD_IRQ | USB_PD_MUX_HPD_LVL;
	anx7447_tcpc_update_hpd_status(m, ms, &ack);

	zassert_false(ack);

	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_HPD_CTRL_0);
	zassert_equal(!!(reg & ANX7447_REG_HPD_PLUG), 1);
	zassert_equal(!!(reg & ANX7447_REG_HPD_IRQ0), 1);

	/* toggle hpd immediately */
	anx7447_emul_set_spi_reg(emul, ANX7447_REG_HPD_CTRL_0, 0);
	anx7447_tcpc_update_hpd_status(m, ms, &ack);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_HPD_CTRL_0);
	zassert_equal(!!(reg & ANX7447_REG_HPD_PLUG), 1);
	zassert_equal(!!(reg & ANX7447_REG_HPD_IRQ0), 1);

	ms = USB_PD_MUX_HPD_IRQ_DEASSERTED | USB_PD_MUX_HPD_LVL_DEASSERTED;
	anx7447_tcpc_update_hpd_status(m, ms, &ack);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_HPD_CTRL_0);
	zassert_equal(!!(reg & ANX7447_REG_HPD_PLUG), 0);
}

ZTEST(anx7447, test_get_chip_info)
{
	struct ec_response_pd_chip_info_v1 chip_info;

	anx7447_emul_set_spi_reg(emul, ANX7447_REG_OCM_MAIN_VERSION, 0x01);
	anx7447_emul_set_spi_reg(emul, ANX7447_REG_OCM_BUILD_VERSION, 0x15);

	tcpc_config[PORT].drv->get_chip_info(PORT, 1, &chip_info);

	zassert_equal(chip_info.fw_version_number, 0x0115);
	zassert_equal(chip_info.min_req_fw_version_number, 0x0115);
}

ZTEST(anx7447, test_dump_registers)
{
	tcpc_config[PORT].drv->dump_registers(PORT);
	zassert_equal(tcpc_dump_std_registers_fake.call_count, 1);
}
ZTEST(anx7447, test_release)
{
	zassert_ok(tcpc_config[PORT].drv->release(PORT));
}

ZTEST(anx7447, test_command_flash_erase)
{
	int rv;
	int reg;

	rv = shell_execute_cmd(get_ec_shell(), "chan 0");

	/* flash empty */
	anx7447_emul_set_spi_reg(emul, ANX7447_REG_OCM_MAIN_VERSION, 0x0);
	CHECK_CONSOLE_CMD("anx_ocm 0 erase", "C0: OCM flash is empty.",
			  EC_SUCCESS);

	anx7447_emul_set_spi_reg(emul, ANX7447_REG_OCM_MAIN_VERSION, 0x1);
	/* set erasing flash instant done */
	anx7447_emul_set_spi_reg(emul, ANX7447_REG_R_RAM_CTRL,
				 ANX7447_R_RAM_CTRL_FLASH_DONE);
	CHECK_CONSOLE_CMD("anx_ocm 0 erase", "C0: OCM flash is not empty.",
			  EC_SUCCESS);

	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_FLASH_INST_TYPE);
	zassert_equal(!!(reg & ANX7447_FLASH_INST_TYPE_WRITEENABLE), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_R_FLASH_RW_CTRL);
	zassert_equal(!!(reg & ANX7447_R_FLASH_RW_CTRL_GENERAL_INST_EN), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_FLASH_ERASE_TYPE);
	zassert_equal(!!(reg & ANX7447_FLASH_ERASE_TYPE_CHIPERASE), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_R_FLASH_RW_CTRL);
	zassert_equal(!!(reg & ANX7447_R_FLASH_RW_CTRL_FLASH_ERASE_EN), 1);

	CHECK_CONSOLE_CMD("anx_ocm 0 erase", "C0: OCM flash is not empty.",
			  EC_SUCCESS);
}

ZTEST(anx7447, test_flash_erase)
{
	int reg;

	anx7447_emul_set_spi_reg(emul, ANX7447_REG_OCM_MAIN_VERSION, 0x1);
	/* set erasing flash instant done */
	anx7447_emul_set_spi_reg(emul, ANX7447_REG_R_RAM_CTRL,
				 ANX7447_R_RAM_CTRL_FLASH_DONE);

	zassert_equal(anx7447_flash_erase(PORT), EC_SUCCESS);

	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_FLASH_INST_TYPE);
	zassert_equal(!!(reg & ANX7447_FLASH_INST_TYPE_WRITEENABLE), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_R_FLASH_RW_CTRL);
	zassert_equal(!!(reg & ANX7447_R_FLASH_RW_CTRL_GENERAL_INST_EN), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_FLASH_ERASE_TYPE);
	zassert_equal(!!(reg & ANX7447_FLASH_ERASE_TYPE_CHIPERASE), 1);
	reg = anx7447_emul_peek_spi_reg(emul, ANX7447_REG_R_FLASH_RW_CTRL);
	zassert_equal(!!(reg & ANX7447_R_FLASH_RW_CTRL_FLASH_ERASE_EN), 1);
}
