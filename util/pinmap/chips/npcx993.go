// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package chips

import (
	"fmt"
	"strings"
)

// As provided by Nuvoton.
var npcx993_pins map[string]string = map[string]string{
	"E7":  "PSL_IN2#&GPI00,GPIO00",
	"E6":  "GPIO01,PSL_IN3#&GPI01",
	"F7":  "GPIO02,PSL_IN4#&GPI02",
	"D9":  "KSO16,GPIO03",
	"D11": "KSO13,GPIO04",
	"C11": "KSO12,GPIO05",
	"B10": "KSO11&P80_DAT,GPIO06",
	"B11": "KSO10&P80_CLK,GPIO07",
	"C10": "KSO09,GPIO10,CR_SIN1",
	"C9":  "KSO08,GPIO11,CR_SOUT1",
	"B9":  "KSO07,GPO12,JEN#",
	"C8":  "KSO06,GPO13,GP_SEL#",
	"C6":  "KSO05,GPIO14",
	"C7":  "KSO04,GPIO15,XNOR",
	"B8":  "KSO03,GPIO16,JTAG_TDO0_SWO0",
	"B7":  "KSO02,GPIO17,JTAG_TDI0",
	"B6":  "KSO01,GPIO20,JTAG_TMS0_SWIO0",
	"B5":  "KSO00,GPIO21,JTAG_TCK0_SWCLK0",
	"C5":  "KSI7,GPIO22,S_SBUA",
	"C4":  "KSI6,GPIO23,S_SBUB",
	"C3":  "KSI5,GPIO24,,GP_MISO",
	"B4":  "KSI4,GPIO25,TRACECLK,GP_SCLK",
	"B3":  "KSI3,GPIO26,TRACEDATA0",
	"A4":  "KSI2,GPIO27,TRACEDATA1",
	"A3":  "KSI1,GPIO30,TRACEDATA2,GP_CS#",
	"A2":  "KSI0,GPIO31,TRACEDATA3,GP_MOSI",
	"E4":  "GPO32,TRIS#",
	"D5":  "GPIO33,CTS#,I2C5_SCL0",
	"B2":  "GPIO34,PS2_DAT2,ADC6",
	"K2":  "GPO35,CR_SOUT4,TEST#",
	"D4":  "GPIO36,RTS#,I2C5_SDA0",
	"C1":  "GPIO37,PS2_CLK2,ADC5",
	"E5":  "GPIO40,TA1",
	"F4":  "GPIOE0,ADC10",
	"C2":  "GPIO41,ADC4",
	"D2":  "GPIOF0,ADC9",
	"D1":  "AVCC",
	"D3":  "GPIO42,ADC3,RI#",
	"E2":  "GPIO43,ADC2",
	"E3":  "GPIO44,ADC1",
	"F2":  "GPIO45,ADC0",
	"E1":  "AVSS",
	"F3":  "GPIOE1,ADC7",
	"G3":  "GPIOF1,ADC8",
	"H1":  "LAD0,GPIO46",
	"J1":  "LAD1,GPIO47",
	"G10": "GPIO50",
	"K1":  "LAD2,GPIO51",
	"L1":  "LAD3,GPIO52",
	"L2":  "LFRAME#,GPIO53",
	"K3":  "LRESET#,GPIO54",
	"M1":  "PCI_CLK,GPIO55",
	"M2":  "GPIO56,CLKRUN#",
	"L3":  "SER_IRQ,GPIO57",
	"F1":  "VHIF",
	"L7":  "GPIOE3,I2C6_SDA1,I3C_SDA",
	"L6":  "GPIOE4,I2C6_SCL1,I3C_SCL",
	"G6":  "GPIO60,PWM7",
	"K4":  "GPIO61,PWROFF#",
	"H2":  "GPIO62,PS2_CLK1",
	"J2":  "GPIO63,PS2_DAT1",
	"G4":  "GPIO64,CR_SIN1",
	"H4":  "GPO65,CR_SOUT1,FLPRG1#",
	"G2":  "GPIO66",
	"J3":  "GPIO67,PS2_CLK0",
	"J4":  "GPIO70,PS2_DAT0",
	"L4":  "VBAT",
	"M4":  "PWRGD,GPIO72",
	"M5":  "32KXOUT",
	"L5":  "32KXIN&32KCLKIN",
	"G5":  "GPIO73,TA2",
	"H5":  "GPIO74",
	"J6":  "GPIO75,32KHZ_OUT,RXD,CR_SIN2",
	"J5":  "GPIO76,EC_SCI#",
	"K6":  "VCC1_RST#,GPO77",
	"K5":  "GPIO80,PWM3",
	"M6":  "VREF_PECI",
	"M7":  "PECI_DATA,GPIO81",
	"M10": "VSBY",
	"J8":  "PSL_OUT&GPIO85,GPO85",
	"H6":  "PSL_GPO,GPOD7",
	"D6":  "KSO14,GPIO82",
	"D7":  "KSO15,GPIO83",
	"J9":  "GPO86,TXD,CR_SOUT2,FLPRG2#",
	"K7":  "GPIO87,I2C1_SDA0",
	"K8":  "GPIO90,I2C1_SCL0",
	"K9":  "GPIO91,I2C2_SDA0",
	"L8":  "GPIO92,I2C2_SCL0",
	"E11": "GPIO93,TA1,F_DIO2",
	"M11": "GPIO94",
	"M12": ",SPIP_MISO,GPIO95",
	"G12": "F_DIO1,GPIO96",
	"L10": ",GPIO97",
	"G11": "F_CS0#,GPIOA0",
	"L12": ",SPIP_SCLK,GPIOA1",
	"F12": "F_SCLK,GPIOA2",
	"K12": ",SPIP_MOSI,GPIOA3",
	"H11": "F_DIO0,GPIOA4,TB1",
	"K11": "GPIOA5",
	"F11": "GPIOA6,PS2_CLK3,TA2,F_CS1#",
	"J11": "GPIOA7,PS2_DAT3,TB2,F_DIO3",
	"H12": "VSPI",
	"L11": "GPIOB0",
	"D8":  "KSO17,GPIOB1,CR_SIN4",
	"K10": "GPIOB2,I2C7_SDA0,DSR#",
	"J10": "GPIOB3,I2C7_SCL0,DCD#",
	"B12": "GPIOB4,I2C0_SDA0",
	"C12": "GPIOB5,I2C0_SCL0",
	"L9":  "GPIOB6,PWM4",
	"J7":  "GPIOB7,PWM5",
	"H8":  "GPIOC0,PWM6",
	"H9":  "GPIOC1,I2C6_SDA0",
	"H10": "GPIOC2,PWM1,I2C6_SCL0",
	"G9":  "GPIOC3,PWM0",
	"G8":  "GPIOC4,PWM2",
	"H7":  "GPIOC5,KBRST#",
	"D10": "GPIOC6,SMI#",
	"F10": "GPIOC7,DTR#_BOUT,ADC11",
	"F9":  "GPIOD0,I2C3_SDA0",
	"F8":  "GPIOD1,I2C3_SCL0",
	"G7":  "PSL_IN1#&GPID2,GPIOD2",
	"E10": "GPIOD3,TB1",
	"A9":  "GPIOD4,CR_SIN3",
	"A10": "GPIOD5,INTRUDER#",
	"H3":  "GPOD6,CR_SOUT3,SHDF_ESPI#",
	"A11": "GPIOE2",
	"A12": "GPIOE5",
	"F6":  "GPIOF2,I2C4_SDA1",
	"F5":  "GPIOF3,I2C4_SCL1",
	"E9":  "GPIOF4,I2C5_SDA1",
	"E8":  "GPIOF5,I2C5_SCL1",
}

