# EC Acronyms and Technologies

## Glossary
*   **8042 Interface**{#8042}

    Interface for sending keyboard events to the [AP](#ap) and for receiving
    commands from the AP. Only supported by x86 based APs.

*   **ACCEL - Accelerometer**{#accel}

    A sensor that measures acceleration, typically over 3-axis. Nominally
    provides information about the orientation of a device. On Chromebook 2-in-1
    devices, there is an accelerometer in the base and one in the lid. Combining
    the measurements from both accelerometers allows for a precise calculation
    of the lid angle, used to switch between tablet and laptop mode.

*   **ACCELGYRO - Accelerometer/Gyroscope**{#accelgyro}

    A combination [accelerometer](#accel) and [gyroscope](#gyro) sensor that
    provides more precise orientation information by measuring both linear and
    rotational motion.

*   **ADC - Analog to Digital Converter**{#adc}

    A sensor that converts an analog voltage to a digital reading.

*   **ALS - Ambient Light Sensor**{#als}

    A sensor that measures the ambient light present. Used to automatically
    control the screen and keyboard backlight level.

*   **AP - Application Processor**{#ap}

    The processor on the board that boots and runs ChromeOS.

*   **BAR - Barometer**{#bar}

    A sensor that measures atmospheric pressure.

*   **BC12 - Battery Charging**{#bc12}

    A device that implements the USB Battery Charging specification, version
    1.2. The complete [BC 1.2 Specification] is available from the USB
    Implementers Forum.

*   **CBI - CROS Board Information**{#cbi}

    A collection of properties describing the board. This includes board
    version, SKU, model name, and other fields. More details are found in the
    [CrOS Board Info] documentation.

*   **CEC - Consumer Electronics Control**{#cec}

    A one-wire bidirectional bus.  More details are on the [CEC Wikipedia page].

*   **DPTF - Dynamic Power and Thermal Framework (Intel)**{#dptf}

    Intel's platform based power and thermal management. See the [DPTF Readme]
    for details on the implementation used in ChromeOS.

*   **EC - Embedded Controller**{#ec}

    The [MCU](#mcu) used to control the keyboard, battery charging, USB port
    switching, sensor management, and other functions, offloading these tasks
    from the [AP](#ap).

*   **EC-3PO**{#ec-3po}

    A replacement of the current UART-based console which moves much of the code
    off the EC into a host tool, reducing the amount of flash space required.

*   **E-Mark - Electronically Marked Cable** {#emark}

    See the [USB-C documentation](./usb-c.md#emark) for more details.

*   **eSPI - Enhanced Serial Peripheral Interface (Intel)**{#espi}

    Intel's synchronous communication interface between the [AP](#ap) and the
    [EC](#ec). Supports quad I/O mode and clock speeds up to 66 Mhz, providing
    bandwidth up to 264 Mbps. The full [eSPI Specification] is available from
    Intel.

*   **FAFT - Fully Automated Firmware Tests**{#faft}

    A collection of tests and related infrastructure that exercise and verify
    capabilities of Chrome OS. See the [FAFT design doc] and [chromium.org
    documentation](https://www.chromium.org/for-testers/faft) for more details.
    Replaced [SAFT](#saft).

*   **GMR - Giant Magnetoresistance Sensor** {#gmr}

    A sensor device that detects a magnetic field. These sensors differ from
    [MAG](#mag) sensors, in that they only detect magnetic fields in close
    proximity to the sensor. On Chromebooks, GMR sensors are used to detect when
    the lid is opened.  On convertible Chromebooks, the GMR sensor also detects
    tablet mode when lid the is opened a full 360 degrees.

*   **GPIO - General Purpose Input/Output**{#gpio}

    An individual signal that can independently controlled and read.  GPIOs are
    used to enable/disable power rails, drive reset signals, and receive
    interrupts from devices connected to the EC.  GPIOs may also be connected
    to [I/O expanders](#ioexpander).

*   **GYRO - Gyroscope**{#gyro}

    A sensor that measures angular momentum, providing information about
    rotational motion of the device.

*   **I/O Expander**{#ioexpander}

    An [I2C](#i2c) peripheral device that provides additional GPIO signals
    (anywhere from 8 - 32 signals).  GPIOs behind an I/O expander are written
    and read using I2C register accesses from the I2C controller in the EC.

*   **I2C - Inter-Integrated Circuit**{#i2c}

    A 2-wire synchronous communication bus, consisting of a clock signal and a
    bidirectional data signal. An I2C bus typically contains one controller
    device and one or more peripheral devices. The I2C standard defines
    supported clock speeds of 100 KHz and 400 KHz. The full [I2C Specification]
    is available from NXP (formerly Phillips).

*   **LED - Light Emitting Diode**{#led}

    A Light Emitting Diode is a semiconductor that emits light when current
    flows through it.

*   **LPC - [Low Pin Count bus]**{#lpc}

    Legacy communication bus between the [AP](#ap) and [EC](#ec). Runs at 33
    MHz, providing a 133 Mbps bandwidth connection.  Replaced by the
    [eSPI](#espi) interface.

*   **MAG - Magnetometer**{#mag}

    A digital compass sensor, providing orientation for navigation.

*   **MCU - Microcontroller Unit**{#mcu}

    A small integrated chip containing a CPU core, on-chip ROM, on-chip RAM.
    Also contains multiple peripheral interfaces, including GPIO, I2C buses, SPI
    buses, ADC, PWM, etc.

*   **MKBP - Matrix Keyboard Protocol**{#mkbp}

    Message based protocol for communicating asynchronous events from the
    [EC](#ec) to the [AP](#ap). Events are not limited to keyboard events with
    the sensor subsystem as one of the main users. An EC board implementation
    can be configured to send keyboard events through MKBP or using the [8042
    interface](#8042). This is the [EC MKBP driver] implementation.

*   **MST - Multi Stream Transport**{#mst}

    Part of the Display Port 1.2 standard, used to drive multiple independent
    video streams from a single display port. The EC code is typically
    responsible for enabling and disabling the MST hub chipset.

*   **OOBM - Out of Band Management**{#oobm}

    A command in the [EC-3PO protocol](#ec-3po) that allows commands to be
    entered to alter the behaviour of the console and interpreter during
    runtime.

*   **PD - USB Power Delivery**{#pd}

    See the [USB-C documentation](./usb-c.md#pd) for more details.

*   **PMIC - Power Management IC**{#pmic}

    An integrated circuit used to turn power rails on and off.

*   **PPC - USB Power Path Controller**{#ppc}

    See the [USB-C documentation](./usb-c.md#ppc) for more details.

*   **PWM - Pulse Width Modulation**{#pwm}

    Method of varying the duty cycle of a signal to control another device. A
    typical application is to control fan speeds or the brightness of a
    backlight.

*   **SAFT - Semi-Automated Firmware Tests**{#saft}

    A suite of tests for firmware, succeeded by [FAFT](#faft). See the
    [chromium.org documentation](https://www.chromium.org/for-testers/saft) for
    more details.

*   **SPI - Serial Peripheral Interconnect**{#spi}

    A 4-wire synchronous communication bus consisting of the signals CLK
    (clock), SDO (Serial Data Out), SDI (Serial Data In), and CS (chip-select,
    one per SPI peripheral).  The SDO and SDI pins are defined from the
    perspective of the device: the SPI controller's SDO pin connects to the SPI
    peripheral's SDI pin and vice-versa. Clock speeds over 100 MHz are
    supported. SPI communication involves the following sequence:

    * SPI controller asserts CS.
    * SPI controller transmits one or bytes on its SDO signal, received by the
      SPI peripheral on its SDI signal.
    * SPI peripheral transmits zero or more bytes on its SDO signal, received
      by the SPI controller on its SDI signal.
    * SPI controller de-asserts CS.

    The specific contents of a SPI frame varies based on the SPI peripheral
    type.

*   **SVDM - Structured Vendor Defined Messages**{#svdm}

    See the [USB-C documentation](./usb-c.md#svdm) for more details.

*   **TCPC - USB Type-C Port Controller**{#tcpc}

    See the [USB-C documentation](./usb-c.md#tcpc) for more details.

*   **UART - Universal Asynchronous Receiver Transceiver**{#uart}

    Also known as a serial port.  An asynchronous communication channel between
    two devices with a dedicated receive pin, transmit pin, and ground. Optional
    hardware flow control signals require additional connections between the
    devices. Standard transmission rates are slow (up to 115200 bits per
    second). Typical use is to provide a debug console to the EC. [RS-232] is
    the protocol standard used by UARTs.

*   **VCONN - Connector Voltage** {#vconn}

    See the [USB-C documentation](./usb-c.md#vconn) for more details.


[BC 1.2 Specification]: <https://www.usb.org/document-library/battery-charging-v12-spec-and-adopters-agreement>
[CrOS Board Info]: <https://chromium.googlesource.com/chromiumos/docs/+/master/design_docs/cros_board_info.md>
[CEC Wikipedia page]: <https://en.wikipedia.org/wiki/Consumer_Electronics_Control>
[DPTF Readme]: <https://github.com/intel/dptf/blob/master/README.txt>
[eSPI Specification]: <https://www.intel.com/content/dam/support/us/en/documents/software/chipset-software/327432-004_espi_base_specification_rev1.0.pdf>
[FAFT design doc]: <https://chromium.googlesource.com/chromiumos/third_party/autotest/+/refs/heads/master/docs/faft-design-doc.md>
[I2C Specification]: <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>
[RS-232]: <https://en.wikipedia.org/wiki/RS-232>
[EC MKBP driver]: <https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/common/keyboard_mkbp.c>
[Low Pin Count bus]: https://en.wikipedia.org/wiki/Low_Pin_Count
