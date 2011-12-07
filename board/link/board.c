/* EC for Link mainboard pins multiplexing */

#include "board.h"
#include "registers.h"
#include "util.h"

void configure_board(void)
{
	/* Enable all of the GPIO modules : GPIOA to GPIOQ */
	LM4_SYSTEM_RCGCGPIO = 0x7fff;

	/* GPIOA muxing :
	 * pins 0/1 : UART0 = function 1
	 */
	LM4_GPIO_PCTL(A)  = 0x00000011;
	LM4_GPIO_AFSEL(A) = 0x03;
	LM4_GPIO_DEN(A)   = 0x03;
}
