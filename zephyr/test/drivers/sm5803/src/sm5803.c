/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/charger/sm5803.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sm5803.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "extpower.h"
#include "hooks.h"
#include "tcpm/tcpci.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

struct sm5803_fixture {
	const struct emul *tcpci_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_snk_emul_data snk_ext;
};

static void pd_connect_source(struct sm5803_fixture *fixture);
static void pd_disconnect(struct sm5803_fixture *fixture);

LOG_MODULE_REGISTER(sm5803_driver_test, LOG_LEVEL_INF);

static void *sm5803_test_setup(void);
static void sm5803_before_test(void *fixture);
static void sm5803_after_test(void *fixture);

ZTEST_SUITE(sm5803, drivers_predicate_post_main, sm5803_test_setup,
	    sm5803_before_test, sm5803_after_test, NULL);

ZTEST(sm5803, test_sensible_ocpc_configuration)
{
	/*
	 * A lot of OCPC-related code assumes there are exactly two chargers,
	 * with IDs 0 and 1. Verify that this test's configuration yields
	 * compatible configuration, since the driver contains some OCPC-related
	 * code that we want to test.
	 */
	zassert_equal(chg_chips[CHARGER_PRIMARY].drv, &sm5803_drv);
	zassert_equal(chg_chips[CHARGER_SECONDARY].drv, &sm5803_drv);
}

#define CHARGER_NUM CHARGER_PRIMARY
#define SM5803_EMUL EMUL_DT_GET(DT_NODELABEL(sm5803_emul))
#define SM5803_EMUL_SECONDARY EMUL_DT_GET(DT_NODELABEL(sm5803_emul_secondary))

ZTEST(sm5803, test_chip_id)
{
	int id;

	/* Emulator only implements chip revision 3. */
	zassert_ok(sm5803_drv.device_id(CHARGER_NUM, &id));
	zassert_equal(id, 3);

	/* After a successful read, the value is cached. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_CHIP_ID);
	zassert_ok(sm5803_drv.device_id(CHARGER_NUM, &id));
}

struct i2c_log_entry {
	bool write;
	uint8_t i2c_addr;
	uint8_t reg_addr;
	uint8_t value;
};

struct i2c_log {
	struct i2c_log_entry entries[128];
	size_t entries_used;
	size_t entries_asserted;
};

#define LOG_ASSERT(_write, _i2c_addr, _reg_addr, _value)                    \
	do {                                                                \
		zassert_true(log_ptr->entries_asserted <                    \
				     log_ptr->entries_used,                 \
			     "No more I2C transactions to verify "          \
			     "(logged %d)",                                 \
			     log_ptr->entries_used);                        \
		size_t i = log_ptr->entries_asserted++;                     \
		struct i2c_log_entry *entry = &log_ptr->entries[i];         \
                                                                            \
		zassert(entry->write == _write &&                           \
				entry->i2c_addr == _i2c_addr &&             \
				entry->reg_addr == _reg_addr &&             \
				(!_write || entry->value == _value),        \
			"I2C log mismatch",                                 \
			"Transaction %d did not match expectations:\n"      \
			"expected %5s of address %#04x register"            \
			" %#04x with value %#04x\n"                         \
			"   found %s of address %#04x register"             \
			" %#04x with value %#04x",                          \
			i, _write ? "write" : "read", _i2c_addr, _reg_addr, \
			_value, entry->write ? "write" : "read",            \
			entry->i2c_addr, entry->reg_addr, entry->value);    \
	} while (0)

#define LOG_ASSERT_R(_i2c_addr, _reg_addr) \
	LOG_ASSERT(false, _i2c_addr, _reg_addr, 0)
#define LOG_ASSERT_W(_i2c_addr, _reg_addr, _value) \
	LOG_ASSERT(true, _i2c_addr, _reg_addr, _value)
#define LOG_ASSERT_RW(_i2c_addr, _reg_addr, _value)         \
	do {                                                \
		LOG_ASSERT_R(_i2c_addr, _reg_addr);         \
		LOG_ASSERT_W(_i2c_addr, _reg_addr, _value); \
	} while (0)

/*
 * Generate a function for each I2C address to log the correct address, because
 * the target pointer is the same for each I2C address and there's no way to
 * determine from parameters what address we're meant to be.
 */
#define DEFINE_LOG_FNS(name, addr)                                          \
	static int i2c_log_write_##name(const struct emul *target, int reg, \
					uint8_t val, int bytes, void *ctx)  \
	{                                                                   \
		struct i2c_log *log = ctx;                                  \
                                                                            \
		if (log->entries_used >= ARRAY_SIZE(log->entries)) {        \
			LOG_ERR("No space to log I2C write to " #name       \
				" page");                                   \
			return -ENOSPC;                                     \
		}                                                           \
                                                                            \
		zassert_equal(target->bus_type, EMUL_BUS_TYPE_I2C);         \
		log->entries[log->entries_used++] = (struct i2c_log_entry){ \
			.write = true,                                      \
			.i2c_addr = addr,                                   \
			.reg_addr = reg,                                    \
			.value = val,                                       \
		};                                                          \
                                                                            \
		/* Discard write. */                                        \
		return 0;                                                   \
	}                                                                   \
                                                                            \
	static int i2c_log_read_##name(const struct emul *target, int reg,  \
				       uint8_t *val, int bytes, void *ctx)  \
	{                                                                   \
		struct i2c_log *log = ctx;                                  \
                                                                            \
		if (log->entries_used >= ARRAY_SIZE(log->entries)) {        \
			LOG_ERR("No space to log I2C read from " #name      \
				" page");                                   \
			return -ENOSPC;                                     \
		}                                                           \
                                                                            \
		zassert_equal(target->bus_type, EMUL_BUS_TYPE_I2C);         \
		log->entries[log->entries_used++] = (struct i2c_log_entry){ \
			.write = false,                                     \
			.i2c_addr = addr,                                   \
			.reg_addr = reg,                                    \
		};                                                          \
                                                                            \
		/* Fall through to emulator read. */                        \
		return 1;                                                   \
	}

DEFINE_LOG_FNS(main, SM5803_ADDR_MAIN_FLAGS);
DEFINE_LOG_FNS(meas, SM5803_ADDR_MEAS_FLAGS);
DEFINE_LOG_FNS(chg, SM5803_ADDR_CHARGER_FLAGS);
DEFINE_LOG_FNS(test, SM5803_ADDR_TEST_FLAGS);

static void configure_i2c_log(const struct emul *emul, struct i2c_log *log)
{
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_main(emul),
				      i2c_log_read_main, log);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_main(emul),
				       i2c_log_write_main, log);
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_meas(emul),
				      i2c_log_read_meas, log);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_meas(emul),
				       i2c_log_write_meas, log);
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(emul),
				      i2c_log_read_chg, log);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(emul),
				       i2c_log_write_chg, log);
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_test(emul),
				      i2c_log_read_test, log);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_test(emul),
				       i2c_log_write_test, log);
}

static void verify_init_common(struct i2c_log *const log_ptr)
{
	/* Enable LDOs */
	LOG_ASSERT_RW(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_REFERENCE, 0);
	/* Psys DAC */
	LOG_ASSERT_RW(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_PSYS1, 0x05);
	/* ADC sigma delta */
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_CC_CONFIG1, 0x09);
	/* PROCHOT comparators */
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT1, 0x2d);
	/* DPM voltage */
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_DPM_VL_SET_MSB,
		     0x12);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_DPM_VL_SET_LSB,
		     0x04);
	/* Default input current limit */
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_CHG_ILIM, 0x05);
	/* Interrupts */
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_INT1_EN, 0x04);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_INT4_EN, 0x13);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_TINT_HIGH_TH, 0xd1);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_TINT_LOW_TH, 0);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_INT2_EN, 0x80);
	/* Charging is exclusively EC-controlled */
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW2, 0x40);
	/* Battery parameters */
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FAST_CONF5, 0x02);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PRE_FAST_CONF_REG1,
		      0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PRECHG, 0x02);
	/* BFET limits */
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_BFET_PWR_MAX_TH, 0x33);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_BFET_PWR_HWSAFE_MAX_TH,
		     0xcd);
	LOG_ASSERT_RW(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_INT3_EN, 0x06);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW3, 0);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_SWITCHER_CONF,
		      0x01);
}

