lattool
=======

Introduction
------------

A tiny tool, that allows to measure interrupt latencies of other boards by
triggering an external interrupt and measuring the delay until the answer
arrives.

The AVR will cyclically fire an event every 100ms.  This event must be captured
by another board as external interrupt / GPIO interrupt.  The board has to
answer by toggling a pin.  The AVR will capture the answer and dump the ticks
to the UART.  The ticks reflect the interrupt answer time.
