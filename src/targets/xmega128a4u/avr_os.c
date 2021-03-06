/*
    avr_os.c

    This is part of OsEID (Open source Electronic ID)

    Copyright (C) 2015-2019 Peter Popovec, popovec.peter@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    CPU initialization and fuses setting for xmega128a4u
    Special: this part of code is responsible for restart of main
    (main must be restarted if card is powered down or on USB suspend etc.)

*/
#include <avr/io.h>
#include <avr/fuse.h>
#include <avr/lock.h>
#include <avr/interrupt.h>
#include "usb.h"
#include "avr_os.h"

#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                                          + __GNUC_PATCHLEVEL__)
/* Test for GCC version */
#if GCC_VERSION == 40902
#define X_GCC_OK
#endif
#if GCC_VERSION == 50400
#define X_GCC_OK
#endif

#ifndef X_GCC_OK
#error only AVR GCC version 4.8.1 / 4.9.2 / 5.4.0 are tested to compile this code
#endif


void init_cpu (void) __attribute__ ((naked))
  __attribute__ ((section (".init1")));
void
init_cpu (void)
{
  cli ();
  // use PLL - multiply 2MHz RC oscilator to 32MHz
  OSC.PLLCTRL = OSC_PLLSRC_RC2M_gc | (32 / 2);
  OSC.CTRL |= OSC_PLLEN_bm;
  // wait to PLL ready
  while (!(OSC.STATUS & OSC_PLLRDY_bm));
// switch CPU core clock to run from PLL
  {
    asm volatile (		//
		   "ldi	r25,4\n"	//      value (clock source for CPU core..)
		   "ldi	r24,0xd8\n"	//      key
		   "ldi r30,0x40\n"	//      0x0040 = CTRL reg address
		   "ldi	r31,0\n"	//
		   "out 0x3b,r1\n"	//      clear RAMPZ
		   "out 0x34,r24\n"	//      write key to CCP
		   "st  Z,r25\n"	//      write value
		   "ldi r24,0x40\n"	//      delay aproximatly 1ms
		   "ldi r25,0x1f\n"	//
		   "sbiw r24,1\n"	//
		   "brne .-4\n"	//
		   :::);
  }
}


void init_usb (void) __attribute__ ((naked))
  __attribute__ ((section (".init7")));
void
init_usb (void)
{
  // Internal 32MHz oscilator is used for USB.. check USB_Init

  // Interrupt controller - enable LOW, MEDIUM and HIGH level irq
  //PMIC.CTRL = PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
  // Interrupt controller - enable  HIGH level irq, move vector to bootloader
  // PMIC_IVSEL_bm is protected by CCP ..  enable access first
  CCP = CCP_IOREG_gc;
  PMIC.CTRL = PMIC_IVSEL_bm;
  PMIC.CTRL =
    PMIC_IVSEL_bm | PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
  //PMIC.CTRL = PMIC_HILVLEN_bm | PMIC_IVSEL_bm;

  sei ();
  USB_Init ();
}

// this code can be used to restart main (or sleep, then restart main)

uint8_t restart_state[2] __attribute__ ((section (".noinit")));

void restart_main (void) __attribute__ ((naked))
  __attribute__ ((section (".init8")));
void
restart_main (void)
{

// save CPU state
  asm volatile (		//
		 "ldi	r30,lo8(%[r_state])\n"	//
		 "ldi	r31,hi8(%[r_state])\n"	//
		 "cli\n"	//
		 "in	r24,0x3d\n"	//
		 "st	Z+,r24\n"	//
		 "in	r24,0x3e\n"	//
		 "st	Z+,r24\n"	//
		 ::[r_state] "m" (restart_state));

// hooks restart main
  asm volatile (		//
		 "rjmp no_sleep\n"	//
		 "do_sleep:\n"	//
		 "cli\n"	//
		 "ldi	r30,lo8(%[sleep_reg])\n"	//
		 "ldi	r31,hi8(%[sleep_reg])\n"	//
		 "ldi	r24,5\n"	// sleep mode PDOWN, enable sleep
		 "st	Z,r24\n"	//
		 "ldi	r24,0\n"	// disable sleep
		 "sei\n"	//
		 "sleep\n"	//
		 "st	Z,r24\n"	//
		 "no_sleep:\n"	//
		 //                   ::[sleep_reg] "m" (SLEEP));        // SLEEP = SLEEP_CTRL
		 ::[sleep_reg] "m" (SLEEP_CTRL)	//
		 :);
// reintialize USB clock/calibration
  USB_Reinit ();
// restore SP for main ..
  asm volatile (		//
		 "ldi	r30,lo8(%[r_state])\n"	//
		 "ldi	r31,hi8(%[r_state])\n"	//
		 "cli\n"	//
		 "ld	r24,Z+\n"	//
		 "out	0x3d,r24\n"	//
		 "ld	r24,Z+\n"	//
		 "out	0x3e,r24\n"	//
		 ::[r_state] "m" (restart_state));
// reinitialize SREG, and RAMP registers

  asm volatile (		//
		 "clr	r1\n"	//
		 "out	0x3f,r1\n"	//
		 "out	%[rampd],r1\n"	//
		 "out	%[rampx],r1\n"	//
		 "out	%[rampy],r1\n"	//
		 "out	%[rampz],r1\n"	//
		 "sei\n"	//
		 ::		//
		 [rampd] "I" (_SFR_IO_ADDR (RAMPD)),	//
		 [rampx] "I" (_SFR_IO_ADDR (RAMPX)),	//
		 [rampy] "I" (_SFR_IO_ADDR (RAMPY)),	//
		 [rampz] "I" (_SFR_IO_ADDR (RAMPZ))	//
		 :);
}