/**
 * Driver internal "init completed" flag needs to be cleared to actually run
 * init.
 */
extern bool chip_inited[2];
/** Driver internal cached value of chip device ID. */
extern int dev_id;

ZTEST(sm5803, test_init_2s)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	/* Hook up logging functions for each I2C address. */
	configure_i2c_log(SM5803_EMUL, &log);

	/* Emulator defaults to 2S PMODE so we don't need to set it. */
	chip_inited[0] = false;
	sm5803_drv.init(CHARGER_NUM);
	sm5803_drv.post_init(CHARGER_NUM);

	/* Ensures we're in a safe state for operation. */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_CLOCK_SEL);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1, 0xf7);
	/* Checks VBUS presence and disables charger. */
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_MSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_LSB);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW1, 0);
	/* Gets chip ID (already cached) and PMODE. */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_PLATFORM);
	/* Writes a lot of registers for presumably important reasons. */
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, 0x26, 0xdc);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x21, 0x9b);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x30, 0xc0);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x80, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1a, 0x08);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, 0x08, 0xc2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x1d, 0x40);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x22, 0xb3);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x3e, 0x3c);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4f, 0xbf);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x52, 0x77);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x53, 0xD2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x54, 0x02);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x55, 0xD1);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x56, 0x7F);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x57, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x58, 0x50);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x59, 0x7F);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5A, 0x13);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5B, 0x52);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5D, 0xD0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x60, 0x44);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x65, 0x35);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x66, 0x29);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x7D, 0x97);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x7E, 0x07);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x33, 0x3C);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5C, 0x7A);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT2, 0x24);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x50, 0x88);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, 0x34, 0x80);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x43, 0x10);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x47, 0x10);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x48, 0x04);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 0);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT1, 0x70);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT3, 0x08);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT4, 0x08);
	verify_init_common(log_ptr);

	zassert_equal(log.entries_asserted, log.entries_used,
		      "recorded %d transactions but only verified %d",
		      log.entries_used, log.entries_asserted);

	/*
	 * Running init again should check and update VBUS presence but not
	 * re-run complete initialization. Doing more than that probably means
	 * the first init failed.
	 */
	log.entries_used = 0;
	sm5803_drv.init(CHARGER_NUM);
	zassert_equal(log.entries_used, 6);
}

ZTEST(sm5803, test_init_3s)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	/* Hook up logging functions for each I2C address. */
	configure_i2c_log(SM5803_EMUL, &log);

	/* Set 3S PMODE and run init */
	chip_inited[0] = false;
	sm5803_emul_set_pmode(SM5803_EMUL, 0x14);
	sm5803_drv.init(CHARGER_NUM);
	sm5803_drv.post_init(CHARGER_NUM);

	/* Ensures we're in a safe state for operation. */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_CLOCK_SEL);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1, 0xf7);
	/* Checks VBUS presence and disables charger. */
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_MSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_LSB);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW1, 0);
	/* Gets chip ID (already cached) and PMODE. */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_PLATFORM);
	/* Writes a lot of registers for presumably important reasons. */
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, 0x26, 0xd8);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x21, 0x9b);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x30, 0xc0);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x80, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1a, 0x08);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, 0x08, 0xc2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x1d, 0x40);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x22, 0xb3);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x3e, 0x3c);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4b, 0xa6);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4f, 0xbf);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x52, 0x77);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x53, 0xD2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x54, 0x02);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x55, 0xD1);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x56, 0x7F);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x57, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x58, 0x50);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x59, 0x7F);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5A, 0x13);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5B, 0x50);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5D, 0xB0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x60, 0x44);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x65, 0x35);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x66, 0x29);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x7D, 0x67);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x7E, 0x04);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x33, 0x3C);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5C, 0x7A);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT2, 0x24);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x50, 0x88);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, 0x34, 0x80);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x43, 0x10);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x47, 0x10);
	LOG_ASSERT_W(SM5803_ADDR_TEST_FLAGS, 0x48, 0x04);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 0);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT1, 0x70);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT3, 0x08);
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT4, 0x08);
	verify_init_common(log_ptr);

	zassert_equal(log.entries_asserted, log.entries_used,
		      "recorded %d transactions but only verified %d",
		      log.entries_used, log.entries_asserted);
}

ZTEST(sm5803, test_init_rev2)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	/* Hook up logging functions for each I2C address. */
	configure_i2c_log(SM5803_EMUL, &log);

	chip_inited[0] = false;
	dev_id = -1;
	sm5803_emul_set_device_id(SM5803_EMUL, 2);
	sm5803_drv.init(CHARGER_NUM);
	sm5803_drv.post_init(CHARGER_NUM);

	/* Ensures we're in a safe state for operation. */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_CLOCK_SEL);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1, 0xf7);
	/* Checks VBUS presence and disables charger. */
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_GPADC_CONFIG1);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_MSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBUS_MEAS_LSB);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW1, 0);
	/* Gets chip ID */
	LOG_ASSERT_R(SM5803_ADDR_MAIN_FLAGS, SM5803_REG_CHIP_ID);
	/* Writes a lot of registers for presumably important reasons. */
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x20, 0x08);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x30, 0xc0);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x80, 0x01);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, 0x08, 0xc2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x1d, 0x40);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x1f, 0x09);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x22, 0xb3);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x23, 0x81);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x28, 0xb7);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4a, 0x82);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4b, 0xa3);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4c, 0xa8);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4d, 0xca);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4e, 0x07);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x4f, 0xff);

	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x50, 0x98);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x51, 0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x52, 0x77);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x53, 0xd2);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x54, 0x02);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x55, 0xd1);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x56, 0x7f);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x57, 0x02);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x58, 0xd1);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x59, 0x7f);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5a, 0x13);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5b, 0x50);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5c, 0x5b);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5d, 0xb0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5e, 0x3c);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x5f, 0x3c);

	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x60, 0x44);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x61, 0x20);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x65, 0x35);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x66, 0x29);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x67, 0x64);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x68, 0x88);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, 0x69, 0xc7);

	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 1);
	LOG_ASSERT_RW(SM5803_ADDR_TEST_FLAGS, 0x8e, 0x20);
	LOG_ASSERT_W(SM5803_ADDR_MAIN_FLAGS, 0x1f, 0);

	verify_init_common(log_ptr);

	zassert_equal(log.entries_asserted, log.entries_used,
		      "recorded %d transactions but only verified %d",
		      log.entries_used, log.entries_asserted);
}

FAKE_VALUE_FUNC(const uint8_t *, system_get_jump_tag, uint16_t, int *, int *);

static const uint8_t *get_fake_inited_jump_tag(uint16_t tag, int *version,
					       int *size)
{
	static const bool bbram_inited[] = { true, true };

	BUILD_ASSERT(sizeof(bbram_inited) == sizeof(chip_inited));
	*version = 1;
	*size = sizeof(bbram_inited);
	return (uint8_t *)bbram_inited;
}

