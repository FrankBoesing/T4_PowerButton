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

#if !defined(T4PowerButton)
#define T4PowerButton

#include "IMXRT.h" 

static void (*__user_power_button_callback)(void);

void __int_power_button(void)  {
  if (SNVS_HPSR & 0x40) {
    SNVS_HPCOMR |= (1 << 31) | (1 << 4);
    SNVS_LPSR |= (1 << 18);
    if (__user_power_button_callback != nullptr) __user_power_button_callback();
    __disable_irq();
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    SNVS_LPCR |= SNVS_LPCR_TOP; //Switch off now
    asm volatile ("dsb");
    while (1) asm ("wfi");
  }
}

void set_arm_power_button_callback(void (*fun_ptr)(void)) {
  SNVS_HPCOMR |= (1 << 31) | (1 << 4);
  SNVS_LPSR |= (1 << 18);
  __user_power_button_callback = fun_ptr;
  if (fun_ptr != nullptr) {
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF) ;
    _VectorsRam[IRQ_SNVS_ONOFF + 16] = &__int_power_button; //set interrupt vector
    asm volatile ("dsb"); //make sure to write before interrupt-enable
    NVIC_SET_PRIORITY(IRQ_SNVS_ONOFF, 255); //lowest priority
    NVIC_ENABLE_IRQ(IRQ_SNVS_ONOFF); //enable interrupt
  } else {
    NVIC_DISABLE_IRQ(IRQ_SNVS_ONOFF);
  }
  asm volatile ("dsb");

}

#endif
