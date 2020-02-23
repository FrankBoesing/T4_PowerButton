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

#include "T4_PowerButton.h"
#include "Arduino.h"


#if defined(ARDUINO_TEENSY40)
  static const unsigned DTCM_START = 0x20000000UL;
  static const unsigned OCRAM_START = 0x20200000UL;
  static const unsigned OCRAM_SIZE = 512;
  static const unsigned FLASH_SIZE = 1984;
#elif defined(ARDUINO_TEENSY41)
  static const unsigned DTCM_START = 0x20000000UL;
  static const unsigned OCRAM_START = 0x20200000UL;
  static const unsigned OCRAM_SIZE = 512;
  static const unsigned FLASH_SIZE = 7936;
#endif

static void (*__user_power_button_callback)(void);

FLASHMEM __attribute__((noreturn))
void arm_power_down() {
    SNVS_LPCR |= SNVS_LPCR_TOP; //Switch off now
    asm volatile ("dsb");
    while (1) asm ("wfi");
}

bool arm_power_button_pressed(void) {
  return (SNVS_LPSR >> 17) & 0x01;
}

FLASHMEM
void __int_power_button(void) {
  if (SNVS_HPSR & 0x40) {
    SNVS_HPCOMR |= (1 << 31)/* | (1 << 4)*/;
    SNVS_LPSR |= (1 << 18) | (1 << 17);
    if (__user_power_button_callback != nullptr) __user_power_button_callback();
    __disable_irq();
    NVIC_CLEAR_PENDING(IRQ_SNVS_ONOFF);
    arm_power_down();
  }
}

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
    NVIC_DISABLE_IRQ(IRQ_SNVS_ONOFF);
  }
  asm volatile ("dsb":::"memory");
}

FLASHMEM
void arm_reset(void) {
#if TEENSYDUINO < 150
	IOMUXC_GPR_GPR16 = 0x00200007;
	asm volatile ("dsb":::"memory");
#endif
	SCB_AIRCR = 0x05FA0004;
	while (1) asm ("wfi");
}

unsigned memfree(void) {
  extern unsigned long _ebss;
  extern unsigned long _sdata;
  extern unsigned long _estack;
  const unsigned DTCM_START = 0x20000000UL;
  unsigned dtcm = (unsigned)&_estack - DTCM_START;
  unsigned stackinuse = (unsigned) &_estack -  (unsigned) __builtin_frame_address(0);
  unsigned varsinuse = (unsigned)&_ebss - (unsigned)&_sdata;
  unsigned freemem = dtcm - (stackinuse + varsinuse);
  return freemem;
}

unsigned heapfree(void) {
// https://forum.pjrc.com/threads/33443-How-to-display-free-ram?p=99128&viewfull=1#post99128
  void* hTop = malloc(1);// current position of heap.
  unsigned heapTop = (unsigned) hTop;
  free(hTop);
  unsigned freeheap = (OCRAM_START + (OCRAM_SIZE * 1024)) - heapTop;
  return freeheap;
}

extern "C" {
void startup_late_hook(void) {
  extern unsigned long _ebss;
  unsigned long * p =  &_ebss;
  size_t size = (size_t)(uint8_t*)__builtin_frame_address(0) - 16 - (uintptr_t) &_ebss;
  memset((void*)p, 0, size);
}
}

unsigned long maxstack(void) {
  extern unsigned long _ebss;
  extern unsigned long _estack;
  unsigned long * p =  &_ebss;
  while (*p == 0) p++;
  return (unsigned) &_estack - (unsigned) p;
}

FLASHMEM
void progInfo(void) {
  Serial.println(__FILE__ " " __DATE__ " " __TIME__ );
  Serial.print("Teensyduino version ");
  Serial.println(TEENSYDUINO / 100.0f);
  Serial.println();
}