// Next functions force ISR PORTA_INT0_vect (or PORTA_INT1_vect) imedietly
// after any high/midle lewel interrupts are completted.  ISR then owerwrite
// return address on stack and does restart of main from specific address
// (do_sleep or no_sleep)

void
CPU_do_sleep ()
{
  cli ();
//pin4 (normaly connected to ground)
  PORTA.DIRCLR = PIN4_bm;	// INPUT
  PORTA.PIN4CTRL = 3;		// interrupt on LEVEL
  PORTA.INTCTRL = 1;		// low level int
  PORTA.INT0MASK = PIN4_bm;	// pin4
  sei ();
}

//
void
CPU_do_restart_main ()
{
  cli ();
//pin4 (normaly connected to ground)
  PORTA.DIRCLR = PIN4_bm;	// INPUT
  PORTA.PIN4CTRL = 3;		// interrupt on LEVEL
  PORTA.INTCTRL = 4;		// low level int
  PORTA.INT1MASK = PIN4_bm;	// pin4
  sei ();
}

ISR (PORTA_INT0_vect, ISR_NAKED)
{
  // no more interupts
  cli ();
  PORTA.INT0MASK = 0;
  PORTA.INTCTRL = 0;
  PORTA.INTFLAGS = 3;		// clear flags

  asm volatile (		//
		 "pop	r24\n"	//
		 "pop	r24\n"	//
		 "pop	r24\n"	//
		 "ldi       r24,pm_lo8(do_sleep)\n"	//
		 "push      r24\n"	//
		 "ldi       r24,pm_hi8(do_sleep)\n"	//
		 "push      r24\n"	//
		 "ldi       r24,pm_hh8(do_sleep)\n"	//
		 "push      r24\n"	//
		 "sei\n"	//
		 "reti\n"	//
		 ::);
}

ISR (PORTA_INT1_vect, ISR_NAKED)
{
  // no more interupts
  cli ();
  PORTA.INT1MASK = 0;
  PORTA.INTCTRL = 0;
  PORTA.INTFLAGS = 3;		// clear flags

  asm volatile (		//
		 "pop	r24\n"	//
		 "pop	r24\n"	//
		 "pop	r24\n"	//
		 "ldi       r24,pm_lo8(no_sleep)\n"	//
		 "push      r24\n"	//
		 "ldi       r24,pm_hi8(no_sleep)\n"	//
		 "push      r24\n"	//
		 "ldi       r24,pm_hh8(no_sleep)\n"	//
		 "push      r24\n"	//
		 "sei\n"	//
		 "reti\n"	//
		 ::);
}

void
CPU_idle (void)
{
  asm volatile (		//
		 "cli\n"	//
		 "ldi	r30,lo8(%[sleep_reg])\n"	//
		 "ldi	r31,hi8(%[sleep_reg])\n"	//
		 "ldi	r24,1\n"	// sleep mode IDLE, enable sleep
		 "st	Z,r24\n"	//
		 "ldi	r24,0\n"	// disable sleep
		 "sei\n"	//
		 "sleep\n"	//
		 "st	Z,r24\n"	//
		 //                   ::[sleep_reg] "m" (SLEEP));        // SLEEP = SLEEP_CTRL
		 ::[sleep_reg] "m" (SLEEP_CTRL)	//
		 :);
}

FUSES =
{
//  .FUSEBYTE1 = 0,     // watchdog - default value
  .FUSEBYTE2 = 0xBF,		// start from bootloader section
//  .FUSEBYTE4 = ,      //
//  .FUSEBYTE5 = ,      //
};


// disable read/write by external programming interface & 0xfc
// disable read/write bootloader section  from application section  &0x3f
// bootloader can access application +application table section
LOCKBITS = (0x3c);
