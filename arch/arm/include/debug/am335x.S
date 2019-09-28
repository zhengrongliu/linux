/*
 * am335x.S
 *
 *  Created on: Feb 6, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#define AM335X_UART_THR		(0x0)
#define AM335X_UART_LSR		(0x14)
#define AM335X_UART_LSR_TXSRE	(0x40)
#define AM335X_UART_LSR_TXFIFOE	(0x20)

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =CONFIG_DEBUG_UART_PHYS		@ System peripherals (phys address)
	ldr	\rv, =CONFIG_DEBUG_UART_VIRT		@ System peripherals (virt address)
	.endm

	.macro	senduart,rd,rx
	strb    \rd, [\rx,#(AM335X_UART_THR)]              @ send lower byte of rd
	.endm

	.macro	waituart,rd,rx
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #(AM335X_UART_LSR)]		@ Read Status Register
	and	\rd, \rd, #(AM335X_UART_LSR_TXSRE|AM335X_UART_LSR_TXFIFOE)
	teq	\rd, #(AM335X_UART_LSR_TXSRE|AM335X_UART_LSR_TXFIFOE)
	bne	1001b
	.endm

