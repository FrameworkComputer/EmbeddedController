// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm_test

import (
	"testing"

	"bytes"
	"fmt"
	"strings"

	"pinmap/pm"
)

type genChip struct {
}

func (c *genChip) Name() string {
	return "Test"
}

func (c *genChip) EnabledNodes() []string {
	return []string{"adc0", "i2c0", "pwm1"}
}

func (c *genChip) Adc(pin string) string {
	return pin
}

func (c *genChip) Gpio(pin string) string {
	return fmt.Sprintf("gpio %s", pin)
}

func (c *genChip) I2c(pin string) string {
	return "i2c0"
}

func (c *genChip) Pwm(pin string) string {
	return "pwm1"
}

func TestGenerate(t *testing.T) {
	pins := &pm.Pins{
		Adc: []*pm.Pin{
			&pm.Pin{pm.ADC, "A1", "EC_ADC_1", "ENUM_ADC_1"},
		},
		I2c: []*pm.Pin{
			&pm.Pin{pm.I2C, "B2", "EC_I2C_CLK_0", "ENUM_I2C_0"},
		},
		Gpio: []*pm.Pin{
			&pm.Pin{pm.Input, "C3", "EC_IN_1", "ENUM_IN_1"},
			&pm.Pin{pm.Output, "D4", "EC_OUT_2", "ENUM_OUT_2"},
		},
		Pwm: []*pm.Pin{
			&pm.Pin{pm.PWM, "E5", "EC_LED_1", "ENUM_LED_1"},
			&pm.Pin{pm.PWM_INVERT, "F6", "EC_LED_2", "ENUM_LED_2"},
		},
	}
	var out bytes.Buffer
	pm.Generate(&out, pins, &genChip{})
	/*
	 * Rather than doing a golden output text compare, it would be better
	 * to parse the device tree directly and ensuing it is correct.
	 * However this would considerably complicate this test.
	 */
	exp :=
		`/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file is auto-generated - do not edit!
 */

/ {

	named-adc-channels {
		compatible = "named-adc-channels";

		adc_ec_adc_1: ec_adc_1 {
			label = "EC_ADC_1";
			enum-name = "ENUM_ADC_1";
			channel = <A1>;
		};
	};

	named-gpios {
		compatible = "named-gpios";

		gpio_ec_in_1: ec_in_1 {
			gpios = <&gpio C3 GPIO_INPUT>;
			label = "EC_IN_1";
			enum-name = "ENUM_IN_1";
		};
		gpio_ec_out_2: ec_out_2 {
			gpios = <&gpio D4 GPIO_OUTPUT>;
			label = "EC_OUT_2";
			enum-name = "ENUM_OUT_2";
		};
	};

	named-i2c-ports {
		compatible = "named-i2c-ports";

		i2c_ec_i2c_clk_0: ec_i2c_clk_0 {
			i2c-port = <&i2c0>;
			enum-name = "ENUM_I2C_0";
		};
	};

	named-pwms {
		compatible = "named-pwms";

		pwm_ec_led_1: ec_led_1 {
			pwms = <&pwm1 0>;
			label = "EC_LED_1";
			enum-name = "ENUM_LED_1";
		};
		pwm_ec_led_2: ec_led_2 {
			pwms = <&pwm1 1>;
			label = "EC_LED_2";
			enum-name = "ENUM_LED_2";
		};
	};
};

&adc0 {
	status = "okay";
};

&i2c0 {
	status = "okay";
};

&pwm1 {
	status = "okay";
};
`
	got := out.String()
	if exp != got {
		// Split each string into lines and compare the lines.
		expLines := strings.Split(exp, "\n")
		gotLines := strings.Split(exp, "\n")
		if len(expLines) != len(gotLines) {
			t.Errorf("Expected %d lines, got %d lines", len(expLines), len(gotLines))
		}
		for i := range expLines {
			if i < len(gotLines) && expLines[i] != gotLines[i] {
				t.Errorf("%d: exp %s, got %s", i+1, expLines[i], gotLines[i])
			}
		}
	}
}