FLASHMEM
void flexRamInfo(void) {

  int itcm = 0;
  int dtcm = 0;
  int ocram = 0;
  uint32_t gpr17 = IOMUXC_GPR_GPR17;

  char __attribute__((unused)) dispstr[17] = {0};
  dispstr[16] = 0;

  for (int i = 15; i >= 0; i--) {
    switch ((gpr17 >> (i * 2)) & 0b11) {
      default: dispstr[15 - i] = '.'; break;
      case 0b01: dispstr[15 - i] = 'O'; ocram++; break;
      case 0b10: dispstr[15 - i] = 'D'; dtcm++; break;
      case 0b11: dispstr[15 - i] = 'I'; itcm++; break;
    }
  }

  const char* fmtstr = "%-6s%7d %5.02f%% of %4dkB (%7d Bytes free) %s\n";

  extern unsigned long _stext; //attention != 0! becaus of alignment for null pointer deref - ITCM starts at 0
  extern unsigned long _etext;
  extern unsigned long _sdata;
  extern unsigned long _ebss;
  extern unsigned long _flashimagelen;
  extern unsigned long _heap_start;
  extern unsigned long _estack;

  Serial.printf(fmtstr, "FLASH:",
                (unsigned)&_flashimagelen,
                ((unsigned)&_flashimagelen) / (FLASH_SIZE * 1024.0f) * 100.0f,
                FLASH_SIZE,
                FLASH_SIZE * 1024 - ((unsigned)&_flashimagelen), "FLASHMEM, PROGMEM");

  Serial.printf(fmtstr, "ITCM:",
                (unsigned)&_etext /* - (unsigned)&_stext*/,
                (float)((unsigned)&_etext /* - (unsigned)&_stext*/) / ((float)itcm * 32768.0f) * 100.0f,
                itcm * 32,
                itcm * 32768 - ((unsigned)&_etext /* - (unsigned)&_stext*/), "(RAM1) FASTRUN");

  void* hTop = malloc(1);// current position of heap.
  unsigned heapTop = (unsigned) hTop;
  free(hTop);
  unsigned freeheap = (OCRAM_START + (OCRAM_SIZE * 1024)) - heapTop;

  Serial.printf("OCRAM:\n  %7d Bytes (%d kB)\n", OCRAM_SIZE * 1024, OCRAM_SIZE);
  Serial.printf("- %7d Bytes (%d kB) DMAMEM\n", ((unsigned)&_heap_start - OCRAM_START), ((unsigned)&_heap_start - OCRAM_START) / 1024);
  Serial.printf("- %7d Bytes (%d kB) Heap\n", (heapTop - (unsigned)&_heap_start ), (heapTop - (unsigned)&_heap_start ) / 1024);
  Serial.printf("  %7d Bytes heap free (%d kB), %d Bytes OCRAM in use (%d kB).\n",
                freeheap, freeheap / 1024,
                heapTop - OCRAM_START, (heapTop - OCRAM_START) / 1024);

  unsigned _dtcm = (unsigned)&_estack - DTCM_START; //or, one could use dtcm * 32768 here.
  //unsigned stackinuse = (unsigned) &_estack -  (unsigned) __builtin_frame_address(0);
  unsigned stackinuse = maxstack();
  unsigned varsinuse = (unsigned)&_ebss - (unsigned)&_sdata;
  //unsigned freemem = _dtcm - stackinuse - varsinuse;
  Serial.printf("DTCM:\n  %7d Bytes (%d kB)\n", _dtcm, _dtcm / 1024);
  Serial.printf("- %7d Bytes (%d kB) global variables\n", varsinuse, varsinuse / 1024);
  Serial.printf("- %7d Bytes (%d kB) max. stack so far\n", stackinuse, stackinuse / 1024);
  Serial.println("=========");
  Serial.printf("  %7d Bytes free (%d kB), %d Bytes in use (%d kB).\n",
                _dtcm - (varsinuse + stackinuse), (_dtcm - (varsinuse + stackinuse)) / 1024,
                varsinuse + stackinuse, (varsinuse + stackinuse) / 1024
               );
}

#endif
