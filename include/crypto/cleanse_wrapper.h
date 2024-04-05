/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* A drop-in replacement wrapper for OPENSSL_cleanse. */

#ifndef __CROS_EC_CRYPTO_CLEANSE_WRAPPER_H
#define __CROS_EC_CRYPTO_CLEANSE_WRAPPER_H

#include "openssl/mem.h"

#include <type_traits>
#include <utility>

/* TODO(b/283907495): replace with upstream version. */
/**
 * CleanseWrapper is a drop-in replacement to make sure the data is cleaned
 * after use.
 *
 * @tparam T The type of the data to be wrapped.
 */
template <typename T> class CleanseWrapper : public T {
    public:
	static_assert(
		std::is_trivial_v<T> &&std::is_standard_layout_v<T>,
		"This only works for the type that is trivial and standard layout.");

	/* Inherit all existing constructor and assign operator. */
	using T::T;

	/* Allow implicit conversions. */
	constexpr CleanseWrapper(const T &t)
		: T(t)
	{
	}

	/* Clean the data. */
	~CleanseWrapper()
	{
		OPENSSL_cleanse(this, sizeof(*this));
	}
};

#endif /* __CROS_EC_CRYPTO_CLEANSE_WRAPPER_H */
