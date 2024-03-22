/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_syv682x.h"
#include "syv682x.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usbc_ppc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#define SYV682X_NODE DT_NODELABEL(syv682x_emul)
#define GPIO_USB_C1_FRS_EN_PATH NAMED_GPIOS_GPIO_NODE(usb_c1_frs_en)

struct ppc_syv682x_fixture {
	const struct emul *ppc_emul;
	struct i2c_common_emul_data *common_data;
	const struct device *frs_en_gpio_port;
	int frs_en_gpio_pin;
};

/* Configuration for a mock I2C access function that sometimes fails. */
struct reg_to_fail_data {
	int reg_access_to_fail;
	int reg_access_fail_countdown;
};

static const int syv682x_port = 1;

static void *syv682x_test_setup(void)
{
	static struct ppc_syv682x_fixture fixture;

	fixture.ppc_emul = EMUL_DT_GET(SYV682X_NODE);
	fixture.common_data =
		emul_syv682x_get_i2c_common_data(fixture.ppc_emul);
	zassert_not_null(fixture.ppc_emul, NULL);
	fixture.frs_en_gpio_port =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_FRS_EN_PATH, gpios));
	zassert_not_null(fixture.frs_en_gpio_port, NULL);
	fixture.frs_en_gpio_pin = DT_GPIO_PIN(GPIO_USB_C1_FRS_EN_PATH, gpios);

	return &fixture;
}

static void syv682x_test_before(void *data)
{
	/* Reset ppc_discharge_vbus to disable to clear the cached force
	 * discharge bit in the CONTROL_2 register
	 */
	ppc_discharge_vbus(syv682x_port, false);
}

static void syv682x_test_after(void *data)
{
	struct ppc_syv682x_fixture *fixture = data;
	const struct emul *emul = fixture->ppc_emul;
	struct i2c_common_emul_data *common_data = fixture->common_data;

	/* Disable the power path and clear interrupt conditions. */
	zassert_ok(syv682x_emul_set_reg(emul, SYV682X_CONTROL_1_REG,
					SYV682X_CONTROL_1_PWR_ENB),
		   NULL);
	syv682x_emul_set_condition(emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_NONE);

	/* Clear the mock read/write functions */
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
	i2c_common_emul_set_write_func(common_data, NULL, NULL);

	/* Don't fail on any register access */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_SUITE(ppc_syv682x, drivers_predicate_post_main, syv682x_test_setup,
	    syv682x_test_before, syv682x_test_after, NULL);

ZTEST_F(ppc_syv682x, test_syv682x_board_is_syv682c)
{
	/*
	 * The SYV682x driver should assume a version-C part in the absence of a
	 * board override.
	 */
	zassert_true(syv682x_board_is_syv682c(syv682x_port));
}

static void check_control_1_default_init(uint8_t control_1)
{
	/*
	 * During init, when not in dead battery mode, the driver should
	 * configure the high-voltage channel as sink but leave the power path
	 * disabled. The driver should set the current limits according to
	 * configuration.
	 */
	int ilim;

	zassert_true(control_1 & SYV682X_CONTROL_1_PWR_ENB,
		     "Default init, but power path enabled");
	ilim = (control_1 & SYV682X_HV_ILIM_MASK) >> SYV682X_HV_ILIM_BIT_SHIFT;
	zassert_equal(ilim, CONFIG_PLATFORM_EC_USBC_PPC_SYV682X_HV_ILIM,
		      "Default init, but HV current limit set to %d", ilim);
	zassert_false(control_1 & SYV682X_CONTROL_1_HV_DR,
		      "Default init, but source mode selected");
	zassert_true(control_1 & SYV682X_CONTROL_1_CH_SEL,
		     "Default init, but 5V power path selected");
}

ZTEST_F(ppc_syv682x, test_syv682x_init_dead_battery)
{
	uint8_t reg;

	/*
	 * With a dead battery, the device powers up sinking VBUS, and the
	 * driver should keep that going.
	 */
	zassert_ok(syv682x_emul_set_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG,
					SYV682X_CONTROL_1_CH_SEL),
		   NULL);
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_VSAFE_5V,
				   SYV682X_CONTROL_4_NONE);
	zassert_ok(ppc_init(syv682x_port), "PPC init failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);
	zassert_true(reg & SYV682X_CONTROL_1_CH_SEL,
		     "Dead battery init, but CH_SEL set to 5V power path");
	zassert_false(reg & (SYV682X_CONTROL_1_PWR_ENB |
			     SYV682X_CONTROL_1_HV_DR),
		      "Dead battery init, but CONTROL_1 is 0x%x", reg);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "Dead battery init, but VBUS source enabled");
}

