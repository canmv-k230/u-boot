#include <asm/asm.h>
#include <asm/io.h>
#include <asm/spl.h>
#include <asm/types.h>
#include <command.h>
#include <common.h>
#include <cpu_func.h>
#include <dm.h>
#include <env_internal.h>
#include <image.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <lmb.h>
#include <stdio.h>

#include <kendryte/k230_platform.h>

#include "board_common.h"

int ddr_init_training(void) {
  if (0x00 != (readl((const volatile void __iomem *)0x980001bcULL) & 0x1)) {
    // have init ,not need reinit;
    return 0;
  }

  board_ddr_init();

  kd_pin_set_ddr(8, 1);
  kd_pin_set_dr(8, 0);

  return 0;
}

int board_early_init_f(void) {
  /* force set boot medium to sdio0 */
  g_boot_medium = BOOT_MEDIUM_SDIO0;
  return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void) {
  // close cam_led
  char val = 0;
  k230_gpio('o', 8, &val);

  return 0;
}
#endif
