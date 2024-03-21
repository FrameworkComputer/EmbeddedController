/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hardware Random Number Generator */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "panic.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"

#define DRBG_CONTEXT_SIZE 240

struct drbg_ctx {
	union {
		uint8_t buffer[DRBG_CONTEXT_SIZE];
		uint32_t buffer32[DRBG_CONTEXT_SIZE / 4];
		uint64_t buffer64[DRBG_CONTEXT_SIZE / 8];
	};
} __aligned(16);

struct drbg_ctx ctx;
struct drbg_ctx *ctx_p = &ctx;

enum ncl_status {
	NCL_STATUS_OK = 0xA5A5,
	NCL_STATUS_FAIL = 0x5A5A,
	NCL_STATUS_INVALID_PARAM = 0x02,
	NCL_STATUS_PARAM_NOT_SUPPORTED,
	NCL_STATUS_SYSTEM_BUSY,
	NCL_STATUS_AUTHENTICATION_FAIL,
	NCL_STATUS_NO_RESPONSE,
	NCL_STATUS_HARDWARE_ERROR,
};

/*
 * This enum defines the security strengths supported by this DRBG mechanism.
 * The internally generated entropy and nonce sizes are derived from these
 * values. The supported actual sizes:
 *       Security strength (bits)    112 128 192 256 128_Test 256_Test
 *
 *       Entropy size (Bytes)        32  48  64  96  111      128
 *       Nonce size (Bytes)          16  16  24  32  16       0
 */
enum ncl_drbg_security_strength {
	NCL_DRBG_SECURITY_STRENGTH_112b = 0,
	NCL_DRBG_SECURITY_STRENGTH_128b,
	NCL_DRBG_SECURITY_STRENGTH_192b,
	NCL_DRBG_SECURITY_STRENGTH_256b,
	NCL_DRBG_SECURITY_STRENGTH_128b_TEST,
	NCL_DRBG_SECURITY_STRENGTH_256b_TEST,
	NCL_DRBG_MAX_SECURITY_STRENGTH
};

/* The actual base address is 13C but we only need the SHA power function */
#define NCL_SHA_BASE_ADDR 0x0000015CUL
struct ncl_sha {
	/* Power on/off SHA module. */
	enum ncl_status (*power)(void *ctx, uint8_t on);
};
#define NCL_SHA ((const struct ncl_sha *)NCL_SHA_BASE_ADDR)

/*
 * The base address of the table that holds the function pointer for each
 * DRBG API in ROM.
 */
#define NCL_DRBG_BASE_ADDR 0x00000110UL
struct ncl_drbg {
	/* Get the DRBG context size required by DRBG APIs. */
	uint32_t (*get_context_size)(void);
	/* Initialize DRBG context. */
	enum ncl_status (*init_context)(void *ctx);
	/* Power on/off DRBG module. */
	enum ncl_status (*power)(void *ctx, uint8_t on);
	/* Finalize DRBG context. */
	enum ncl_status (*finalize_context)(void *ctx);
	/* Initialize the DRBG hardware module and enable interrupts. */
	enum ncl_status (*init)(void *ctx, bool int_enable);
	/*
	 * Configure DRBG, pres_resistance enables/disables (1/0) prediction
	 * resistance
	 */
	enum ncl_status (*config)(void *ctx, uint32_t reseed_interval,
				  uint8_t pred_resistance);
	/*
	 * This routine creates a first instantiation of the DRBG mechanism
	 * parameters. The routine pulls an initial seed from the HW RNG module
	 * and resets the reseed counter. DRBG and SHA modules should be
	 * activated prior to the this operation.
	 */
	enum ncl_status (*instantiate)(
		void *ctx, enum ncl_drbg_security_strength sec_strength,
		const uint8_t *pers_string, uint32_t pers_string_len);
	/* Uninstantiate DRBG module */
	enum ncl_status (*uninstantiate)(void *ctx);
	/* Reseeds the internal state of the given instantce */
	enum ncl_status (*reseed)(void *ctc, uint8_t *add_data,
				  uint32_t add_data_len);
	/* Generates a random number from the current internal state. */
	enum ncl_status (*generate)(void *ctx, const uint8_t *add_data,
				    uint32_t add_data_len, uint8_t *out_buff,
				    uint32_t out_buff_len);
	/* Clear all DRBG SSPs (Sensitive Security Parameters) in HW & driver */
	enum ncl_status (*clear)(void *ctx);
};
#define NCL_DRBG ((const struct ncl_drbg *)NCL_DRBG_BASE_ADDR)

struct npcx_trng_state {
	enum ncl_status trng_init;
};
struct npcx_trng_state trng_state = { .trng_init = 0 };
struct npcx_trng_state *state_p = &trng_state;

uint32_t npcx_trng_power(bool on_off)
{
	enum ncl_status status = NCL_STATUS_FAIL;

	status = NCL_DRBG->power(ctx_p, on_off);
	if (status != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG power returned %x\n", status);
		return status;
	}

	status = NCL_SHA->power(ctx_p, on_off);
	if (status != NCL_STATUS_OK) {
		ccprintf("ERROR! SHA power returned %x\n", status);
		return status;
	}

	return status;
}