ZTEST_F(ppc_syv682x, test_syv682x_init_vsafe0v)
{
	uint8_t reg;

	/* With VBUS at vSafe0V, init should set the default configuration. */
	zassert_ok(syv682x_emul_set_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG,
					SYV682X_CONTROL_1_PWR_ENB),
		   NULL);
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_VSAFE_0V,
				   SYV682X_CONTROL_4_NONE);
	zassert_ok(ppc_init(syv682x_port), "PPC init failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);
	check_control_1_default_init(reg);
}

ZTEST_F(ppc_syv682x, test_syv682x_init_sink_disabled)
{
	uint8_t reg;

	/* With sink disabled, init should do the same thing. */
	zassert_ok(syv682x_emul_set_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG,
					SYV682X_CONTROL_1_CH_SEL),
		   NULL);
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_VSAFE_0V,
				   SYV682X_CONTROL_4_NONE);
	zassert_ok(ppc_init(syv682x_port), "PPC init failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);
	check_control_1_default_init(reg);
}

ZTEST_F(ppc_syv682x, test_syv682x_init_common)
{
	uint8_t reg;
	int ilim;

	zassert_ok(ppc_init(syv682x_port), "PPC init failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);

	/*
	 * Any init sequence should also disable the FRS GPIO, set the 5V
	 * current limit according to configuration, set over-current, over-
	 * voltage, and discharge parameters appropriately, and enable CC lines.
	 */
	zassert_equal(gpio_emul_output_get(fixture->frs_en_gpio_port,
					   fixture->frs_en_gpio_pin),
		      0, "FRS enabled, but FRS GPIO not asserted");
	ilim = (reg & SYV682X_5V_ILIM_MASK) >> SYV682X_5V_ILIM_BIT_SHIFT;
	zassert_equal(ilim, CONFIG_PLATFORM_EC_USB_PD_PULLUP,
		      "Default init, but 5V current limit set to %d", ilim);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_2_REG, &reg),
		   NULL);
	zassert_equal(reg,
		      (SYV682X_OC_DELAY_10MS << SYV682X_OC_DELAY_SHIFT) |
			      (SYV682X_DSG_RON_200_OHM
			       << SYV682X_DSG_RON_SHIFT) |
			      (SYV682X_DSG_TIME_50MS << SYV682X_DSG_TIME_SHIFT),
		      "Default init, but CONTROL_2 is 0x%x", reg);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_3_REG, &reg),
		   NULL);
	zassert_equal(reg,
		      (SYV682X_OVP_23_7 << SYV682X_OVP_BIT_SHIFT) |
			      SYV682X_RVS_MASK,
		      "Default init, but CONTROL_3 is 0x%x", reg);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   NULL);
	zassert_equal(reg & ~SYV682X_CONTROL_4_INT_MASK,
		      SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS,
		      "Default init, but CONTROL_4 is 0x%x", reg);
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_source_enable)
{
	uint8_t reg;

	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
		   "VBUS enable failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB, 0,
		      "VBUS sourcing enabled but power path disabled");
	zassert_true(ppc_is_sourcing_vbus(syv682x_port),
		     "PPC is not sourcing VBUS after VBUS enabled");
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_source_disable)
{
	zassert_ok(ppc_vbus_source_enable(syv682x_port, false),
		   "VBUS disable failed");
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC sourcing VBUS after disable");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_source_oc)
{
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
		   "VBUS enable failed");
	/* An OC event less than 100 ms should not cause VBUS to turn off. */
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_OC_5V,
				   SYV682X_CONTROL_4_NONE);
	msleep(50);
	zassert_true(ppc_is_sourcing_vbus(syv682x_port),
		     "PPC is not sourcing VBUS after 50 ms OC");
	/* But one greater than 100 ms should. */
	msleep(60);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC is sourcing VBUS after 100 ms OC");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_tsd)
{
	/*
	 * A TSD event should cause the driver to disable source and sink paths.
	 * (The device will have already physically disabled them.) The state of
	 * the sink path is not part of the driver's API.
	 */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
		   "Source enable failed");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_TSD,
				   SYV682X_CONTROL_4_NONE);
	msleep(1);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC is sourcing power after TSD");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_vbus_ovp)
{
	/* An OVP event should cause the driver to disable the source path. */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
		   "Source enable failed");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_OVP,
				   SYV682X_CONTROL_4_NONE);
	msleep(1);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC is sourcing power after OVP");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_vbus_hv_oc)
{
	uint8_t reg;

	/*
	 * A high-voltage OC while sinking should cause the driver to try to
	 * re-enable the sink path until the OC count limit is reached, at which
	 * point the driver should leave it disabled.
	 */
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, true),
		   "Sink enable failed");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_OC_HV,
				   SYV682X_CONTROL_4_NONE);
	msleep(1);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB, 0,
		      "Power path disabled after HV_OC handled");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_OC_HV,
				   SYV682X_CONTROL_4_NONE);
	/* Alert GPIO doesn't change so wait for delayed syv682x interrupt */
	msleep(15);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB, 0,
		      "Power path disabled after HV_OC handled");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_OC_HV,
				   SYV682X_CONTROL_4_NONE);
	/* Alert GPIO doesn't change so wait for delayed syv682x interrupt */
	msleep(15);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	zassert_equal(reg & SYV682X_CONTROL_1_PWR_ENB,
		      SYV682X_CONTROL_1_PWR_ENB,
		      "Power path enabled after HV_OC handled 3 times");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_vconn_oc)
{
	uint8_t reg;

	/*
	 * A VCONN OC event less than 100 ms should not cause the driver to turn
	 * VCONN off.
	 */
	ppc_set_vconn(syv682x_port, true);
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_VCONN_OCP);
	msleep(1);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_true(reg & (SYV682X_CONTROL_4_VCONN1 |
			    SYV682X_CONTROL_4_VCONN2),
		     "VCONN disabled after initial VCONN OC");
	msleep(50);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_true(reg & (SYV682X_CONTROL_4_VCONN1 |
			    SYV682X_CONTROL_4_VCONN2),
		     "VCONN disabled after short VCONN OC");
	/*
	 * But if the event keeps going for over 100 ms continuously, the driver
	 * should turn VCONN off.
	 */
	msleep(60);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_false(reg & (SYV682X_CONTROL_4_VCONN1 |
			     SYV682X_CONTROL_4_VCONN2),
		      "VCONN enabled after long VCONN OC");
}

