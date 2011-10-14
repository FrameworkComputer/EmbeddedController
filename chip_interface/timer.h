/* timer.h - periodical timer
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_TIMER_H
#define __CHIP_INTERFACE_TIMER_H

/* init hardware and prepare ISR */
EcError CrPeriodicalTimerInit(void);

EcError CrPeriodicalTimerRegister(
    int interval  /* ms */,
    int (*timer)(int /* delta ms from last call */));


#endif  /* __CHIP_INTERFACE_TIMER_H */
