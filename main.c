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

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "uart.h"

#define ARRAY_SIZE(x)  (sizeof(x) / sizeof((x)[0]))

//#define NOISE_CANCELER

#define INPUT_DDR DDRB
#define INPUT_PORT PORTB
#define INPUT_PIN PINB
#define INPUT PB0 /* ICP1 */

#define OUTPUT_DDR DDRD
#define OUTPUT_PORT PORTD
#define OUTPUT PD3

#define RESET_DDR DDRB
#define RESET_PORT PORTB
#define RESET_PIN PB1

#define RESET_HIGH() RESET_PORT |= (1 << RESET_PIN)
#define RESET_LOW() RESET_PORT &= ~(1 << RESET_PIN)

#define OUTPUT_LOW() OUTPUT_PORT &= ~(1 << OUTPUT)
#define OUTPUT_HIGH() OUTPUT_PORT |= (1 << OUTPUT)

#ifdef NOISE_CANCELER
#define DELAY_TICKS 3 /* cf. data sheet p. 119 */
#else
#define DELAY_TICKS 0
#endif

/* It takes us two ticks till the signal arrives. Measured with a scope */
#define ACTIVATION_TICKS 2

static volatile bool data_rdy, spurious_catpure, fired;
static volatile uint16_t capture_ticks;

#define STOP 1
#define STOPPED 2
#define LATENCY_RUN 3
#define LATENCY_RUNNING 4
static volatile unsigned char status;

struct setting {
	unsigned char timeout;
	unsigned char fire_freq;
};

static const struct setting settings[] = {
	/* 10Hz */ {
		.timeout = 20,
		.fire_freq = 25,
	},
	/* 50Hz */ {
		.timeout = 3,
		.fire_freq = 5,
	},
};

static const struct setting *setting = &settings[0];

static void perform_board_reset(void)
{
	RESET_LOW();
	_delay_ms(100);
	RESET_HIGH();
}

static void uart_handler(unsigned char in)
{
	if (in == 'h') {
		status = STOP;
	} else if (in == 's') {
		status = LATENCY_RUN;
	} else if (in == 'r') {
		uart_puts("Resetting board\n");
		perform_board_reset();
	} else if (isdigit(in)) {
		in -= '0';
		if (in < ARRAY_SIZE(settings)) {
			uart_puts("Setting ");
			uart_putc(in + 0x30);
			setting = &settings[in];
		} else {
			uart_puts("Invalid setting");
		}
		uart_putc('\n');
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
	OUTPUT_HIGH();

	if (tick == setting->timeout) {
		if (data_rdy) {
			data_rdy = false;
			uart_integer(capture_ticks);
		} else {
			uart_puts("TO\n");
		}
		fired = false;
	} else if (tick == setting->fire_freq) {
		tick = 0;

		/* fire! */
		fired = true;
#ifdef NOISE_CANCELER
		TCNT1 = - 1 - ACTIVATION_TICKS - DELAY_TICKS;
#else
		TCNT1 = - 1 - ACTIVATION_TICKS;
#endif
		OUTPUT_LOW();
	}
}

ISR(TIMER1_CAPT_vect)
{
	if (fired && !data_rdy) {
		capture_ticks = ICR1;
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
	OUTPUT_HIGH();

	RESET_HIGH();
	RESET_DDR |= (1 << RESET_PIN);

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

	TCCR1A = 0;
	/* no prescaler */
	TCCR1B = (1 << CS10);
#ifdef NOISE_CANCELER
	TCCR1B |= (1 << ICNC1);
#endif
	TIMSK1 = 0;

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
		} else if (status == LATENCY_RUN) {
			uart_puts("Starting latency measurement...\n");
			TIMSK0 = 0;
			TIMSK1 = 0;
			TCNT0 = 0;
			TCNT1 = 0;
			TCCR1B &= ~(1 << ICES1); /* edge select: falling edge */
			status = LATENCY_RUNNING;
			TIMSK0 = (1 << OCIE0A);
			TIMSK1 = (1 << ICIE1);
		}
	}
}