ZTEST_F(ppc_syv682x, test_syv682x_interrupt_vconn_ov)
{
	uint8_t reg;

	/*
	 * A VCONN over-voltage (VBAT_OVP) event will cause the device to
	 * disconnect CC and VCONN. The driver should then reinitialize the
	 * device, which will enable both CC lines but leave VCONN disabled. The
	 * driver should then run generic CC over-voltage handling.
	 */
	ppc_set_vconn(syv682x_port, true);
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_VBAT_OVP);
	msleep(1);
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_true(reg & SYV682X_CONTROL_4_CC1_BPS,
		     "CC1 disabled after handling VBAT_OVP");
	zassert_true(reg & SYV682X_CONTROL_4_CC2_BPS,
		     "CC2 disabled after handling VBAT_OVP");
	zassert_false(reg & (SYV682X_CONTROL_4_VCONN1 |
			     SYV682X_CONTROL_4_VCONN2),
		      "VCONN enabled after handling VBAT_OVP");
}

ZTEST_F(ppc_syv682x, test_syv682x_frs_enable)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_FRS_EN_PATH, gpios));
	uint8_t reg;

	/*
	 * Enabling FRS should enable only the appropriate CC line based on
	 * polarity. Disabling FRS should enable both CC lines.
	 */
	ppc_vbus_sink_enable(syv682x_port, true);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC is sourcing VBUS after sink enabled");
	ppc_set_polarity(syv682x_port, 0 /* CC1 */);
	ppc_set_frs_enable(syv682x_port, true);
	zassert_equal(gpio_emul_output_get(gpio_dev, fixture->frs_en_gpio_pin),
		      1, "FRS enabled, but FRS GPIO not asserted");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_equal(
		reg & (SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS),
		SYV682X_CONTROL_4_CC1_BPS,
		"FRS enabled with CC1 polarity, but CONTROL_4 is 0x%x", reg);
}

ZTEST_F(ppc_syv682x, test_syv682x_frs_disable)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_FRS_EN_PATH, gpios));
	uint8_t reg;

	ppc_vbus_sink_enable(syv682x_port, true);
	zassert_false(ppc_is_sourcing_vbus(syv682x_port),
		      "PPC is sourcing VBUS after sink enabled");
	ppc_set_polarity(syv682x_port, 0 /* CC1 */);

	ppc_set_frs_enable(syv682x_port, false);
	zassert_equal(gpio_emul_output_get(gpio_dev, fixture->frs_en_gpio_pin),
		      0, "FRS disabled, but FRS GPIO not deasserted");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_4_REG, &reg),
		   "Reading CONTROL_4 failed");
	zassert_equal(
		reg & (SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS),
		SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS,
		"FRS disabled with CC1 polarity, but CONTROL_4 is 0x%x", reg);
}