void npcx_trng_hw_init(void)
{
#ifndef CHIP_VARIANT_NPCX9MFP
#error "Please add support for CONFIG_RNG on this chip family."
#endif

	uint32_t context_size = 0;

	state_p->trng_init = NCL_STATUS_FAIL;

	context_size = NCL_DRBG->get_context_size();
	if (context_size != DRBG_CONTEXT_SIZE)
		ccprintf("ERROR! Unexpected NCL DRBG context_size = %d\n",
			 context_size);

	state_p->trng_init = npcx_trng_power(true);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! npcx_trng_power returned %x\n",
			 state_p->trng_init);
		return;
	}

	state_p->trng_init = NCL_DRBG->init_context(ctx_p);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG init_context returned %x\r",
			 state_p->trng_init);
		return;
	}

	state_p->trng_init = NCL_DRBG->init(ctx_p, 0);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG init returned %x\r", state_p->trng_init);
		return;
	}

	/* Disable automatic reseeding since it takes a long time and can cause
	 * host commands to timeout. See See b/322827873 for more details.
	 *
	 * The DRBG algorithm used is Hash_DRBG, which has a maxmium of 2^48
	 * requests between reseeds (reseed_interval). See NIST SP 800-90A Rev.
	 * 1, Section 10.1: DRBG Mechanisms Based on Hash Functions.
	 *
	 * https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-90Ar1.pdf#page=47
	 */
	const uint32_t reseed_interval = UINT32_MAX;
	state_p->trng_init = NCL_DRBG->config(ctx_p, reseed_interval, false);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG config returned %x\r",
			 state_p->trng_init);
		return;
	}

	/* NIST SP 800-90A Rev. 1 Section 8.4 states:
	 *
	 * The pseudorandom bits returned from a DRBG shall not be used for any
	 * application that requires a higher security strength than the DRBG is
	 * instantiated to support. The security strength provided in these
	 * returned bits is the minimum of the security strength supported by
	 * the DRBG and the length of the bit string returned, i.e.:
	 *
	 * Security_strength_of_output =
	 *   min(output_length, DRBG_security_strength)
	 *
	 * A concatenation of bit strings resulting from multiple calls to a
	 * DRBG will not provide a security strength for the concatenated string
	 * that is greater than the instantiated security strength of the DRBG.
	 * For example, two 128-bit output strings requested from a DRBG that
	 * supports a 128-bit security strength cannot be concatenated to form a
	 * 256-bit string with a security strength of 256 bits.
	 *
	 * https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-90Ar1.pdf#page=23
	 */
	state_p->trng_init = NCL_DRBG->instantiate(
		ctx_p, NCL_DRBG_SECURITY_STRENGTH_256b, NULL, 0);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG instantiate returned %x\r",
			 state_p->trng_init);
		return;
	}

	/* Turn off hardware blocks after hw_init, trng_init will power on */
	state_p->trng_init = npcx_trng_power(false);
	if (state_p->trng_init != NCL_STATUS_OK) {
		ccprintf("ERROR! npcx_trng_power returned %x\n",
			 state_p->trng_init);
		return;
	}
}

test_mockable void trng_init(void)
{
	enum ncl_status status = NCL_STATUS_FAIL;

	status = npcx_trng_power(true);
	if (status != NCL_STATUS_OK) {
		ccprintf("ERROR! trng_init failed %x\n", status);
		software_panic(PANIC_SW_BAD_RNG, task_get_current());
	}
}

uint32_t trng_rand(void)
{
	uint32_t return_value;
	enum ncl_status status = NCL_STATUS_FAIL;

	/* Don't attempt generate and panic if initialization failed */
	if (state_p->trng_init != NCL_STATUS_OK)
		software_panic(PANIC_SW_BAD_RNG, task_get_current());

	status =
		NCL_DRBG->generate(ctx_p, NULL, 0, (uint8_t *)&return_value, 4);
	if (status != NCL_STATUS_OK) {
		ccprintf("ERROR! DRBG generate returned %x\r", status);
		software_panic(PANIC_SW_BAD_RNG, task_get_current());
	}

	return return_value;
}

test_mockable void trng_exit(void)
{
	enum ncl_status status = NCL_STATUS_FAIL;

	status = npcx_trng_power(false);
	if (status != NCL_STATUS_OK)
		ccprintf("ERROR! trng_exit failed %x\n", status);
}

/* Shutting down and reinitializing TRNG is time consuming so don't call
 * this unless it is necessary
 */
test_mockable void npcx_trng_hw_off(void)
{
	enum ncl_status status = NCL_STATUS_FAIL;

	status = NCL_DRBG->clear(ctx_p);
	if (status != NCL_STATUS_OK)
		ccprintf("ERROR! DRBG clear returned %x\r", status);

	status = NCL_DRBG->uninstantiate(ctx_p);
	if (status != NCL_STATUS_OK)
		ccprintf("ERROR! DRBG uninstantiate returned %x\r", status);

	status = npcx_trng_power(false);
	if (status != NCL_STATUS_OK)
		ccprintf("ERROR! npcx_trng_power returned %x\n", status);
}
