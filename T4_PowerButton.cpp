/* This library code is placed under the MIT license
 * Copyright (c) 2020 Frank Bösing
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "T4_PowerButton.h"

static void (*__user_power_button_callback)(void);
static callback_ex_action (*__user_power_button_callback_ex)(void);

FLASHMEM __attribute__((noreturn))
void arm_power_down() {
  SNVS_LPCR |= SNVS_LPCR_TOP; //Switch off now
  asm volatile ("dsb");
  while (1) asm ("wfi");
}

/*
 * To be used combined with callback_ex_action_poweroff_keeparmed
 * If this function is not called, the normal program will not
 * continue. IntervalTimers, however, will. See the example
 */
FLASHMEM
void rearm_power_button_callback(void)
{
  if(__user_power_button_callback != nullptr || __user_power_button_callback_ex != nullptr)
    SNVS_LPSR |= (1 << 18) | (1 << 17);
}


// I think bit 18 should be checked instead bit 17
bool arm_power_button_pressed(void) {
  return (SNVS_LPSR >> 18) & 0x01;
}

/*
 * Checks whether one or both possible callbacks has been installed.
 * Each installed callback will be called.
 * The possible result of __user_power_button_callback_ex will be
 * evaluated and the action will be accordingly
 */
FLASHMEM
void __int_power_button(void) {
  if (SNVS_HPSR & 0x40) {
    SNVS_HPCOMR |= (1 << 31) ;//| (1 << 4);
    // This would prevent the callback from being called again
    // while the poweroff line is low. We will postpone this decision
    // till after the callbacks are called;
    // SNVS_LPSR |= (1 << 18) | (1 << 17);

    if (__user_power_button_callback != nullptr) __user_power_button_callback();

    callback_ex_action _action = __user_power_button_callback_ex == nullptr ? callback_ex_action_poweroff : callback_ex_action_poweroff_cancel;
    if (__user_power_button_callback_ex != nullptr)
        _action = __user_power_button_callback_ex();

    if(_action == callback_ex_action_poweroff) {
      SNVS_LPSR |= (1 << 18) | (1 << 17);
      __disable_irq();
      NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
      arm_power_down();
    } else {
        if(_action == callback_ex_action_poweroff_cancel)
          SNVS_LPSR |= (1 << 18) | (1 << 17);
        // Else keeparmed
    }
  }
}

/*
 * This installs a callback that does not return a value.
 * When there is no callback installed using
 * set_arm_power_button_callback_ex, a poweroff will be
 * performed after the callback has returned (once the poweroff line has been brought low)
 */
FLASHMEM
void set_arm_power_button_callback(void (*fun_ptr)(void)) {
  SNVS_LPSR |= (1 << 18) | (1 << 17);
  __user_power_button_callback = fun_ptr;
  if (fun_ptr != nullptr) {
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    attachInterruptVector(IRQ_SNVS_ONOFF, &__int_power_button);
    NVIC_SET_PRIORITY(IRQ_SNVS_ONOFF, 255); //lowest priority
    asm volatile ("dsb"); //make sure to write before interrupt-enable
    NVIC_ENABLE_IRQ(IRQ_SNVS_ONOFF);
  } else {
    if (__user_power_button_callback_ex == nullptr)
      NVIC_DISABLE_IRQ(IRQ_SNVS_ONOFF);
  }
  asm volatile ("dsb":::"memory");
}

/*
 * This installs a callback that returns an int.
 * The return value (enum callback_ex_action) of the callback will
 * determine, whether a poweroff is performed,
 * or the poweroff is canceled or the callback function
 * should be called back, while the poweroff line is low.
 */
FLASHMEM
void set_arm_power_button_callback_ex(callback_ex_action (*fun_ptr)(void)) {
  SNVS_LPSR |= (1 << 18) | (1 << 17);
  __user_power_button_callback_ex = fun_ptr;
  if (fun_ptr != nullptr) {
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    attachInterruptVector(IRQ_SNVS_ONOFF, &__int_power_button);
    NVIC_SET_PRIORITY(IRQ_SNVS_ONOFF, 255); //lowest priority
    asm volatile ("dsb"); //make sure to write before interrupt-enable
    NVIC_ENABLE_IRQ(IRQ_SNVS_ONOFF);
  } else {
    if (__user_power_button_callback == nullptr)
      NVIC_DISABLE_IRQ(IRQ_SNVS_ONOFF);
  }
  asm volatile ("dsb":::"memory");
}

void set_arm_power_button_callback(void (*fun_ptr)(void));
void set_arm_power_button_debounce(arm_power_button_debounce debounce) { SNVS_LPCR = (SNVS_LPCR & ~(3 << 18)) | (debounce << 18); }
void set_arm_power_button_press_time_emergency(arm_power_button_press_time_emergency emg) { SNVS_LPCR = (SNVS_LPCR & ~(3 << 16)) | (emg << 16); }
void set_arm_power_button_press_on_time(arm_power_button_press_on_time ontime) { SNVS_LPCR = (SNVS_LPCR & ~(3 << 20)) | (ontime << 20); }
void arm_enable_nvram(void) { SNVS_LPCR |= (1 << 24); }

FLASHMEM __attribute__((noreturn))
void arm_reset(void) {
#if TEENSYDUINO < 150
  IOMUXC_GPR_GPR16 = 0x00200007;
  asm volatile ("dsb":::"memory");
#endif
  SCB_AIRCR = 0x05FA0004;
  while (1) asm ("wfi");
}
