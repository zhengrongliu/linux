/*
 * board-dt.c
 *
 *  Created on: Jul 9, 2017
 *      Author: Zhengrong Liu<towering@126.com>
 */

#include <linux/kernel.h>
#include <asm/mach/arch.h>

static const char *const at91sam9_compat[] __initconst = {
	"atmel,at91sam9x5",
	NULL
};

DT_MACHINE_START(AT91SAM9, "at91sam9 (Device Tree Support)")
	.dt_compat = at91sam9_compat,
MACHINE_END
