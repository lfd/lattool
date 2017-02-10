/*
 * lattool - An AVR tool that allows to measure interrupt response times
 *
 * Copyright (c) OTH Regensburg, 2017
 *
 * Authors:
 *  Ralf Ramsauer <ralf.ramsauer@oth-regensburg.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <stdbool.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#include "uart.h"

#define INPUT_DDR DDRB
#define INPUT_PORT PORTB
#define INPUT_PIN PINB
#define INPUT PB0 /* ICP1 */

#define OUTPUT_DDR DDRD
#define OUTPUT_PORT PORTD
#define OUTPUT PD3

#define OUTPUT_LOW() OUTPUT_PORT &= ~(1 << OUTPUT)
#define OUTPUT_HIGH() OUTPUT_PORT |= (1 << OUTPUT)

static volatile bool data_rdy, spurious_catpure, fired;
static volatile uint16_t latency_ticks;

ISR(TIMER0_COMPA_vect)
{
	static unsigned char tick = 0;
	char buffer[10];

	TCNT0 = 0;
	tick++;

	if (tick == 20) {
		if (data_rdy) {
			data_rdy = false;
			snprintf(buffer, sizeof(buffer), "%d\n", latency_ticks);
			uart_puts(buffer);
		} else {
			uart_puts("Timeout\r\n");
		}
		fired = false;
	} else if (tick == 25) {
		tick = 0;

		/* fire! */
		fired = true;
		TCNT1 = 0;
		OUTPUT_HIGH();
		asm volatile("nop");
		OUTPUT_LOW();
	}
}

ISR(TIMER1_CAPT_vect)
{
	if (fired && !data_rdy) {
		latency_ticks = ICR1;
		data_rdy = true;
		fired = false;
	} else {
		spurious_catpure = true;
	}
}

int main(void)
{
	INPUT_DDR &= ~(1 << INPUT);
	/* activate pull up */
	INPUT_PORT |= (1 << INPUT);

	OUTPUT_DDR |= (1 << OUTPUT);
	OUTPUT_LOW();

	uart_init();
	uart_puts("Interrupt response Latency Measurement Tool\r\n");

	/* Timer/Counter 0 gives us a 4ms beat */
	TCCR0A = 0;
	/* 256 prescaler */
	TCCR0B = (1 << CS02);
	/* This gives us a 4ms beat */
	OCR0A = 250;
	TIMSK0 = (1 << OCIE0A);

	TCCR1A = 0;
	/* enable input capture on rising edge, no prescaler */
	TCCR1B = (1 << ICNC1) | (1 << ICES1) | (1 << CS10);
	TIMSK1 = (1 << ICIE1);

	for(;;) {
		if (spurious_catpure) {
			spurious_catpure = false;
			uart_puts("spur\r\n");
		}
	}
}
