/*
  Hardware
		                       ATtiny85
		                     +----------+
		             RESET - | 1      8 | - VCC      - BAT(+)
		               PB3 - | 2      7 | - PB2/INT0 - LED0(-) 
		LED3(+) - OC1B/PB4 - | 3      6 | - PB1/OC0B - LED2(+)
		 BAT(-) -      GND - | 4      5 | - PB0/OC0A - LED1(+)
		                     +----------+

  Use a common cathode RGB LED.
  Use 3V CR2032 battery.

  Configure low fuse to: 
  
   bit 7: CKDIV8 - 0
   bit 6: CKOUT  - 1
   bit 5: SUT1   - 1 \ slow startup (+64ms)
   bit 4: SUT0   - 0 /
   bit 3: CKSEL3 - 0 \
   bit 2: CKSEL2 - 1 | internal 128KHz 
   bit 1: CKSEL1 - 0 | 
   bit 0: CKSEL0 - 0 /
   ------------------
                0x64

*/

#ifndef __AVR_ATtiny85__
#error "Must be compiled for AVR ATtiny85"
#endif

#define F_CPU 128000L

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>

const uint8_t LED0_BIT = 2;
const uint8_t LED1_BIT = 0;
const uint8_t LED2_BIT = 1;
const uint8_t LED3_BIT = 4;

EMPTY_INTERRUPT(WDT_vect);
EMPTY_INTERRUPT(INT0_vect);

void wdSleepImpl(uint8_t wdtcr) {
	WDTCR |= _BV(WDCE); // enable the WDT Change Bit
	WDTCR = wdtcr;
	wdt_reset(); // start counting with new timeout setting
	WDTCR |= _BV(WDIF); // now reset interrupt flag [again] after all config / timer reset done
	sei();
	sleep_cpu();
	cli();
}

inline __attribute__((always_inline)) void wdSleep(uint8_t wdto) {
	// statically compute WDTCR value to enable WDT Interrupt and set timeout
	// note: bit 3 is separate
	wdSleepImpl(_BV(WDIF) | _BV(WDIE) | (wdto & 7) | (wdto >> 3 << WDP3));
}

bool night() {
	// charge
	PORTB |= _BV(LED0_BIT);
	wdSleep(WDTO_15MS);
	DDRB &= ~_BV(LED0_BIT);
	PORTB &= ~_BV(LED0_BIT);
	// wait discharge
	GIMSK |= _BV(INT0); // enable INT0 (default = when low)
	GIFR |= _BV(INTF0); // reset interrupt flag
	wdSleep(WDTO_250MS);
	bool result = (PINB & _BV(LED0_BIT)) != 0; // night if has not discharged yet
	GIMSK &= ~_BV(INT0); // disable INT0
	// back to output
	DDRB |= _BV(LED0_BIT);
	return result;
}

// XABC fast random generator (with a CAFEBABE seed)
uint8_t x = 0xCA;
uint8_t a = 0XFE;
uint8_t b = 0xBA;
uint8_t c = 0xBE;

// returns random number from 0 to 255
uint8_t random() {
	x++;                 //x is incremented every round and is not affected by any other variable
	a = (a^c^x);         //note the mix of addition and XOR
	b = (b+a);           //And the use of very few instructions
	c = (c+((b>>1)^a));  //the right shift is to ensure that high-order bits from b can affect
	return c;            //low order bits of other variables
}

inline void animateOne() {
	uint8_t p1 = 0;
	uint8_t p2 = 0;
	uint8_t p3 = 0;
	switch (random() & 3) {
		case 0:
			wdSleep(WDTO_1S); // skip 1 second
			return; // nothing else in this cycle
		case 1:
			p1 = 0xff;
			if (random() & 1)
				p2 = random();
			else
				p3 = random();
			break;
		case 2:
			p2 = 0xff;
			if (random() & 1) 
				p1 = random();
			else
				p3 = random();
			break;
		case 3:
			p3 = 0xff;
			if (random() & 1)
				p1 = random();
			else
				p2 = random();
	}
	// power on timers
	PRR &= ~(_BV(PRTIM1) | _BV(PRTIM0));
	// turn on timers
	TCCR0A = _BV(COM0A1) | _BV(COM0B1) | _BV(WGM01) | _BV(WGM01); // PWM 0A, PWM 0B, clear on match, set on top
	TCCR0B = _BV(CS00); // run, no prescaler
	GTCCR = _BV(PWM1B) | _BV(COM1B1); // PWM 1B, clear on match, set on top
	TCCR1 = _BV(CS10); // run, no prescaler
	// do the actual animation
	uint16_t s1 = 0;
	uint16_t s2 = 0;
	uint16_t s3 = 0;
	uint8_t i = 0;
	// ramp up
	do {
		s1 += p1;
		s2 += p2;
		s3 += p3;
		OCR0A = s1 >> 8;
		OCR0B = s2 >> 8;
		OCR1B = s3 >> 8;	
		_delay_us(1);
	} while (++i != 0);
	// ramp down
	do {
		s1 -= p1;
		s2 -= p2;
		s3 -= p3;
		OCR0A = s1 >> 8;
		OCR0B = s2 >> 8;
		OCR1B = s3 >> 8;
		_delay_us(1);
	} while (++i != 0);
	// turn off timers
	TCCR0A = 0;
	TCCR0B = 0;
	GTCCR = 0;
	TCCR1 = 0;
	// power off timers
	PRR |= _BV(PRTIM1) | _BV(PRTIM0);
}

inline void animateLoop() {
	for (uint8_t i = 0; i < 60; i++) {
		animateOne();
		if (night())
			return;
	}
}

int main(void) {
	// ----------------- setup -----------------
	PRR = _BV(PRTIM1) | _BV(PRTIM0) | _BV(PRUSI) | _BV(PRADC); // turn off Time1, Timer0, USI & ADC
	ACSR = _BV(ACD); // turn off Analog Comparator
	DDRB = _BV(LED0_BIT) | _BV(LED1_BIT) | _BV(LED2_BIT) | _BV(LED3_BIT); // All LED pins are output
	PORTB = 0xff & ~(_BV(LED0_BIT) | _BV(LED1_BIT) | _BV(LED2_BIT) | _BV(LED3_BIT)); // pull up all other pins to ensure defined level and save power
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	sleep_enable();
	// ----------------- loop -----------------
    while (true) {
		animateLoop();
		// sleep while day continues
		do {
			wdSleep(WDTO_8S);
		} while (!night());
		// sleep while night
		do {
			wdSleep(WDTO_8S);
		} while (night());
    }
}