ZTEST_F(ppc_syv682x, test_syv682x_frs_trigger)
{
	/*
	 * An FRS event when the PPC is Sink should cause the PPC to switch from
	 * Sink to Source.
	 */
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_FRS,
				   SYV682X_CONTROL_4_NONE);
	msleep(1);
	zassert_true(ppc_is_sourcing_vbus(syv682x_port),
		     "PPC is not sourcing VBUS after FRS signal handled");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_NONE);
}

ZTEST_F(ppc_syv682x, test_syv682x_source_current_limit_usb_default)
{
	uint8_t reg;
	int ilim_val;

	zassert_ok(ppc_set_vbus_source_current_limit(syv682x_port,
						     TYPEC_RP_USB),
		   "Could not set source current limit");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	ilim_val = (reg & SYV682X_5V_ILIM_MASK) >> SYV682X_5V_ILIM_BIT_SHIFT;
	zassert_equal(reg & SYV682X_5V_ILIM_MASK, SYV682X_5V_ILIM_1_25,
		      "Set USB Rp value, but 5V_ILIM is %d", ilim_val);
}

ZTEST_F(ppc_syv682x, test_syv682x_source_current_limit_1500ma)
{
	uint8_t reg;
	int ilim_val;

	zassert_ok(ppc_set_vbus_source_current_limit(syv682x_port,
						     TYPEC_RP_1A5),
		   "Could not set source current limit");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	ilim_val = (reg & SYV682X_5V_ILIM_MASK) >> SYV682X_5V_ILIM_BIT_SHIFT;
	zassert_equal(ilim_val, SYV682X_5V_ILIM_1_75,
		      "Set 1.5A Rp value, but 5V_ILIM is %d", ilim_val);
}

ZTEST_F(ppc_syv682x, test_syv682x_source_current_limit_3000ma)
{
	uint8_t reg;
	int ilim_val;

	zassert_ok(ppc_set_vbus_source_current_limit(syv682x_port,
						     TYPEC_RP_3A0),
		   "Could not set source current limit");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   "Reading CONTROL_1 failed");
	ilim_val = (reg & SYV682X_5V_ILIM_MASK) >> SYV682X_5V_ILIM_BIT_SHIFT;
	zassert_equal(ilim_val, SYV682X_5V_ILIM_3_30,
		      "Set 3.0A Rp value, but 5V_ILIM is %d", ilim_val);
}

ZTEST_F(ppc_syv682x, test_syv682x_write_busy)
{
	/*
	 * Writes should fail while the BUSY bit is set, except that writes to
	 * CONTROL_4 should succeed on the SYV682C. 1000 reads is intentionally
	 * many more than the driver is expected to make before reaching its
	 * timeout. It is not a goal of this test to verify the frequency of
	 * polling or the exact value of the timeout.
	 */
	syv682x_emul_set_busy_reads(fixture->ppc_emul, 1000);
	zassert_equal(ppc_set_vbus_source_current_limit(syv682x_port,
							TYPEC_RP_USB),
		      EC_ERROR_TIMEOUT, "SYV682 busy, but write completed");
	zassert_ok(ppc_set_frs_enable(syv682x_port, false),
		   "Could not set CONTROL_4 while busy on SYV682C");

	/*
	 * If the busy bit clears before the driver reaches its timeout, the
	 * write should succeed.
	 */
	syv682x_emul_set_busy_reads(fixture->ppc_emul, 1);
	zassert_equal(ppc_set_vbus_source_current_limit(syv682x_port,
							TYPEC_RP_USB),
		      0, "SYV682 not busy, but write failed");

	syv682x_emul_set_busy_reads(fixture->ppc_emul, 0);
}