type Npcx993 struct {
	okay []string // Nodes to enable.
}

// Name returns the name of the chip.
func (c *Npcx993) Name() string {
	return "NPCX993"
}

// EnabledNodes returns the list of node names that are to
// enabled in DTS.
func (c *Npcx993) EnabledNodes() []string {
	return c.okay
}

// Adc returns the ADC config associated with this pin.
func (c *Npcx993) Adc(p string) string {
	s, ok := npcx993_pins[p]
	if ok {
		// Found the pin, now find the ADC name.
		for _, ss := range strings.Split(s, ",") {
			if strings.HasPrefix(ss, "ADC") && len(ss) > 3 {
				c.okay = append(c.okay, "adc0") // Enable ADC
				return ss[3:]
			}
		}
		return ""
	} else {
		return ""
	}
}

// Gpio returns the GPIO config for this pin, as
// a GPIO controller name and a pin number.
func (c *Npcx993) Gpio(p string) (string, int) {
	s, ok := npcx993_pins[p]
	if ok {
		// Found the pin, now find the GP name.
		for _, ss := range strings.Split(s, ",") {
			var offs int
			if strings.HasPrefix(ss, "GPO") && len(ss) == 5 {
				offs = 3
			} else if strings.HasPrefix(ss, "GPIO") && len(ss) == 6 {
				offs = 4
			} else {
				continue
			}
			lc := strings.ToLower(ss)
			pin := int(lc[offs+1] - '0')
			if pin < 0 || pin > 9 {
				fmt.Printf("Pin value is %d\n", pin)
				return "", 0
			}
			return fmt.Sprintf("gpio%c", lc[offs]), pin
		}
		return "", 0
	} else {
		return "", 0
	}
}

// I2c returns the I2C config for this pin.
// Searches for the pattern I2Cx_SCLy.
func (c *Npcx993) I2c(p string) string {
	s, ok := npcx993_pins[p]
	if ok {
		// Found the pin, now find the I2C port.
		for _, ss := range strings.Split(s, ",") {
			if len(ss) != 9 {
				continue
			}
			if strings.HasPrefix(ss, "I2C") &&
				ss[4:8] == "_SCL" {
				i2c := fmt.Sprintf("i2c%c_%c", ss[3], ss[8])
				c.okay = append(c.okay, i2c)
				c.okay = append(c.okay, fmt.Sprintf("i2c_ctrl%c", ss[3]))
				return i2c
			}
		}
		return ""
	} else {
		return ""
	}
}
