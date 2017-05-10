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

struct reset {
	volatile unsigned char *ddr;
	volatile unsigned char *port;
	unsigned char pin;
};

static const struct reset resets[] = {
	{
		.ddr = &DDRB,
		.port = &PORTB,
		.pin = PB1,
	}, {
		.ddr = &DDRB,
		.port = &PORTB,
		.pin = PB2,
	},
};

#define OUTPUT_LOW() OUTPUT_PORT &= ~(1 << OUTPUT)
#define OUTPUT_HIGH() OUTPUT_PORT |= (1 << OUTPUT)

#ifdef NOISE_CANCELER
#define DELAY_TICKS 3 /* cf. data sheet p. 119 */
#else
#define DELAY_TICKS 1
#endif

/* It takes us two ticks till the signal arrives. Measured with a scope */
#define ACTIVATION_TICKS 2

static volatile bool data_rdy, spurious_catpure, fired;
static volatile uint16_t capture_ticks;

#define STOP 1
#define STOPPED 2
#define LATENCY_RUN 3
#define LATENCY_RUNNING 4
#define LEVEL_RUN 5
#define LEVEL_RUNNING 6
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

static void resets_init(void)
{
	unsigned int i;
	const struct reset *rst = resets;

	for (i = 0; i < ARRAY_SIZE(resets); i++, rst++) {
		*rst->port |= (1 << rst->pin);
		*rst->ddr |= (1 << rst->pin);
	}
}

static void perform_board_reset(unsigned int board)
{
	const struct reset *rst = resets + board;

	*rst->port &= ~(1 << rst->pin);
	_delay_ms(100);
	*rst->port |= (1 << rst->pin);
}

static void uart_handler(unsigned char in)
{
	if (in == 'h') {
		status = STOP;
	} else if (in == 's') {
		status = LATENCY_RUN;
	} else if (in == 'l') {
		status = LEVEL_RUN;
	} else if (in == 'r') {
		uart_puts("Resetting board 0\n");
		perform_board_reset(0);
	} else if (in == 't') {
		uart_puts("Resetting board 1\n");
		perform_board_reset(1);
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
		TCNT1 = - 1 - ACTIVATION_TICKS - DELAY_TICKS;
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
	uint16_t capture;

	INPUT_DDR &= ~(1 << INPUT);
	/* activate pull up */
	INPUT_PORT |= (1 << INPUT);

	OUTPUT_DDR |= (1 << OUTPUT);
	OUTPUT_HIGH();

	resets_init();

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
		} else if (status == LEVEL_RUN) {
			uart_puts("Starting level measurement\n");
			TIMSK0 = 0;
			TIMSK1 = 0;
			status = LEVEL_RUNNING;

			while (status == LEVEL_RUNNING) {
				TCCR1B = (1 << CS10); /* edge select falling edge, start counter */
				TIFR1 = (1 << ICF1);
				/* wait for falling edge or status change */
				while (!(TIFR1 & (1 << ICF1)) &&
				       status == LEVEL_RUNNING);
				capture = ICR1;
				TCCR1B = (1 << CS10) | (1 << ICES1); /* edge select: rising edge */
				TIFR1 = (1 << ICF1); /* clear interrupt flag */

				/* wait for rising edge or status change */
				while (!(TIFR1 & (1 << ICF1)) &&
				       status == LEVEL_RUNNING);
				capture = ICR1 - capture;
				TIFR1 = (1 << ICF1); /* clear interrupt flag */

				if (status == LEVEL_RUNNING)
					uart_integer(capture);
			}
		}
	}
}