ZTEST_F(ppc_syv682x, test_syv682x_dev_is_connected)
{
	uint8_t reg;

	zassert_ok(ppc_dev_is_connected(syv682x_port, PPC_DEV_SRC),
		   "Could not connect device as source");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_2_REG, &reg),
		   "Reading CONTROL_2 failed");
	zassert_false(reg & SYV682X_CONTROL_2_FDSG,
		      "Connected as source, but force discharge enabled");

	zassert_ok(ppc_dev_is_connected(syv682x_port, PPC_DEV_DISCONNECTED),
		   "Could not disconnect device");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_2_REG, &reg),
		   "Reading CONTROL_2 failed");
	zassert_true(reg & SYV682X_CONTROL_2_FDSG,
		     "Disconnected, but force discharge disabled");

	zassert_ok(ppc_dev_is_connected(syv682x_port, PPC_DEV_SNK),
		   "Could not connect device as source");
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_sink_enable_trivial)
{
	/*
	 * If VBUS source is already enabled, disabling VBUS sink should
	 * trivially succeed.
	 */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, true),
		   "VBUS enable failed");
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, false),
		   "Sink disable failed");
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_sink_enable_power_path)
{
	uint8_t reg;
	int ilim;

	/*
	 * After enabling VBUS sink, the HV power path should be enabled in sink
	 * mode with the configured current limit.
	 */
	zassert_ok(ppc_vbus_source_enable(syv682x_port, false),
		   "VBUS enable failed");
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, true),
		   "Sink disable failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);
	zassert_true(reg & SYV682X_CONTROL_1_CH_SEL,
		     "Sink enabled, but CH_SEL set to 5V power path");
	zassert_false(reg & SYV682X_CONTROL_1_PWR_ENB,
		      "Sink enabled, but power path disabled");
	zassert_false(reg & SYV682X_CONTROL_1_HV_DR,
		      "Sink enabled, but high-voltage path in source mode");
	ilim = (reg & SYV682X_HV_ILIM_MASK) >> SYV682X_HV_ILIM_BIT_SHIFT;
	zassert_equal(ilim, CONFIG_PLATFORM_EC_USBC_PPC_SYV682X_HV_ILIM,
		      "Sink enabled, but HV current limit set to %d", ilim);
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_sink_disable)
{
	uint8_t reg;

	zassert_ok(ppc_vbus_source_enable(syv682x_port, false),
		   "VBUS enable failed");
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, true),
		   "Sink disable failed");

	zassert_ok(ppc_vbus_sink_enable(syv682x_port, false),
		   "Sink disable failed");
	zassert_ok(syv682x_emul_get_reg(fixture->ppc_emul,
					SYV682X_CONTROL_1_REG, &reg),
		   NULL);
	zassert_true(reg & SYV682X_CONTROL_1_PWR_ENB,
		     "Sink disabled, but power path enabled");
}

ZTEST_F(ppc_syv682x, test_syv682x_vbus_sink_oc_limit)
{
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, true),
		   "Sink enable failed");

	/* Generate 4 consecutive sink over-current interrupts. After reaching
	 * this count, the driver should prevent sink enable until the count is
	 * cleared by sink disable.
	 */
	for (int i = 0; i < 4; ++i) {
		syv682x_emul_set_condition(fixture->ppc_emul,
					   SYV682X_STATUS_OC_HV,
					   SYV682X_CONTROL_4_NONE);
		msleep(15);
	}
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_NONE);

	zassert_not_equal(ppc_vbus_sink_enable(syv682x_port, true), EC_SUCCESS,
			  "VBUS sink enable succeeded after 4 OC events");

	zassert_ok(ppc_vbus_sink_enable(syv682x_port, false),
		   "Sink disable failed");
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, true),
		   "Sink enable failed");
	zassert_ok(ppc_vbus_sink_enable(syv682x_port, false),
		   "Sink disable failed");
}

ZTEST_F(ppc_syv682x, test_syv682x_set_vconn)
{
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_VBAT_OVP);
	zassert_not_equal(ppc_set_vconn(syv682x_port, true), EC_SUCCESS,
			  "VBAT OVP, but ppc_set_vconn succeeded");
}

ZTEST(ppc_syv682x, test_syv682x_ppc_dump)
{
	/*
	 * The ppc_dump command should succeed for this port. Don't check the
	 * output, since there are no standard requirements for that.
	 */
	const struct ppc_drv *drv = ppc_chips[syv682x_port].drv;

	zassert_ok(drv->reg_dump(syv682x_port), "ppc_dump command failed");
}

/* Intercepts I2C reads as a mock. Fails to read for the register at offset
 * reg_access_to_fail on read number N, where N is the initial value of
 * reg_access_fail_countdown.
 */
