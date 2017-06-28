/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "common.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "signing.h"
#include "task.h"

#include "cryptoc/sha.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define fail() cprints(CC_SYSTEM, "FAIL: %s:%d", __FILE__, __LINE__)

static p256_int x, y, d;

static HASH_CTX sig_sha[stream_count];

enum signer_states {
	state_notready = 0,
	state_ready,
	state_started,
};

/* Current state of each signer stream. */
static int signer_state[stream_count];

/* Bytes ingested into the hash so far. */
static int signer_bytes[stream_count];

/* Human readable name of each stream. */
static const char *signer_name[stream_count] = {
	"spi", "uart"
};

void init_signing(void)
{
	/* Add this enum to dcrypto.h */
	enum dcrypto_appid appid = PERSO_AUTH;
	struct APPKEY_CTX ctx;
	uint32_t key_bytes[8];
	const uint32_t PERSO_SALT[8] = {0xd00d1e, 0xba0, 0xc0ffee};

	/*
	 * Initialize signing key
	 */
	if (!DCRYPTO_appkey_init(appid, &ctx))
		fail();
	if (!DCRYPTO_appkey_derive(appid, PERSO_SALT, key_bytes))
		fail();
	if (!DCRYPTO_p256_key_from_bytes(&x, &y, &d,
					(const uint8_t *)key_bytes))
		fail();

	/* (x,y) = pubkey, d = privkey */
	signer_state[stream_uart] = state_ready;
	signer_state[stream_spi] = state_ready;
}

/*
 * Start collecting data into a hash to be signed.
 * stream_id can be either stream_uart or stream_spi.
 */
int sig_start(enum stream_id id)
{
	if ((id < 0) || (id >= stream_count))
		return EC_ERROR_PARAM1;

	if (signer_state[id] != state_ready) {
		CPRINTS("Signer %d not ready", id);
		return EC_ERROR_INVAL;
	}

	/* Zero the hash. */
	DCRYPTO_SHA256_init(&sig_sha[id], 0);
	signer_bytes[id] = 0;
	signer_state[id] = state_started;

	return EC_SUCCESS;
}

/*
 * Append data into this stream's hash for future signing.
 * This function is called inline with data receive, from the UART rx code
 * or the SPI rx code.
 *
 * This can be called any time, but only hashes data when the stream
 * capture is started.
 */
int sig_append(enum stream_id id, const uint8_t *data, size_t data_len)
{
	HASH_CTX *sha = &sig_sha[id];
	const uint8_t *blob = data;
	size_t len = data_len;

	if ((id < 0) || (id >= stream_count))
		return EC_ERROR_PARAM1;

	if (signer_state[id] != state_started)
		return EC_ERROR_INVAL;

	HASH_update(sha, blob, len);
	signer_bytes[id] += len;

	return EC_SUCCESS;
}

/* Close this stream's capture and print out the signature. */
int sig_sign(enum stream_id id)
{
	HASH_CTX *sha = &sig_sha[id];
	p256_int r, s;  /* signature tuple */
	p256_int digest;
	struct drbg_ctx drbg;

	if ((id < 0) || (id >= stream_count))
		return EC_ERROR_PARAM1;

	if (signer_state[id] != state_started) {
		CPRINTS("Signer %d not starter", id);
		return EC_ERROR_INVAL;
	}

	p256_from_bin(HASH_final(sha), &digest);
	drbg_rand_init(&drbg);

	if (!dcrypto_p256_ecdsa_sign(&drbg, &d, &digest, &r, &s)) {
		fail();
		return EC_ERROR_INVAL;
	}

	/* Check that the signature was correctly computed */
	if (!dcrypto_p256_ecdsa_verify(&x, &y, &digest, &r, &s)) {
		fail();
		return EC_ERROR_INVAL;
	}

	/* Serialize r, s into output. */

	CPRINTS("Signed %d bytes from %s.", signer_bytes[id], signer_name[id]);
	CPRINTS("digest:");
	CPRINTS("%08x %08x %08x %08x",
		digest.a[0], digest.a[1], digest.a[2], digest.a[3]);
	CPRINTS("%08x %08x %08x %08x",
		digest.a[4], digest.a[5], digest.a[6], digest.a[7]);
	CPRINTS("r:");
	CPRINTS("%08x %08x %08x %08x", r.a[0], r.a[1], r.a[2], r.a[3]);
	CPRINTS("%08x %08x %08x %08x", r.a[4], r.a[5], r.a[6], r.a[7]);
	CPRINTS("s:");
	CPRINTS("%08x %08x %08x %08x", s.a[0], s.a[1], s.a[2], s.a[3]);
	CPRINTS("%08x %08x %08x %08x", s.a[4], s.a[5], s.a[6], s.a[7]);

	signer_state[id] = state_ready;
	return EC_SUCCESS;
}


/*
 * Intercept UART data between the uart driver and usb bridge.
 *
 * This code is called by the ec's queue implementation, and ingests
 * the UART RX queue, appends the data to the signer, then passes it
 * on the the USB bridge's TX queue.
 */
void signer_written(struct consumer const *consumer, size_t count)
{
	struct signer_config const *config =
		DOWNCAST(consumer, struct signer_config, consumer);
	struct producer const *producer = &(config->producer);
	enum stream_id id = config->id;

	/* This queue receives characters from the UART. */
	struct queue const *sig_in = consumer->queue;

	/*
	 * This enqueues characters into the USB bridge,
	 *  once they have been hashed.
	 */
	struct queue const *sig_out = producer->queue;
	char c;

	/* Copy UART rx from queue. */
	while (queue_count(sig_in) && QUEUE_REMOVE_UNITS(sig_in, &c, 1)) {
		/* Append this data to the hash. */
		sig_append(id, &c, 1);
		/* Pass the data to the USB bridge. */
		QUEUE_ADD_UNITS(sig_out, &c, 1);
	}
}

struct producer_ops const signer_producer_ops = {
	.read = NULL,
};

struct consumer_ops const signer_consumer_ops = {
	.written = signer_written,
	.flush   = NULL,
};
