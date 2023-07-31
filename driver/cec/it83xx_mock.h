/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_DRIVER_CEC_IT83XX_MOCK_H
#define __CROS_EC_DRIVER_CEC_IT83XX_MOCK_H

/* Mock versions of the CEC registers for testing */

#undef IT83XX_CEC_CECDR
#undef IT83XX_CEC_CECFSTS
#undef IT83XX_CEC_CECDLA
#undef IT83XX_CEC_CECCTRL
#undef IT83XX_CEC_CECSTS
#undef IT83XX_CEC_CECIE
#undef IT83XX_CEC_CECOPSTS
#undef IT83XX_CEC_CECRH

#define IT83XX_CEC_CECDR (mock_it83xx_cec_regs.cecdr)
#define IT83XX_CEC_CECFSTS (mock_it83xx_cec_regs.cecfsts)
#define IT83XX_CEC_CECDLA (mock_it83xx_cec_regs.cecdla)
#define IT83XX_CEC_CECCTRL (mock_it83xx_cec_regs.cecctrl)
#define IT83XX_CEC_CECSTS (mock_it83xx_cec_regs.cecsts)
#define IT83XX_CEC_CECIE (mock_it83xx_cec_regs.cecie)
#define IT83XX_CEC_CECOPSTS (mock_it83xx_cec_regs.cecopsts)
#define IT83XX_CEC_CECRH (mock_it83xx_cec_regs.cecrh)

struct mock_it83xx_cec_regs {
	uint8_t cecdr;
	uint8_t cecfsts;
	uint8_t cecdla;
	uint8_t cecctrl;
	uint8_t cecsts;
	uint8_t cecie;
	uint8_t cecopsts;
	uint8_t cecrh;
};

extern struct mock_it83xx_cec_regs mock_it83xx_cec_regs;

#endif /* __CROS_EC_DRIVER_CEC_IT83XX_MOCK_H */
