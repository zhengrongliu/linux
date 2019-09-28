/*
 * board-dt.c
 *
 *  Created on: Feb 6, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/kernel.h>
#include <asm/mach/arch.h>

static const char *const am335x_compat[] __initconst = {
	"ti,am335x",
	NULL
};

DT_MACHINE_START(AM335X, "AM335x (Device Tree Support)")
	.dt_compat = am335x_compat,
MACHINE_END
