#include <asm/asm.h>
#include <asm/io.h>
#include <asm/spl.h>
#include <asm/types.h>
#include <command.h>
#include <common.h>
#include <cpu_func.h>
#include <env_internal.h>
#include <gzip.h>
#include <image.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <lmb.h>
#include <stdio.h>
#include <dm.h>

#include "board_common.h"

#include <kendryte/k230_platform.h>

int ddr_init_training(void)
{
    if (0x00 != (readl((const volatile void __iomem*)0x980001bcULL) & 0x1)) {
        // have init ,not need reinit;
        return 0;
    }

    return board_ddr_init();
}

int board_early_init_f(void)
{
    g_boot_medium = BOOT_MEDIUM_SDIO0;

    int pins[] = { 11, 12, 40, 41, 42, 43, 46, 47, 48, 49 };

    for (int i = 0; i < ARRAY_SIZE(pins); i++) {
        kd_pin_set_ddr(pins[i], 1); // Set pins to output mode
        kd_pin_set_dr(pins[i], 0); // Set pins to low
    }

    return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void) { return 0; }
#endif