static int mock_read_intercept_reg_fail(const struct emul *emul, int reg,
					uint8_t *val, int bytes, void *data)
{
	struct reg_to_fail_data *test_data = data;

	if (reg == test_data->reg_access_to_fail) {
		test_data->reg_access_fail_countdown--;
		if (test_data->reg_access_fail_countdown <= 0)
			return -1;
	}
	return 1;
}

ZTEST_F(ppc_syv682x, test_syv682x_i2c_error_status)
{
	/* Failed STATUS read should cause init to fail. */
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  SYV682X_STATUS_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "STATUS read error, but init succeeded");
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ppc_syv682x, test_syv682x_i2c_error_control_1)
{
	const struct ppc_drv *drv = ppc_chips[syv682x_port].drv;
	struct reg_to_fail_data reg_fail = {
		.reg_access_to_fail = 0,
		.reg_access_fail_countdown = 0,
	};

	/* Failed CONTROL_1 read */
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  SYV682X_CONTROL_1_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_1 read error, but init succeeded");
	zassert_not_equal(ppc_vbus_source_enable(syv682x_port, true),
			  EC_SUCCESS,
			  "CONTROL_1 read error, but VBUS source enable "
			  "succeeded");
	zassert_not_equal(ppc_vbus_sink_enable(syv682x_port, true), EC_SUCCESS,
			  "CONTROL_1 read error, but VBUS sink enable "
			  "succeeded");
	zassert_not_equal(ppc_set_vbus_source_current_limit(syv682x_port,
							    TYPEC_RP_USB),
			  EC_SUCCESS,
			  "CONTROL_1 read error, but set current limit "
			  "succeeded");
	zassert_ok(drv->reg_dump(syv682x_port),
		   "CONTROL_1 read error, and ppc_dump failed");
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Init reads CONTROL_1 several times. The 3rd read happens while
	 * setting the source current limit. Check that init fails when that
	 * read fails.
	 */
	i2c_common_emul_set_read_func(fixture->common_data,
				      &mock_read_intercept_reg_fail, &reg_fail);
	reg_fail.reg_access_to_fail = SYV682X_CONTROL_1_REG;
	reg_fail.reg_access_fail_countdown = 3;
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_1 read error, but init succeeded");
	i2c_common_emul_set_read_func(fixture->common_data, NULL, NULL);

	/* Failed CONTROL_1 write */
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   SYV682X_CONTROL_1_REG);

	/* During init, the driver will write CONTROL_1 either to disable all
	 * power paths (normal case) or to enable the sink path (dead battery
	 * case). vSafe0V in STATUS is one indication of the normal case.
	 */
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_VSAFE_0V,
				   SYV682X_CONTROL_4_NONE);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_1 write error, but init succeeded");
	syv682x_emul_set_condition(fixture->ppc_emul, SYV682X_STATUS_NONE,
				   SYV682X_CONTROL_4_NONE);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_1 write error, but init succeeded");

	zassert_not_equal(ppc_vbus_source_enable(syv682x_port, true),
			  EC_SUCCESS,
			  "CONTROL_1 write error, but VBUS source "
			  "enable succeeded");
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ppc_syv682x, test_syv682x_i2c_error_control_2)
{
	/* Failed CONTROL_2 read */
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  SYV682X_CONTROL_2_REG);
	zassert_not_equal(ppc_discharge_vbus(syv682x_port, true), EC_SUCCESS,
			  "CONTROL_2 read error, but VBUS discharge succeeded");
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Failed CONTROL_2 write */
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   SYV682X_CONTROL_2_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_2 write error, but init succeeded");
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ppc_syv682x, test_syv682x_i2c_error_control_3)
{
	/* Failed CONTROL_3 read */
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  SYV682X_CONTROL_3_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_3 read error, but VBUS discharge succeeded");
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Failed CONTROL_3 write */
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   SYV682X_CONTROL_3_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_3 write error, but init succeeded");
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ppc_syv682x, test_syv682x_i2c_error_control_4)
{
	/* Failed CONTROL_4 read */
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  SYV682X_CONTROL_4_REG);
	zassert_not_equal(ppc_set_vconn(syv682x_port, true), EC_SUCCESS,
			  "CONTROL_2 read error, but VCONN set succeeded");
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Failed CONTROL_4 write */
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   SYV682X_CONTROL_4_REG);
	zassert_not_equal(ppc_init(syv682x_port), EC_SUCCESS,
			  "CONTROL_4 write error, but init succeeded");
	zassert_not_equal(ppc_set_vconn(syv682x_port, true), EC_SUCCESS,
			  "CONTROL_4 write error, but VCONN set succeeded");
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}
