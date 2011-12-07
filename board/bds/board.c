/* Stellaris EKB-LM4F-EAC pins multiplexing */

#include "board.h"
#include "registers.h"
#include "util.h"

void configure_board(void)
{
	/* Enable all of the GPIO modules : GPIOA to GPIOQ */
	LM4_SYSTEM_RCGCGPIO = 0x7fff;

	/* GPIOA muxing :
	 * pins 0/1 : UART0 = function 1
	 * pins 2/3/4/5 : SSI0 = function 2
	 * pin 6 : GPIO = function 0 (SD card CS -- open drain)
	 * pin 7 : GPIO = function 0 (user LED)
	 */
	LM4_GPIO_PCTL(A)  = 0x00222211;
	LM4_GPIO_AFSEL(A) = 0x3f;
	LM4_GPIO_DEN(A)   = 0xff;
	LM4_GPIO_PDR(A)   = 0x00;
	LM4_GPIO_PUR(A)   = 0x3c;
	LM4_GPIO_DIR(A)   = 0xc0;
	LM4_GPIO_ODR(A)   = 0x40;
	LM4_GPIO_DR2R(A)  = 0xc3;
	LM4_GPIO_DR8R(A)  = 0x3c;
	LM4_GPIO_DATA_BITS(A, 0x100) = 0x40;
	LM4_GPIO_DATA_BITS(A, 0x200) = 0;

	/* GPIOB muxing
	 * pin 0 : GPIO = function 0 (USB ID)
	 * pin 1 : USB digital (VBus sense)
	 */
	LM4_GPIO_DEN(B)  |= 0x01;
	LM4_GPIO_AFSEL(B)|= 0x01;

	/* GPIOC muxing
	 * pins 0/1/2/3 : JTAG (default config)
	 * pin 4 : GPIO = function 0 (OLED +15v power enable)
	 * pin 6 : USB digital (USB power enable)
	 * pin 7 : USB digital (USB current overflow)
	 */
	LM4_GPIO_PCTL(C)  = 0x88001111;
	LM4_GPIO_AFSEL(C) = 0xcf;
	LM4_GPIO_DEN(C)   = 0xdf;
	LM4_GPIO_DIR(C)   = 0x10;
	LM4_GPIO_DATA_BITS(C, 0x40) = 0;

	/* GPIOD muxing
	 * pins 0/1/2/3/4 : GPIO = function 0 (buttons up,down,left,right,select)
	 * pin 5 : GPIO = function 0 (OLED d/Cn)
	 * pin 6 : GPIO = function 0 (OLED reset)
	 */
	LM4_GPIO_DEN(D)   = 0x7f;
	LM4_GPIO_DIR(D)   = 0x60;
	LM4_GPIO_PUR(D)   = 0x1f;

	/* GPIOE muxing
	 * pin 3 : Analog function : AIN0 ADC (potentiometer)
	 * pin 6/7: USB analog
	 */
	LM4_GPIO_AMSEL(E) = 0x8;
}