ZTEST(sm5803, test_init_status_preserve)
{
	/* run init_status_preserve() */
	chip_inited[0] = true;
	chip_inited[1] = true;
	hook_notify(HOOK_SYSJUMP);

	/* pretend we rebooted and reset init state */
	chip_inited[0] = false;
	chip_inited[1] = false;
	system_get_jump_tag_fake.custom_fake = get_fake_inited_jump_tag;
	/*
	 * init_status_retrieve() should restore chip_inited before charger
	 * init runs, which would fail since we haven't mocked a number of the
	 * registers it writes.
	 */
	hook_notify(HOOK_INIT);
	zassert_true(chip_inited[0]);
	zassert_true(chip_inited[1]);
}

ZTEST(sm5803, test_fast_charge_current)
{
	int ma;

	/*
	 * Can set and read back charge current limit,
	 * which is adjusted when 0.
	 */
	zassert_ok(charger_set_current(CHARGER_NUM, 0));
	zassert_equal(1, sm5803_emul_get_fast_charge_current_limit(SM5803_EMUL),
		      "Zero current limit should be converted to nonzero");
	zassert_ok(charger_get_current(CHARGER_NUM, &ma));
	zassert_equal(ma, 100,
		      "Actual current should be 100 mA times register value");

	/* Errors are propagated. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_FAST_CONF4);
	zassert_not_equal(
		0, charger_set_current(CHARGER_NUM, 1000),
		"set_current should fail if FAST_CONF4 is unreadable");
	zassert_not_equal(
		0, charger_get_current(CHARGER_NUM, &ma),
		"get_current should fail if FAST_CONF4 is unreadable");
}

ZTEST(sm5803, test_measure_input_current)
{
	int ma;

	sm5803_emul_set_input_current(SM5803_EMUL, 852);
	zassert_ok(charger_get_input_current(CHARGER_NUM, &ma));
	zassert_equal(ma, 849, "actual returned input current was %d", ma);

	/* Communication errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_IBUS_CHG_MEAS_LSB);
	zassert_not_equal(0, charger_get_input_current(CHARGER_NUM, &ma));
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_IBUS_CHG_MEAS_MSB);
	zassert_not_equal(0, charger_get_input_current(CHARGER_NUM, &ma));
}

ZTEST(sm5803, test_input_current_limit)
{
	int icl;
	bool reached;

	/* Can set and read back the input current limit. */
	zassert_ok(charger_set_input_current_limit(CHARGER_NUM, 2150));
	zassert_equal(21, sm5803_emul_read_chg_reg(SM5803_EMUL,
						   SM5803_REG_CHG_ILIM));
	zassert_ok(charger_get_input_current_limit(CHARGER_NUM, &icl));
	zassert_equal(2100, icl,
		      "expected 2100 mA input current limit, but was %d", icl);

	/* Can also check whether input current is limited. */
	zassert_ok(charger_is_icl_reached(CHARGER_NUM, &reached));
	zassert_false(reached);
	sm5803_emul_set_input_current(SM5803_EMUL, 2400);
	zassert_ok(charger_is_icl_reached(CHARGER_NUM, &reached));
	zassert_true(reached);

	/* Communication errors bubble up. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_CHG_ILIM);
	zassert_not_equal(0,
			  charger_get_input_current_limit(CHARGER_NUM, &icl));
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_CHG_ILIM);
	zassert_not_equal(0,
			  charger_set_input_current_limit(CHARGER_NUM, 1400));
}

/* Analog measurement of VBUS. */
ZTEST(sm5803, test_get_vbus_voltage)
{
	int mv;

	/* Regular measurement with VBUS ADC enabled works. */
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 5032);
	zassert_ok(charger_get_vbus_voltage(CHARGER_NUM, &mv));
	/* 5.031 is the nearest value representable by the VBUS ADC. */
	zassert_equal(mv, 5031, "driver reported %d mV VBUS", mv);

	/* Communication errors for ADC value bubble up. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBUS_MEAS_LSB);
	zassert_not_equal(0, charger_get_vbus_voltage(CHARGER_NUM, &mv));
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBUS_MEAS_MSB);
	zassert_not_equal(0, charger_get_vbus_voltage(CHARGER_NUM, &mv));

	/* Returns a NOT_POWERED error if the VBUS ADC is disabled. */
	sm5803_emul_set_gpadc_conf(SM5803_EMUL,
				   (uint8_t)~SM5803_GPADCC1_VBUS_EN, 0);
	zassert_equal(EC_ERROR_NOT_POWERED,
		      charger_get_vbus_voltage(CHARGER_NUM, &mv));
}

ZTEST(sm5803, test_get_battery_current)
{
	int ma;

	sm5803_emul_set_battery_current(SM5803_EMUL, 1234);
	zassert_ok(charger_get_actual_current(CHARGER_NUM, &ma));
	/* 1229 mA is nearest value representable at ADC resolution */
	zassert_equal(ma, 1229, "read value was %d", ma);

	/* Communication errors bubble up. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_IBAT_CHG_AVG_MEAS_LSB);
	zassert_not_equal(0, charger_get_actual_current(CHARGER_NUM, &ma));
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_IBAT_CHG_AVG_MEAS_MSB);
	zassert_not_equal(0, charger_get_actual_current(CHARGER_NUM, &ma));
}

/* Digital VBUS presence detection derived from DHG_DET. */
ZTEST(sm5803, test_digital_vbus_presence_detect)
{
	/*
	 * CHG_DET going high (from VBUS presence) triggers an interrupt and
	 * presence update.
	 */
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 5000);
	k_sleep(K_SECONDS(1)); /* Allow interrupt to be serviced. */
	zassert_true(sm5803_is_vbus_present(CHARGER_NUM));

	/* VBUS going away triggers another interrupt and update. */
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 0);
	k_sleep(K_SECONDS(1)); /* Allow interrupt to be serviced. */
	zassert_false(sm5803_is_vbus_present(CHARGER_NUM));
}

/* VBUS detection for PD, analog or digital depending on chip state. */
ZTEST(sm5803, test_check_vbus_level)
{
	/* Default state with VBUS ADC enabled: uses analog value */
	zassert_true(sm5803_check_vbus_level(CHARGER_NUM, VBUS_REMOVED));
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 5000);
	zassert_true(sm5803_check_vbus_level(CHARGER_NUM, VBUS_PRESENT));

	/* 4.6V is less than vSafe5V */
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 4600);
	k_sleep(K_SECONDS(1));
	zassert_false(sm5803_check_vbus_level(CHARGER_NUM, VBUS_PRESENT));

	/* Unrecognized levels are never matched */
	zassert_false(sm5803_check_vbus_level(CHARGER_NUM, -1));

	/*
	 * With ADC disabled, uses digital presence only. 4.6V is high enough
	 * to trip CHG_DET but wasn't enough to count as present with the analog
	 * reading.
	 */
	sm5803_emul_set_gpadc_conf(SM5803_EMUL, 0, 0);
	zassert_true(sm5803_check_vbus_level(CHARGER_NUM, VBUS_PRESENT));

	/* 0.4V is !CHG_DET */
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 400);
	k_sleep(K_SECONDS(1));
	zassert_true(sm5803_check_vbus_level(CHARGER_NUM, VBUS_REMOVED));

	/* Unrecognized levels are never matched */
	zassert_false(sm5803_check_vbus_level(CHARGER_NUM, -1));
}

