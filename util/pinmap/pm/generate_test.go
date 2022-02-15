// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm_test

import (
	"testing"

	"bytes"
	"fmt"
	"strings"
	"time"

	"pinmap/pm"
)

type genChip struct {
}

func (c *genChip) Name() string {
	return "Test"
}

func (c *genChip) EnabledNodes() []string {
	return []string{"adc0", "i2c0", "i2c1", "i2c2", "pwm1"}
}

func (c *genChip) Adc(pin string) string {
	return pin
}

func (c *genChip) Gpio(pin string) (string, int) {

	return fmt.Sprintf("gpio%c", pin[0]), int(pin[1] - '0')
}

func (c *genChip) I2c(pin string) string {
	switch pin {
	case "B2":
		return "i2c2"
	case "B3":
		return "i2c1"
	case "B4":
		return "i2c0"
	}
	panic(fmt.Sprintf("Unknown I2C: %s", pin))
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
			&pm.Pin{pm.I2C, "B4", "EC_C_I2C_CLK", "ENUM_I2C_0"},
			&pm.Pin{pm.I2C, "B3", "EC_B_I2C_CLK", "ENUM_I2C_1"},
			&pm.Pin{pm.I2C, "B2", "EC_A_I2C_CLK", "ENUM_I2C_2"},
		},
		Gpio: []*pm.Pin{
			&pm.Pin{pm.Input, "C3", "EC_IN_1", "ENUM_IN_1"},
			&pm.Pin{pm.Output, "D4", "EC_OUT_2", "ENUM_OUT_2"},
			&pm.Pin{pm.InputPU, "G7", "EC_IN_3", "ENUM_IN_3"},
			&pm.Pin{pm.InputPD, "H8", "EC_IN_4", "ENUM_IN_4"},
		},
		Pwm: []*pm.Pin{
			&pm.Pin{pm.PWM, "E5", "EC_LED_1", "ENUM_LED_1"},
			&pm.Pin{pm.PWM_INVERT, "F6", "EC_LED_2", "ENUM_LED_2"},
		},
	}
	var out bytes.Buffer
	pm.Generate(&out, pins, &genChip{}, true)
	/*
	 * Rather than doing a golden output text compare, it would be better
	 * to parse the device tree directly and ensuing it is correct.
	 * However this would considerably complicate this test.
	 */
	expFmt :=
		`/* Copyright %d The Chromium OS Authors. All rights reserved.
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
			io-channels = <&adc0 A1>;
		};
	};

	named-gpios {
		compatible = "named-gpios";

		gpio_ec_in_1: ec_in_1 {
			gpios = <&gpioC 3 GPIO_INPUT>;
			enum-name = "ENUM_IN_1";
		};
		gpio_ec_in_3: ec_in_3 {
			gpios = <&gpioG 7 GPIO_INPUT_PULL_UP>;
			enum-name = "ENUM_IN_3";
		};
		gpio_ec_in_4: ec_in_4 {
			gpios = <&gpioH 8 GPIO_INPUT_PULL_DOWN>;
			enum-name = "ENUM_IN_4";
		};
		gpio_ec_out_2: ec_out_2 {
			gpios = <&gpioD 4 GPIO_OUTPUT>;
			enum-name = "ENUM_OUT_2";
		};
	};

	named-i2c-ports {
		compatible = "named-i2c-ports";

		i2c_ec_c_i2c_clk: ec_c_i2c_clk {
			i2c-port = <&i2c0>;
			enum-name = "ENUM_I2C_0";
		};
		i2c_ec_b_i2c_clk: ec_b_i2c_clk {
			i2c-port = <&i2c1>;
			enum-name = "ENUM_I2C_1";
		};
		i2c_ec_a_i2c_clk: ec_a_i2c_clk {
			i2c-port = <&i2c2>;
			enum-name = "ENUM_I2C_2";
		};
	};

	named-pwms {
		compatible = "named-pwms";

		pwm_ec_led_1: ec_led_1 {
			pwms = <&pwm1 0>;
			enum-name = "ENUM_LED_1";
		};
		pwm_ec_led_2: ec_led_2 {
			pwms = <&pwm1 1>;
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

&i2c1 {
	status = "okay";
};

&i2c2 {
	status = "okay";
};

&pwm1 {
	status = "okay";
};

&gpioC {
	gpio-line-names =
		"",
		"",
		"",
		"ec_in_1";
};

&gpioD {
	gpio-line-names =
		"",
		"",
		"",
		"",
		"ec_out_2";
};

&gpioG {
	gpio-line-names =
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"ec_in_3";
};

&gpioH {
	gpio-line-names =
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"ec_in_4";
};
`
	exp := fmt.Sprintf(expFmt, time.Now().Year())
	got := out.String()
	if exp != got {
		// Split each string into lines and compare the lines.
		expLines := strings.Split(exp, "\n")
		gotLines := strings.Split(got, "\n")
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
