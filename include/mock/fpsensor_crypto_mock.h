/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file fpsensor_crypto_mock.h
 * @brief Controls for the mock fpsensor_crypto library
 */

#ifndef __MOCK_FPSENSOR_CRYPTO_MOCK_H
#define __MOCK_FPSENSOR_CRYPTO_MOCK_H

enum mock_ctrl_fpsensor_crypto_sha256_type {
	MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_REAL,
	MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_ZEROS,
	MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_FF,
};

struct mock_ctrl_fpsensor_crypto {
	enum mock_ctrl_fpsensor_crypto_sha256_type output_type;
};

#define MOCK_CTRL_DEFAULT_FPSENSOR_CRYPTO    \
	((struct mock_ctrl_fpsensor_crypto){ \
		.output_type =               \
			MOCK_CTRL_FPSENSOR_CRYPTO_HKDF_SHA256_TYPE_REAL })

extern struct mock_ctrl_fpsensor_crypto mock_ctrl_fpsensor_crypto;

#endif /* __MOCK_FPSENSOR_CRYPTO_MOCK_H */