ZTEST_F(sm5803, test_lpm)
{
	uint8_t gpadc1, gpadc2;
	uint8_t cc_conf1;
	uint8_t flow1, flow2, flow3;

	pd_connect_source(fixture);

	/* Charger should now have exited runtime LPM. */
	zassert_false(sm5803_emul_is_clock_slowed(SM5803_EMUL));
	sm5803_emul_get_gpadc_conf(SM5803_EMUL, &gpadc1, &gpadc2);
	/* All except IBAT_DISCHG enabled. */
	zassert_equal(gpadc1, 0xf7, "actual value was %#x", gpadc1);
	/* Default value. */
	zassert_equal(gpadc2, 1, "actual value was %#x", gpadc2);
	/* Sigma-delta for Coulomb Counter is enabled. */
	cc_conf1 = sm5803_emul_get_cc_config(SM5803_EMUL);
	zassert_equal(cc_conf1, 0x09, "actual value was %#x", cc_conf1);
	/* Charger is sinking. */
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	zassert_equal(flow1, 0x01, "FLOW1 should be set for sinking, was %#x",
		      flow1);

	/* Disconnect source, causing charger to go to runtime LPM. */
	pd_disconnect(fixture);

	zassert_true(sm5803_emul_is_clock_slowed(SM5803_EMUL));
	/* Sigma delta was disabled. */
	cc_conf1 = sm5803_emul_get_cc_config(SM5803_EMUL);
	zassert_equal(sm5803_emul_get_cc_config(SM5803_EMUL), 0x01,
		      "actual value was %#x", cc_conf1);
	/*
	 * Runtime LPM hook runs before the charge manager updates, so we expect
	 * the GPADCs to be left on because the charger is still set for sinking
	 * when it goes to runtime LPM.
	 */
	sm5803_emul_get_gpadc_conf(SM5803_EMUL, &gpadc1, &gpadc2);
	zassert_equal(gpadc1, 0xf7, "actual value was %#x", gpadc1);
	zassert_equal(gpadc2, 1, "actual value was %#x", gpadc2);

	/*
	 * Reconnect the source and inhibit charging, so GPADCs can be disabled
	 * when we disconnect it.
	 */
	pd_connect_source(fixture);
	zassert_ok(charger_set_mode(CHARGE_FLAG_INHIBIT_CHARGE));
	pd_disconnect(fixture);

	/* This time LPM actually did disable the GPADCs. */
	sm5803_emul_get_gpadc_conf(SM5803_EMUL, &gpadc1, &gpadc2);
	zassert_equal(gpadc1, 0, "actual value was %#x", gpadc1);
	zassert_equal(gpadc2, 0, "actual value was %#x", gpadc2);
}

