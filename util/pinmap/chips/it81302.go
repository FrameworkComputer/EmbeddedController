// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package chips

import (
	"fmt"
	"strings"
)

// As provided by ITE.
var it81302_pins map[string]string = map[string]string{
	"A1":  "GPL5",
	"A2":  "GPL4",
	"A3":  "SMDAT1/GPC2",
	"A4":  "SMCLK1/GPC1",
	"A5":  "SMCLK0/GPB3",
	"A6":  "FSCK/GPG7",
	"A7":  "FMOSI/GPG4",
	"A8":  "GPH6/ID6",
	"A9":  "GPH4/ID4",
	"A10": "SMINT11/PD1CC2/GPF5",
	"A11": "RTS0#/SMINT9/GPF3",
	"A12": "GPF1",
	"A13": "ADC13/GPL0",
	"A14": "ADC14/GPL1",
	"A15": "ADC15/GPL2",
	"B1":  "GPL6",
	"B2":  "RING#/PWRFAIL#/CK32KOUT/LPCRST#/GPB7",
	"B3":  "SMDAT0/GPB4",
	"B4":  "SOUT0/GPB1",
	"B5":  "SIN0/GPB0",
	"B6":  "DTR1#/GPG1/ID7",
	"B7":  "FMISO/GPG5",
	"B8":  "FSCE#/GPG3",
	"B9":  "GPH5/ID5",
	"B10": "GPH3/ID3",
	"B11": "DTR0#/SMINT8/GPF2",
	"B12": "CEC/GPF0",
	"B13": "GPE2",
	"B14": "GPE1",
	"B15": "ADC16/GPL3",
	"C1":  "GPL7",
	"C2":  "SMCLK2/PECI/GPF6",
	"C14": "DCD0#/GPJ4",
	"C15": "GPE3",
	"D1":  "GPC4",
	"D2":  "SMDAT2/PECIRQT#/GPF7",
	"D14": "TACH1B/SMINT7/GPJ3",
	"D15": "RIG0#/GPJ5",
	"E1":  "GPB2",
	"E2":  "GPC0",
	"E5":  "VSTBY",
	"E6":  "VFSPI",
	"E7":  "DSR0#/GPG6",
	"E8":  "SOUT1/SMDAT3/PD2CC2/GPH2/ID2",
	"E9":  "CLKRUN#/GPH0/ID0",
	"E10": "SMINT10/PD1CC1/GPF4",
	"E11": "VSTBY",
	"E14": "SMINT5/GPJ1",
	"E15": "TACH0B/SMINT6/GPJ2",
	"F1":  "PWRSW/GPE4",
	"F2":  "GPC6",
	"F5":  "VSTBY",
	"F6":  "VSS",
	"F7":  "SSCE1#/GPG0",
	"F8":  "SSCE0#/GPG2",
	"F9":  "SIN1/SMCLK3/PD2CC1/GPH1/ID1",
	"F10": "AVCC",
	"F11": "AVSS",
	"F14": "ADC7/CTS1#/GPI7",
	"F15": "TACH2/SMINT4/GPJ0",
	"G1":  "CK32K/GPJ6",
	"G2":  "GA20/GPB5",
	"G5":  "VSS",
	"G6":  "VSS",
	"G10": "ADC3/SMINT2/GPI3",
	"G11": "ADC5/DCD1#/GPI5",
	"G14": "ADC4/SMINT3/GPI4",
	"G15": "ADC6/DSR1#/GPI6",
	"H1":  "ALERT#/SERIRQ/GPM6",
	"H2":  "GPJ7",
	"H5":  "VSS",
	"H6":  "VSS",
	"H10": "KSI7",
	"H11": "ADC0/GPI0",
	"H14": "ADC1/SMINT0/GPI1",
	"H15": "ADC2/SMINT1/GPI2",
	"J1":  "EIO3/LAD3/GPM3",
	"J2":  "ECS#/LFRAME#/GPM5",
	"J5":  "KBRST#/GPB6",
	"J6":  "VSS",
	"J10": "KSI4",
	"J11": "KSI5",
	"J14": "KSI6",
	"J15": "KSI3/SLIN#",
	"K1":  "EIO1/LAD1/GPM1",
	"K2":  "EIO2/LAD2/GPM2",
	"K5":  "VBAT",
	"K6":  "VCC",
	"K7":  "PWM5/SMDAT5/GPA5",
	"K8":  "KSO1/PD1",
	"K9":  "KSO5/PD5",
	"K10": "KSI2/INIT#",
	"K11": "KSO17/SMISO/GPC5",
	"K14": "KSI1/AFD#",
	"K15": "KSI0/STB#",
	"L1":  "ESCK/LPCCLK/GPM4",
	"L2":  "EIO0/LAD0/GPM0",
	"L5":  "VSTBY",
	"L6":  "VCORE",
	"L7":  "PWM4/SMCLK5/GPA4",
	"L8":  "PWM7/RIG1#/GPA7",
	"L9":  "KSO4/PD4",
	"L10": "KSO9/BUSY",
	"L11": "VSTBY",
	"L14": "KSO16/SMOSI/GPC3",
	"L15": "KSO15",
	"M1":  "ECSMI#/GPD4",
	"M2":  "WRST#",
	"M14": "KSO14",
	"M15": "KSO13",
	"N1":  "PWUREQ#/BBO/SMCLK2ALT/GPC7",
	"N2":  "LPCPD#/GPE6",
	"N14": "KSO12/SLCT",
	"N15": "WUI14/GPK6",
	"P1":  "WUI8/GPK0",
	"P2":  "RI1#/GPD0",
	"P3":  "L80HLAT/BAO/SMCLK4/GPE0",
	"P4":  "RI2#/GPD1",
	"P5":  "ECSCI#/GPD3",
	"P6":  "PWM1/GPA1",
	"P7":  "PWM3/GPA3",
	"P8":  "GINT/CTS0#/GPD5",
	"P9":  "RTS1#/GPE5",
	"P10": "KSO2/PD2",
	"P11": "KSO6/PD6",
	"P12": "KSO8/ACK#",
	"P13": "KSO10/PE",
	"P14": "KSO11/ERR#",
	"P15": "WUI15/GPK7",
	"R1":  "WUI9/GPK1",
	"R2":  "WUI10/GPK2",
	"R3":  "WUI11/GPK3",
	"R4":  "L80LLAT/SMDAT4/GPE7",
	"R5":  "ERST#/LPCRST#/GPD2",
	"R6":  "PWM0/GPA0",
	"R7":  "PWM2/GPA2",
	"R8":  "PWM6/SSCK/GPA6",
	"R9":  "KSO0/PD0",
	"R10": "KSO3/PD3",
	"R11": "KSO7/PD7",
	"R12": "TACH0A/GPD6",
	"R13": "TACH1A/GPD7",
	"R14": "WUI13/GPK5",
	"R15": "WUI12/GPK4",
}

