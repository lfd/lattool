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
#include <stdlib.h>

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

#define STOP 1
#define STOPPED 2
#define RUN 3
#define RUNNING 4
static unsigned char status;

static void uart_handler(unsigned char in)
{
	if (in == 'h') {
		status = STOP;
	} else if (in == 's') {
		status = RUN;
	}
}

static inline void uart_integer(uint16_t integer)
{
	char buffer[10];
	utoa(integer, buffer, 10);
	uart_puts(buffer);
	uart_putc('\n');
}

ISR(TIMER0_COMPA_vect)
{
	static unsigned char tick = 0;

	TCNT0 = 0;
	tick++;
	OUTPUT_LOW();

	if (tick == 20) {
		if (data_rdy) {
			data_rdy = false;
			uart_integer(latency_ticks);
		} else {
			uart_puts("TO\n");
		}
		fired = false;
	} else if (tick == 25) {
		tick = 0;

		/* fire! */
		fired = true;
		TCNT1 = 0;
		OUTPUT_HIGH();
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
	uart_puts("Interrupt response Latency Measurement Tool\n");
	uart_set_recv_handler(uart_handler);

	/* Timer/Counter 0 gives us a 4ms beat */
	TCCR0A = 0;
	/* 256 prescaler */
	TCCR0B = (1 << CS02);
	/* This gives us a 4ms beat */
	OCR0A = 250;
	TIMSK0 = 0;
	//TIMSK0 = (1 << OCIE0A);

	TCCR1A = 0;
	/* enable input capture on rising edge, no prescaler */
	TCCR1B = (1 << ICNC1) | (1 << ICES1) | (1 << CS10);
	TIMSK1 = 0;
	//TIMSK1 = (1 << ICIE1);

	sei();

	for(;;) {
		if (spurious_catpure) {
			spurious_catpure = false;
			uart_puts("SP\n");
		}

		if (status == STOP) {
			TIMSK0 = 0;
			TIMSK1 = 0;
			status = STOPPED;
			uart_puts("Stopped measurement...\n");
		} else if (status == RUN) {
			uart_puts("Starting measurement...\n");
			TCNT0 = 0;
			TCNT1 = 0;
			TIMSK0 = (1 << OCIE0A);
			TIMSK1 = (1 << ICIE1);
			status = RUNNING;
		}
	}
}