ZTEST(sm5803, test_get_battery_cells)
{
	int cells;

	/* Default PMODE reports 2s */
	zassert_ok(sm5803_drv.get_battery_cells(CHARGER_NUM, &cells));
	zassert_equal(cells, 2);

	/* 3s PMODE is 3s */
	sm5803_emul_set_pmode(SM5803_EMUL, 0x14);
	zassert_ok(sm5803_drv.get_battery_cells(CHARGER_NUM, &cells));
	zassert_equal(cells, 3);

	/* Unrecognized PMODE is an error */
	sm5803_emul_set_pmode(SM5803_EMUL, 0x1f);
	zassert_not_equal(sm5803_drv.get_battery_cells(CHARGER_NUM, &cells), 0);
	zassert_equal(cells, -1);

	/* Communication error bubbles up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_PLATFORM);
	zassert_not_equal(sm5803_drv.get_battery_cells(CHARGER_NUM, &cells), 0);
}

ZTEST(sm5803, test_gpio)
{
	/* Open drain output */
	zassert_ok(sm5803_configure_gpio0(CHARGER_NUM, GPIO0_MODE_OUTPUT, 1));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x42);
	/* Set output high, from default of low. */
	zassert_ok(sm5803_set_gpio0_level(CHARGER_NUM, 1));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x43);
	/* Set it low again. */
	zassert_ok(sm5803_set_gpio0_level(CHARGER_NUM, 0));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x42);

	/* Push-pull prochot. */
	zassert_ok(sm5803_configure_gpio0(CHARGER_NUM, GPIO0_MODE_PROCHOT, 0));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x00);

	/* CHG_DET output enable lives in this register too */
	zassert_ok(sm5803_configure_chg_det_od(CHARGER_NUM, 1));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x80);
	zassert_ok(sm5803_configure_chg_det_od(CHARGER_NUM, 0));
	zassert_equal(sm5803_emul_get_gpio_ctrl(SM5803_EMUL), 0x00);

	/* Communication errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_GPIO0_CTRL);
	zassert_not_equal(
		sm5803_configure_gpio0(CHARGER_NUM, GPIO0_MODE_INPUT, 0), 0);
	zassert_not_equal(sm5803_set_gpio0_level(CHARGER_NUM, 0), 0);
	zassert_not_equal(sm5803_configure_chg_det_od(CHARGER_NUM, 1), 0);
}

ZTEST(sm5803, test_vsys_compensation)
{
	struct ocpc_data ocpc;

	/* Minimum possible resistance */
	ocpc.combined_rsys_rbatt_mo = 0;
	zassert_equal(sm5803_drv.set_vsys_compensation(CHARGER_NUM, &ocpc, 0,
						       0),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(sm5803_emul_get_ir_comp(SM5803_EMUL), 0x2100,
		      "actual IR_COMP value was %#x",
		      sm5803_emul_get_ir_comp(SM5803_EMUL));

	/* Maximum resistance supported by SM5803 (1.67 mOhm * 0x3FF) */
	ocpc.combined_rsys_rbatt_mo = 1709;
	zassert_equal(sm5803_drv.set_vsys_compensation(CHARGER_NUM, &ocpc, 0,
						       0),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(sm5803_emul_get_ir_comp(SM5803_EMUL), 0xE1FF,
		      "actual IR_COMP value was %#x",
		      sm5803_emul_get_ir_comp(SM5803_EMUL));

	/* Typical actual resistance */
	ocpc.combined_rsys_rbatt_mo = 42;
	zassert_equal(sm5803_drv.set_vsys_compensation(CHARGER_NUM, &ocpc, 0,
						       0),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(sm5803_emul_get_ir_comp(SM5803_EMUL), 0x2119,
		      "actual IR_COMP value was %#x",
		      sm5803_emul_get_ir_comp(SM5803_EMUL));

	/* Communication errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_IR_COMP1);
	zassert_not_equal(sm5803_drv.set_vsys_compensation(CHARGER_NUM, &ocpc,
							   0, 0),
			  EC_ERROR_UNIMPLEMENTED);
}

ZTEST(sm5803, test_vbus_sink_enable)
{
	uint8_t flow1, flow2, flow3;

	zassert_ok(sm5803_vbus_sink_enable(CHARGER_NUM, 1));
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	zassert_equal(flow1, 0x01, "FLOW1 should be set to sink mode; was %#x",
		      flow1);
	zassert_equal(
		flow2, 0x07,
		"FLOW2 should enable automatic charge management; was %#x",
		flow2);

	/*
	 * TODO(b:283026626): exercise the branch for disconnected battery
	 * in sm5803_vbus_sink_enable.
	 */

	zassert_ok(sm5803_vbus_sink_enable(CHARGER_NUM, 0));
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	zassert_equal(flow1, 0, "FLOW1 should disable sinking; was %#x", flow1);
	zassert_equal(flow2, 0, "FLOW2 should disable auto charge; was %#x",
		      flow2);

	/* Secondary charger has slightly different operation. */
	zassert_ok(sm5803_vbus_sink_enable(CHARGER_SECONDARY, 1));
	sm5803_emul_get_flow_regs(SM5803_EMUL_SECONDARY, &flow1, &flow2,
				  &flow3);
	zassert_equal(flow1, 0x01, "FLOW1 should be set to sink mode; was %#x",
		      flow1);

	zassert_ok(sm5803_vbus_sink_enable(CHARGER_SECONDARY, 0));
	sm5803_emul_get_flow_regs(SM5803_EMUL_SECONDARY, &flow1, &flow2,
				  &flow3);
	zassert_equal(flow1, 0, "FLOW1 should disable sinking; was %#x", flow1);
}

ZTEST(sm5803, test_charge_ramp)
{
	int icl;

	/* Enables DPM loop when ramp is requested */
	zassert_ok(sm5803_drv.set_hw_ramp(CHARGER_NUM, 1));
	zassert_equal(sm5803_emul_get_chg_mon(SM5803_EMUL), 1);
	/* These functions always report these values */
	zassert_equal(sm5803_drv.ramp_is_stable(CHARGER_NUM), 0);
	zassert_equal(sm5803_drv.ramp_is_detected(CHARGER_NUM), 1);
	/* Ramp limit is always the same as regular ICL */
	zassert_ok(charger_get_input_current_limit(CHARGER_NUM, &icl));
	zassert_equal(sm5803_drv.ramp_get_current_limit(CHARGER_NUM), icl);

	/* Requesting disable turns off the DPM loop */
	zassert_ok(sm5803_drv.set_hw_ramp(CHARGER_NUM, 0));
	zassert_equal(sm5803_emul_get_chg_mon(SM5803_EMUL), 0);
}

/** Get the source mode voltage target, in mV. */
static uint32_t get_source_voltage(void)
{
	return 2720 + 10 * ((sm5803_emul_get_disch_conf1(SM5803_EMUL) << 3) |
			    (sm5803_emul_get_disch_conf2(SM5803_EMUL) & 7));
}

ZTEST(sm5803, test_configure_sourcing)
{
	/* Typical configuration, 5V @ 1.5A */
	zassert_ok(sm5803_drv.set_otg_current_voltage(CHARGER_NUM, 1500, 5000));
	zassert_equal(sm5803_emul_get_disch_conf5(SM5803_EMUL), 30,
		      "Current limit should be 30 * 50 mA, but"
		      " register value is %#x",
		      sm5803_emul_get_disch_conf5(SM5803_EMUL));
	zassert_equal(get_source_voltage(), 5000,
		      "actual source voltage was set to %d mV",
		      get_source_voltage());

	/* Very large currents don't write bits they shouldn't */
	zassert_ok(sm5803_drv.set_otg_current_voltage(CHARGER_NUM, 6500, 5000));
	zassert_equal(
		sm5803_emul_get_disch_conf5(SM5803_EMUL), 0x7F,
		"current limit should be maximum, but register value is %#x",
		sm5803_emul_get_disch_conf5(SM5803_EMUL));

	/* Small voltages don't underflow */
	zassert_ok(sm5803_drv.set_otg_current_voltage(CHARGER_NUM, 1000, 2000));
	zassert_equal(
		get_source_voltage(), 2720,
		"small voltages should clamp to minimum, but is set to %d mV",
		get_source_voltage());

	/* Errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_DISCH_CONF5);
	zassert_not_equal(
		sm5803_drv.set_otg_current_voltage(CHARGER_NUM, 1500, 5000), 0);
}

ZTEST(sm5803, test_sourcing)
{
	uint8_t gpadc1, gpadc2;
	uint8_t flow1, flow2, flow3;

	/* Preset source current limit */
	sm5803_drv.set_otg_current_voltage(CHARGER_NUM, 1000, 0);
	/* Turn off GPADCs so we can check they get turned on */
	sm5803_emul_set_gpadc_conf(SM5803_EMUL, 0, 0);

	zassert_ok(sm5803_drv.enable_otg_power(CHARGER_NUM, 1));
	zassert_true(sm5803_drv.is_sourcing_otg_power(CHARGER_NUM, 0));
	/* GPADCs got turned on as needed. */
	sm5803_emul_get_gpadc_conf(SM5803_EMUL, &gpadc1, &gpadc2);
	zassert_equal(gpadc1, 0xf7,
		      "GPADCs should be set to active state, but CONF1 = %#x",
		      gpadc1);
	zassert_equal(gpadc2, 0,
		      "GPADCs should be set to active state, but CONF2 = %#x",
		      gpadc2);
	/* Source current limit is turned on */
	zassert_equal(sm5803_emul_get_ana_en1(SM5803_EMUL), 0x19,
		      "CLS_DISABLE should be cleared, but ANA_EN1 = %#x",
		      sm5803_emul_get_ana_en1(SM5803_EMUL));
	/* Source current ramp was disabled */
	zassert_equal(sm5803_emul_get_disch_conf6(SM5803_EMUL), 0x01);
	/* FLOW1 was set to sourcing with current regulation */
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	zassert_equal(flow1, 0x07);
	/* Source voltage is 5V and current limit is retained from previous */
	zassert_equal(get_source_voltage(), 5000);

	/* Status polling function works */
	zassert_true(sm5803_drv.is_sourcing_otg_power(CHARGER_NUM, 0));

	/* Now turn off source mode */
	sm5803_emul_set_disch_status(SM5803_EMUL, 0x18);
	zassert_ok(sm5803_drv.enable_otg_power(CHARGER_NUM, 0));
	zassert_equal(sm5803_emul_get_disch_status(SM5803_EMUL), 0,
		      "discharge status flags should be cleared but were not");
	zassert_equal(sm5803_emul_get_disch_conf6(SM5803_EMUL), 0,
		      "DISCH_CONF6 should be cleared, but is now %#x",
		      sm5803_emul_get_disch_conf6(SM5803_EMUL));
	/* Source mode is disabled */
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	zassert_equal(flow1, 0);

	/* Assorted errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_DISCH_CONF5);
	zassert_not_equal(sm5803_drv.enable_otg_power(CHARGER_NUM, 1), 0);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_ANA_EN1);
	zassert_not_equal(sm5803_drv.enable_otg_power(CHARGER_NUM, 1), 0);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_FLOW1);
	zassert_not_equal(sm5803_drv.enable_otg_power(CHARGER_NUM, 0), 0);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_STATUS_DISCHG);
	zassert_not_equal(sm5803_drv.enable_otg_power(CHARGER_NUM, 0), 0);
}

ZTEST(sm5803, test_set_option)
{
	uint8_t flow1, flow2, flow3;

	/* set_option() writes all three flow registers */
	zassert_ok(sm5803_drv.set_option(CHARGER_NUM, 0xE54321));
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, &flow2, &flow3);
	/* FLOW1 bits 4-6 always read 0 */
	zassert_equal(flow1, 0x01, "actual value was %#x", flow1);
	zassert_equal(flow2, 0x43, "actual value was %#x", flow2);
	/* FLOW3 bit 7 is unimplemented */
	zassert_equal(flow3, 0x65, "actual value was %#x", flow3);

	/* and I2C errors are returned */
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_FLOW3);
	zassert_not_equal(sm5803_drv.set_option(CHARGER_NUM, 0), 0);
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_FLOW2);
	zassert_not_equal(sm5803_drv.set_option(CHARGER_NUM, 0), 0);
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_FLOW1);
	zassert_not_equal(sm5803_drv.set_option(CHARGER_NUM, 0), 0);
}

FAKE_VALUE_FUNC(int, extpower_is_present);