// it81302 represents an ITE81302 EC.
type It81302 struct {
	okay []string // Nodes to enable.
}

// Name returns the name of this EC.
func (c *It81302) Name() string {
	return "IT81302"
}

// EnabledNodes returns a list of the DTS nodes that require enabling.
func (c *It81302) EnabledNodes() []string {
	return c.okay
}

// Adc returns the configuration of this pin as an ADC.
func (c *It81302) Adc(p string) string {
	s, ok := it81302_pins[p]
	if ok {
		// Found the pin, now find the ADC name.
		for _, ss := range strings.Split(s, "/") {
			if strings.HasPrefix(ss, "ADC") && len(ss) > 3 {
				c.okay = append(c.okay, "adc0")
				return fmt.Sprintf("%s", ss[3:])
			}
		}
		return ""
	} else {
		return ""
	}
}

// Gpio returns the configuration of this pin as a
// GPIO controller name and a pin number.
func (c *It81302) Gpio(p string) (string, int) {
	s, ok := it81302_pins[p]
	if ok {
		// Found the pin, now find the GP name.
		for _, ss := range strings.Split(s, "/") {
			if strings.HasPrefix(ss, "GP") && len(ss) == 4 {
				lc := strings.ToLower(ss)
				pin := int(lc[3] - '0')
				if pin < 0 || pin > 9 {
					return "", 0
				}
				return fmt.Sprintf("gpio%c", lc[2]), pin
			}
		}
		return "", 0
	} else {
		return "", 0
	}
}

// I2c returns the configuration of this pin as an I2C bus.
func (c *It81302) I2c(p string) string {
	s, ok := it81302_pins[p]
	if ok {
		// Found the pin, now find the I2C name.
		for _, ss := range strings.Split(s, "/") {
			if strings.HasPrefix(ss, "SMCLK") && len(ss) > 5 {
				i2c := fmt.Sprintf("i2c%s", ss[5:])
				c.okay = append(c.okay, i2c)
				return i2c
			}
		}
		return ""
	} else {
		return ""
	}
}

// Pwm returns the configuration of this pin as a PWM.
func (c *It81302) Pwm(p string) string {
	s, ok := it81302_pins[p]
	if ok {
		// Found the pin, now find the PWM name.
		for _, ss := range strings.Split(s, "/") {
			if strings.HasPrefix(ss, "PWM") && len(ss) > 3 {
				pwm := fmt.Sprintf("pwm%s", ss[3:])
				c.okay = append(c.okay, pwm)
				return fmt.Sprintf("%s %s", pwm, ss[3:])
			}
		}
		return ""
	} else {
		return ""
	}
}
