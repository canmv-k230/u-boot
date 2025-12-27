#include <asm/asm.h>
#include <asm/io.h>
#include <asm/types.h>
#include <lmb.h>
#include <cpu_func.h>
#include <stdio.h>
#include <common.h>
#include <command.h>
#include <image.h>
#include <gzip.h>
#include <asm/spl.h>
#include <linux/kernel.h>
#include <env_internal.h>
#include <linux/delay.h>

#include "board_common.h"

#include <kendryte/k230_platform.h>

int ddr_init_training(void)
{
	if( 0x00 != (readl((const volatile void __iomem *)0x980001bcULL) & 0x1 )) {
		//have init ,not need reinit;
		return 0; 
	}

	board_ddr_init();

	return 0;
}

int board_early_init_f(void) {
  /* force set boot medium to sdio0 */
  g_boot_medium = BOOT_MEDIUM_SDIO0;
  return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{

	return 0;
}
#endif