ZTEST_F(sm5803, test_set_mode)
{
	int temp;

	/* Make cur.ac = 1 */
	pd_connect_source(fixture);
	extpower_is_present_fake.return_val = 1;
	zassert_true(extpower_is_present());
	/* add delay to let curr.ac to be 1 */
	k_sleep(K_SECONDS(1));

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle off"));
	charge_idle_enabled = 1;
	zassert_ok(charger_set_mode(0));
	zassert_ok(charger_get_option(&temp));
	/* Flow1(0x1C) Bit[1:0]=01, Flow2(0x1D) Bit[2:0]=111 */
	zassert_equal(temp, 0x701);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle on"));
	zassert_ok(charger_set_mode(0));
	zassert_ok(charger_get_option(&temp));
	/* Flow1(0x1C) Bit[1:0]=01, Flow2(0x1D) Bit[2:0]=000 */
	zassert_equal(temp, 0x1);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate discharge on"));
	zassert_ok(charger_set_mode(0));
	zassert_equal(charge_idle_enabled, 0);

	/* Reset AC status */
	extpower_is_present_fake.return_val = 0;
	zassert_false(extpower_is_present());
}

ZTEST(sm5803, test_acok)
{
	bool acok;

	zassert_ok(sm5803_is_acok(CHARGER_NUM, &acok));
	zassert_false(acok);

	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 4986);
	zassert_ok(sm5803_is_acok(CHARGER_NUM, &acok));
	zassert_true(acok);

	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBUS_MEAS_MSB);
	zassert_not_equal(sm5803_is_acok(CHARGER_NUM, &acok), 0);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_STATUS1);
	zassert_not_equal(sm5803_is_acok(CHARGER_NUM, &acok), 0);
}

ZTEST(sm5803, test_chg_det)
{
	int chg_det;

	zassert_ok(sm5803_get_chg_det(CHARGER_NUM, &chg_det));
	zassert_false(chg_det);

	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 9001);
	zassert_ok(sm5803_get_chg_det(CHARGER_NUM, &chg_det));
	zassert_true(chg_det);

	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_STATUS1);
	zassert_not_equal(sm5803_get_chg_det(CHARGER_NUM, &chg_det), 0);
}

ZTEST(sm5803, test_vbus_discharge)
{
	/* Enabling VBUS discharge sets the discharge enable bit */
	zassert_ok(sm5803_set_vbus_disch(CHARGER_NUM, 1));
	zassert_equal(sm5803_emul_get_ports_ctrl(SM5803_EMUL), 1,
		      "actual value was %#x",
		      sm5803_emul_get_ports_ctrl(SM5803_EMUL));

	/* And we can clear it */
	zassert_ok(sm5803_set_vbus_disch(CHARGER_NUM, 0));
	zassert_equal(sm5803_emul_get_ports_ctrl(SM5803_EMUL), 0,
		      "actual value was %#x",
		      sm5803_emul_get_ports_ctrl(SM5803_EMUL));

	/* Errors are returned */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_PORTS_CTRL);
	zassert_not_equal(sm5803_set_vbus_disch(CHARGER_NUM, 0), 0);
}

ZTEST(sm5803, test_hibernate)
{
	uint8_t gpadc1, gpadc2;

	/*
	 * Assorted registers get programmed on hibernate; use the secondary
	 * charger because it has some extra handling.
	 */
	sm5803_hibernate(CHARGER_SECONDARY);
	zassert_equal(
		sm5803_emul_get_reference_reg(SM5803_EMUL_SECONDARY), 0x03,
		"REFERENCE1 should disable LDOs, but actual value was %#x",
		sm5803_emul_get_reference_reg(SM5803_EMUL_SECONDARY));
	zassert_true(sm5803_emul_is_clock_slowed(SM5803_EMUL_SECONDARY));

	sm5803_emul_get_gpadc_conf(SM5803_EMUL_SECONDARY, &gpadc1, &gpadc2);
	zassert_equal(gpadc1, 0,
		      "GPADCs should be disabled, but GPADC1 was %#x", gpadc1);
	zassert_equal(gpadc2, 0,
		      "GPADCs should be disabled, but GPADC2 was %#x", gpadc2);

	zassert_false(sm5803_emul_is_psys_dac_enabled(SM5803_EMUL_SECONDARY));

	zassert_equal(sm5803_emul_get_cc_config(SM5803_EMUL_SECONDARY), 0x01,
		      "Sigma-delta should be disabled, but CC_CONFIG1 was %#x",
		      sm5803_emul_get_cc_config(SM5803_EMUL_SECONDARY));

	zassert_equal(sm5803_emul_get_phot1(SM5803_EMUL_SECONDARY), 0x20,
		      "PHOT1 should disable comparators, but value was %#x",
		      sm5803_emul_get_phot1(SM5803_EMUL_SECONDARY));

	/* Primary charger doesn't disable LDOs. */
	sm5803_hibernate(CHARGER_PRIMARY);
	zassert_equal(
		sm5803_emul_get_reference_reg(SM5803_EMUL), 0,
		"REFERENCE1 should not disable LDOs, but actual value was %#x",
		sm5803_emul_get_reference_reg(SM5803_EMUL));

	/* Error paths don't do anything catastrophically bad */
	i2c_common_emul_set_write_fail_reg(
		sm5803_emul_get_i2c_main(SM5803_EMUL_SECONDARY),
		SM5803_REG_REFERENCE);
	sm5803_hibernate(CHARGER_SECONDARY);

	i2c_common_emul_set_read_fail_reg(
		sm5803_emul_get_i2c_main(SM5803_EMUL_SECONDARY),
		SM5803_REG_REFERENCE);
	sm5803_hibernate(CHARGER_SECONDARY);
}

ZTEST(sm5803, test_set_voltage)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				      i2c_log_read_chg, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				       i2c_log_write_chg, log_ptr);

	test_sm5803_set_fast_charge_disabled(true);
	zassert_ok(sm5803_drv.set_voltage(CHARGER_NUM, 12000));
	/* VSYS and VBAT were programmed to the requested voltage */
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_VSYS_PREREG_MSB,
		     0x74);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_VSYS_PREREG_LSB, 0);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_VBAT_FAST_MSB, 0x74);
	LOG_ASSERT_W(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_VBAT_FAST_LSB, 0);
	/* Fast charge was enabled */
	zassert_false(test_sm5803_get_fast_charge_disabled());
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_FLOW2, 0x07);
}

ZTEST(sm5803, test_get_actual_voltage)
{
	int mv;

	sm5803_emul_set_vsys_measured_mv(SM5803_EMUL, 7450);
	zassert_ok(sm5803_drv.get_actual_voltage(CHARGER_NUM, &mv));
	/* Nearest value representable by the measurement registers. */
	zassert_equal(mv, 7441, "actual reported value was %d mV", mv);

	/* Communication errors bubble up */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VSYS_AVG_MEAS_LSB);
	zassert_not_equal(sm5803_drv.get_actual_voltage(CHARGER_NUM, &mv), 0);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VSYS_AVG_MEAS_MSB);
	zassert_not_equal(sm5803_drv.get_actual_voltage(CHARGER_NUM, &mv), 0);
}

ZTEST(sm5803, test_linear_charge)
{
	uint8_t flow1, flow3;

	/* Initial attempt is rebuffed because BFET is disabled. */
	zassert_equal(sm5803_drv.enable_linear_charge(CHARGER_SECONDARY, 1),
		      EC_ERROR_TRY_AGAIN);
	/* Set target voltage, kicking the primary charger's BFET on. */
	zassert_ok(sm5803_drv.set_voltage(CHARGER_SECONDARY, 10600));
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 0x01,
		      "Primary charger FLOW1 should be sinking, but was %#x",
		      flow1);
	zassert_equal(sm5803_emul_get_log1(SM5803_EMUL), 0x04,
		      "Primary charger BFET should be on, but LOG1 was %#x",
		      sm5803_emul_get_log1(SM5803_EMUL));

	/* Now linear charge enable is permitted. */
	zassert_ok(sm5803_drv.enable_linear_charge(CHARGER_SECONDARY, 1));
	sm5803_emul_get_flow_regs(SM5803_EMUL_SECONDARY, &flow1, NULL, &flow3);
	zassert_equal(flow1, 0x09,
		      "secondary charger should be sinking in linear mode,"
		      " but FLOW1 was %#x",
		      flow1);
	zassert_equal(flow3, 0x70,
		      "mystery bits 4-6 of FLOW3 should have been set, but"
		      " value was %#x",
		      flow3);

	/* Disables when requested. */
	zassert_ok(sm5803_drv.enable_linear_charge(CHARGER_SECONDARY, 0));
	sm5803_emul_get_flow_regs(SM5803_EMUL_SECONDARY, &flow1, NULL, &flow3);
	zassert_equal(flow1, 0x01,
		      "secondary charger linear mode should be disabled, but"
		      " FLOW1 was %#x",
		      flow1);
	zassert_equal(flow3, 0,
		      "FLOW3 mystery bits should be cleared, but value was %#x",
		      flow3);
}

