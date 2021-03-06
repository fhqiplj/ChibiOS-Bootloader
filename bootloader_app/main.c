/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <stdio.h>
#include <string.h>

#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "common.h"
#include "usb_config.h"
#include "stm32f30x_iwdg.h"
#include "comm.h"

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

uint8_t reset_flags = 0;

/*===========================================================================*/
/* Generic code.                                                             */
/*===========================================================================*/

/*
 * Red LED blinker thread, times are in milliseconds.
 */
static WORKING_AREA(waThread1, 128);
static msg_t Thread1(void *arg) {

  (void)arg;
  chRegSetThreadName("blinker");
  while (TRUE) {
    systime_t time = USBD1.state == USB_ACTIVE ? 250 : 500;
    palClearPad(GPIOE, GPIOE_LED3_RED);
    chThdSleepMilliseconds(time);
    palSetPad(GPIOE, GPIOE_LED3_RED);
    chThdSleepMilliseconds(time);
  }
}

/*
 * USB Bulk thread, times are in milliseconds.
 */
static WORKING_AREA(waThread2, 8192);
static msg_t Thread2(void *arg) {

  uint8_t clear_buff[64];
  EventListener el1;
  flagsmask_t flags;
  (void)arg;
  chRegSetThreadName("USB");

  /*
   * Activates the USB driver and then the USB bus pull-up on D+.
   * Note, a delay is inserted in order to not have to disconnect the cable
   * after a reset.
   */
  usbDisconnectBus(serusbcfg.usbp);
  //chThdSleepMilliseconds(1500);
  usbStart(&USBD1, &usbcfg);
  usbConnectBus(serusbcfg.usbp);

  /*
   * Initializes a Bulk USB driver.
   */
  bduObjectInit(&BDU1);
  bduStart(&BDU1, &bulkusbcfg);

  chEvtRegisterMask(chnGetEventSource(&BDU1), &el1, ALL_EVENTS);

  while(USBD1.state != USB_READY) chThdSleepMilliseconds(10);
  while(BDU1.state != BDU_READY) chThdSleepMilliseconds(10);

  while (TRUE) {

    //chThdSleepMilliseconds(1);
    palClearPad(GPIOE, GPIOE_LED10_RED);

    chEvtWaitAny(ALL_EVENTS);
    chSysLock();
    flags = chEvtGetAndClearFlagsI(&el1);
    chSysUnlock();

    //if (chIQGetFullI(&BDU1.iqueue) > 4) {

    if (flags & CHN_INPUT_AVAILABLE) {

      palSetPad(GPIOE, GPIOE_LED10_RED);

      uint8_t cmd_res = read_cmd((BaseChannel *)&BDU1, reset_flags);

      chnReadTimeout((BaseChannel *)&BDU1, clear_buff, 64, MS2ST(25) );

      if (cmd_res == 0)
      {
        palTogglePad(GPIOE, GPIOE_LED4_BLUE);
        continue;
      }
      else {
        palTogglePad(GPIOE, GPIOE_LED8_ORANGE);
      }

    }
  }
}

/*
 * Application entry point.
 */
int main(void) {

  /*!< Independent Watchdog reset flag */
  if (RCC->CSR & RCC_CSR_IWDGRSTF) {
    /* User App did not start properly */

    reset_flags |= FLAG_IWDRST;
  }

  /*!< Software Reset flag */
  else if (RCC->CSR & RCC_CSR_SFTRSTF) {
    /* Bootloader called by user app */

    reset_flags |= FLAG_SFTRST;
  }

  else if (checkUserCode(USER_APP_ADDR) == 1) {

      /* Setup IWDG in case the target application does not load */

      const uint32_t LsiFreq = 42000;
      IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);

      IWDG_SetPrescaler(IWDG_Prescaler_32);

      IWDG_SetReload(LsiFreq/128);

      IWDG_Enable();

      jumpToUser(USER_APP_ADDR);
  }

  /*!< Remove reset flags */
  RCC->CSR |= RCC_CSR_RMVF;
  reset_flags = FLAG_OK;

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  /*
   * Creates the blinker and bulk threads.
   */
  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);
  chThdCreateStatic(waThread2, sizeof(waThread2), NORMALPRIO+1, Thread2, NULL);

  /*
   * Normal main() thread activity, in this demo it does nothing except
   * sleeping in a loop and check the button state.
   */
  while (TRUE) {
    chThdSleepMilliseconds(1000);
  }
}
