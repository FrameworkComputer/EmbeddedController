/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "CryptoEngine.h"

/*
 * This is unfortunate, but this is the only way to bring in necessary data
 * from the TPM2 library, as this file is not compiled in embedded mode.
 */
#include "CpriHashData.c"