ZTEST(sm5803, test_explicit_lpm)
{
	/* Enter LPM */
	sm5803_enable_low_power_mode(CHARGER_NUM);
	zassert_false(sm5803_emul_is_psys_dac_enabled(SM5803_EMUL));
	zassert_equal(
		sm5803_emul_get_phot1(SM5803_EMUL), 0x20,
		"PROCHOT comparators should be disabled, but PHOT1 was %#x",
		sm5803_emul_get_phot1(SM5803_EMUL));

	/* Exit LPM */
	sm5803_disable_low_power_mode(CHARGER_NUM);
	zassert_true(sm5803_emul_is_psys_dac_enabled(SM5803_EMUL));
	zassert_equal(
		sm5803_emul_get_phot1(SM5803_EMUL), 0x2d,
		"PROCHOT comparators should be enabled, but PHOT1 was %#x",
		sm5803_emul_get_phot1(SM5803_EMUL));

	/* Errors are handled */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					  SM5803_REG_PHOT1);
	sm5803_enable_low_power_mode(CHARGER_NUM);
	sm5803_disable_low_power_mode(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_PSYS1);
	sm5803_enable_low_power_mode(CHARGER_NUM);
	sm5803_disable_low_power_mode(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_REFERENCE);
	sm5803_enable_low_power_mode(CHARGER_NUM);
	sm5803_disable_low_power_mode(CHARGER_NUM);
}

ZTEST_F(sm5803, test_explicit_lpm_connected)
{
	pd_connect_source(fixture);

	/* Going to explicit LPM leaves VBUS comparator enabled. */
	sm5803_enable_low_power_mode(CHARGER_NUM);
	zassert_equal(sm5803_emul_get_phot1(SM5803_EMUL), 0x28,
		      "Comparators other than VBUS should be disabled,"
		      " but PHOT1 was %#x",
		      sm5803_emul_get_phot1(SM5803_EMUL));
}

ZTEST_F(sm5803, test_vbat_overvoltage_2s)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;
	uint8_t flow1;

	pd_connect_source(fixture);
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 1, "charger should be sinking, but FLOW1 was %#x",
		      flow1);

	/* Log accesses to sense parameters to verify they're as expected. */
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				      i2c_log_read_meas, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				       i2c_log_write_meas, log_ptr);

	/*
	 * Trigger VBAT_SNS overvoltage interrupt. Default threshold for 2S is
	 * 9V.
	 */
	sm5803_emul_set_vbat_sns_mv(SM5803_EMUL, 9050);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	/* Allow interrupt to be serviced */
	k_sleep(K_SECONDS(0.1));

	/*
	 * Interrupt handler logged voltages, then reset the threshold to reset
	 * the interrupt, and programmed the expected threshold back.
	 */
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MEAS_MSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MEAS_LSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH, 0xff);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH, 0xdc);
	/* Interrupt handler also stopped sinking. */
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 0,
		      "FLOW1 should disable charger, but value was %#x", flow1);

	/*
	 * Charger will now attempt to re-enable sinking automatically. Stop
	 * logging because we don't care anymore and there will be a lot of
	 * accesses in the background while we wait.
	 */
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				      NULL, NULL);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				       NULL, NULL);
	k_sleep(K_SECONDS(2));
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 1,
		      "FLOW1 should resume sinking, but value was %#x", flow1);

	/* Assorted error paths in the interrupt. */
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBATSNSP_MAX_TH);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBATSNSP_MEAS_LSB);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_meas(SM5803_EMUL),
					  SM5803_REG_VBATSNSP_MEAS_MSB);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	i2c_common_emul_set_read_fail_reg(sm5803_emul_get_i2c_main(SM5803_EMUL),
					  SM5803_REG_PLATFORM);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
}

ZTEST_F(sm5803, test_vbat_overvoltage_3s)
{
	/*
	 * This test case is nearly identical to test_vbat_overvoltage_2s but
	 * expects different thresholds.
	 */
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;
	uint8_t flow1;

	/* Set 3S PMODE */
	sm5803_emul_set_pmode(SM5803_EMUL, 0x14);

	/* Connect PD source and begin charging. */
	pd_connect_source(fixture);
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 1, "charger should be sinking, but FLOW1 was %#x",
		      flow1);

	/* Log accesses to sense parameters to verify they're as expected. */
	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				      i2c_log_read_meas, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_meas(SM5803_EMUL),
				       i2c_log_write_meas, log_ptr);

	/*
	 * Trigger VBAT_SNS overvoltage interrupt. Default threshold for 3S is
	 * 13.3V.
	 */
	sm5803_emul_set_vbat_sns_mv(SM5803_EMUL, 13450);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_VBATSNSP, 0, 0);
	/* Allow interrupt to be serviced */
	k_sleep(K_SECONDS(0.1));
	zassert_false(
		gpio_pin_get_dt(sm5803_emul_get_interrupt_gpio(SM5803_EMUL)),
		"IRQ is still asserted");

	/*
	 * Interrupt handler logged voltages, then reset the threshold to reset
	 * the interrupt, and programmed the expected threshold back.
	 */
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MEAS_MSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MEAS_LSB);
	LOG_ASSERT_R(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH, 0xff);
	LOG_ASSERT_W(SM5803_ADDR_MEAS_FLAGS, SM5803_REG_VBATSNSP_MAX_TH, 0xd8);
	/* Interrupt handler also stopped sinking. */
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 0,
		      "FLOW1 should disable charger, but value was %#x", flow1);
}

FAKE_VOID_FUNC(chipset_throttle_cpu, int);

ZTEST(sm5803, test_thermal_throttle)
{
	/* Above 360K, charger throttles AP. */
	sm5803_emul_set_internal_temperature(SM5803_EMUL, 362);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_TINT, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	zassert_equal(chipset_throttle_cpu_fake.call_count, 1);
	zassert_equal(chipset_throttle_cpu_fake.arg0_val, 1);

	/* Next watching only for temperature to drop below 330K. */
	zassert_equal(sm5803_emul_get_tint_high_th(SM5803_EMUL), 0xff,
		      "TINT_HIGH_TH should be set to maximum, but was %#x",
		      sm5803_emul_get_tint_high_th(SM5803_EMUL));
	zassert_equal(sm5803_emul_get_tint_low_th(SM5803_EMUL), 0xbf,
		      "TINT_LOW_TH should be set to 330K, but was %#x",
		      sm5803_emul_get_tint_low_th(SM5803_EMUL));

	/* Falling below 330K, releases throttle. */
	sm5803_emul_set_internal_temperature(SM5803_EMUL, 328);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, SM5803_INT2_TINT, 0, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	zassert_equal(chipset_throttle_cpu_fake.call_count, 2);
	zassert_equal(chipset_throttle_cpu_fake.arg0_val, 0);

	/* Watches for temperatures rising above 360K again. */
	zassert_equal(sm5803_emul_get_tint_high_th(SM5803_EMUL), 0xd1,
		      "TINT_HIGH_TH should be set to 360K, but was %#x",
		      sm5803_emul_get_tint_high_th(SM5803_EMUL));
	zassert_equal(sm5803_emul_get_tint_low_th(SM5803_EMUL), 0,
		      "TINT_LOW_TH should be set to minimum, but was %#x",
		      sm5803_emul_get_tint_low_th(SM5803_EMUL));
}

ZTEST(sm5803, test_bfet_limit_interrupt)
{
	/*
	 * BFET limit interrupts don't take any action,
	 * but do log some messages.
	 */
	sm5803_emul_set_irqs(SM5803_EMUL, 0, 0, SM5803_INT3_BFET_PWR_LIMIT, 0);
	sm5803_handle_interrupt(CHARGER_NUM);
	zassert_false(
		gpio_pin_get_dt(sm5803_emul_get_interrupt_gpio(SM5803_EMUL)),
		"IRQ is still asserted");
}

ZTEST_F(sm5803, test_chg_fail_interrupt)
{
	uint8_t flow1;

	pd_connect_source(fixture);
	sm5803_emul_get_flow_regs(SM5803_EMUL, &flow1, NULL, NULL);
	zassert_equal(flow1, 1, "charger should be sinking, but FLOW1 was %#x",
		      flow1);

	/*
	 * Overtemperature stops charging automatically and restarts after
	 * 30 seconds.
	 */
	sm5803_emul_set_chg_status(SM5803_EMUL, SM5803_STATUS_CHG_OV_ITEMP);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, 0, 0, SM5803_INT4_CHG_FAIL);
	sm5803_handle_interrupt(CHARGER_NUM);
	/* Fault flag was cleared */
	zassert_equal(sm5803_emul_get_chg_status(SM5803_EMUL), 0);
	k_sleep(K_SECONDS(35));

	/* Battery overvoltage is the same but restarts more quickly. */
	sm5803_emul_set_chg_status(SM5803_EMUL, SM5803_STATUS_CHG_OV_VBAT);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, 0, 0, SM5803_INT4_CHG_FAIL);
	sm5803_handle_interrupt(CHARGER_NUM);
	zassert_equal(sm5803_emul_get_chg_status(SM5803_EMUL), 0);
	k_sleep(K_SECONDS(5));
}

ZTEST(sm5803, test_chg_done_interrupt)
{
	/* CHG_DONE just logs a message and takes no further action. */
	sm5803_emul_set_irqs(SM5803_EMUL, 0, 0, 0, SM5803_INT4_CHG_DONE);
	sm5803_handle_interrupt(CHARGER_NUM);
}

FAKE_VOID_FUNC(board_overcurrent_event, int, int);

ZTEST_F(sm5803, test_otg_fail_interrupt)
{
	sm5803_emul_set_disch_status(SM5803_EMUL,
				     SM5803_STATUS_DISCHG_VBUS_SHORT);
	sm5803_emul_set_irqs(SM5803_EMUL, 0, 0, 0, SM5803_INT4_OTG_FAIL);
	sm5803_handle_interrupt(CHARGER_NUM);

	/*
	 * TODO(b/223901282): actually connect a sink so the overcurrent event
	 * gets logged and we can verify that source mode got disabled.
	 */
	zassert_equal(board_overcurrent_event_fake.call_count, 0,
		      "actual call count was %d",
		      board_overcurrent_event_fake.call_count);
}

ZTEST(sm5803, test_set_prochot_duration)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				      i2c_log_read_chg, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				       i2c_log_write_chg, log_ptr);

	zassert_ok(sm5803_set_phot_duration(CHARGER_NUM, 4));
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT1, 0x40);

	/* Error is handled */
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_PHOT1);
	zassert_true(sm5803_set_phot_duration(CHARGER_NUM, 4));
}

ZTEST(sm5803, test_set_vbus_monitor_sel)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				      i2c_log_read_chg, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				       i2c_log_write_chg, log_ptr);

	zassert_ok(sm5803_set_vbus_monitor_sel(CHARGER_NUM, 4));
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT2, 0x24);

	/* Error is handled */
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_PHOT2);
	zassert_true(sm5803_set_vbus_monitor_sel(CHARGER_NUM, 4));
}

ZTEST(sm5803, test_set_vsys_monitor_sel)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				      i2c_log_read_chg, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				       i2c_log_write_chg, log_ptr);

	zassert_ok(sm5803_set_vsys_monitor_sel(CHARGER_NUM, 10));
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT3, 0xa);

	/* Error is handled */
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_PHOT3);
	zassert_true(sm5803_set_vsys_monitor_sel(CHARGER_NUM, 10));
}

ZTEST(sm5803, test_set_ibat_phot_sel)
{
	struct i2c_log log = {};
	struct i2c_log *const log_ptr = &log;

	i2c_common_emul_set_read_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				      i2c_log_read_chg, log_ptr);
	i2c_common_emul_set_write_func(sm5803_emul_get_i2c_chg(SM5803_EMUL),
				       i2c_log_write_chg, log_ptr);

	zassert_ok(sm5803_set_ibat_phot_sel(CHARGER_NUM, 37800));
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT4, 0x3f);

	zassert_ok(sm5803_set_ibat_phot_sel(CHARGER_NUM, 38000));
	LOG_ASSERT_RW(SM5803_ADDR_CHARGER_FLAGS, SM5803_REG_PHOT4, 0x3f);

	/* Error is handled */
	i2c_common_emul_set_write_fail_reg(sm5803_emul_get_i2c_chg(SM5803_EMUL),
					   SM5803_REG_PHOT4);
	zassert_true(sm5803_set_ibat_phot_sel(CHARGER_NUM, 37800));
}

static void pd_disconnect(struct sm5803_fixture *fixture)
{
	if (pd_is_connected(CHARGER_NUM)) {
		switch (pd_get_power_role(CHARGER_NUM)) {
		case PD_ROLE_SOURCE:
			zassert_ok(tcpci_emul_disconnect_partner(
				fixture->tcpci_emul));
			break;
		case PD_ROLE_SINK:
			set_ac_enabled(false);
			zassert_ok(tcpci_emul_disconnect_partner(
				fixture->tcpci_emul));
			sm5803_emul_set_vbus_voltage(SM5803_EMUL, 0);
			break;
		}

		k_sleep(K_SECONDS(1));
	}
}

static void pd_connect_source(struct sm5803_fixture *fixture)
{
	fixture->partner.extensions =
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner, NULL);

	zassert_ok(tcpci_partner_connect_to_tcpci(&fixture->partner,
						  fixture->tcpci_emul));
	sm5803_emul_set_vbus_voltage(SM5803_EMUL, 5000);
	k_sleep(K_SECONDS(10));
	zassert_true(pd_is_connected(CHARGER_NUM));
}

static void *sm5803_test_setup(void)
{
	static struct sm5803_fixture fixture = {
		.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc),
	};

	return &fixture;
}

static void sm5803_before_test(void *data)
{
	struct sm5803_fixture *fixture = data;

	/* Turn AP on so we're allowed to source on type-C. */
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));

	tcpci_partner_init(&fixture->partner, PD_REV30);

	/* Ensure the driver's cached device ID is a "typical" chip. */
	dev_id = 3;

	system_get_jump_tag_fake.custom_fake = NULL;
	RESET_FAKE(board_overcurrent_event);
	RESET_FAKE(extpower_is_present);
}

static void sm5803_after_test(void *data)
{
	struct sm5803_fixture *fixture = data;

	pd_disconnect(fixture);
}
