/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "board_common.h"
#include <kendryte/k230_platform.h>

#include <asm/asm.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/spl.h>
#include <asm/types.h>
#include <command.h>
#include <common.h>
#include <cpu_func.h>
#include <env_internal.h>
#include <gzip.h>
#include <image.h>
#include <linux/kernel.h>
#include <lmb.h>
#include <malloc.h>
#include <memalign.h>
#include <u-boot/crc.h>

#include <stdio.h>

DECLARE_GLOBAL_DATA_PTR;

uint64_t g_dram_base = CONFIG_MEM_BASE_ADDR;
uint64_t g_dram_size = 128 * 1024 * 1024;

int mmc_get_env_dev(void) { return (BOOT_MEDIUM_SDIO1 == g_boot_medium) ? 1 : 0; }

enum env_location arch_env_get_location(enum env_operation op, int prio)
{
    if (0 != prio) {
        return ENVL_UNKNOWN;
    }

#ifdef CONFIG_ENV_IS_NOWHERE
    return ENVL_NOWHERE;
#endif

    if (g_boot_medium == BOOT_MEDIUM_NORFLASH) {
        return ENVL_SPI_FLASH;
    }

    if (g_boot_medium == BOOT_MEDIUM_NANDFLASH) {
        return ENVL_SPINAND;
    }

    return ENVL_MMC;
}

#ifndef CONFIG_SPL_BUILD
int __weak kd_board_init(void) { return 0; }

int board_init(void)
{
    if ((BOOT_MEDIUM_SDIO0 == g_boot_medium) || (BOOT_MEDIUM_SDIO1 == g_boot_medium)) {
#define SD_HOST_REG_VOL_STABLE (1 << 4)
#define SD_CARD_WRITE_PROT     (1 << 6)

        u32 sd0_ctrl = readl((void*)SD0_CTRL);
        sd0_ctrl |= SD_HOST_REG_VOL_STABLE | SD_CARD_WRITE_PROT;
        writel(sd0_ctrl, (void*)SD0_CTRL);
    }

    return kd_board_init();
}
#endif

uint64_t detect_ddr_size(void)
{
    uint64_t ddr_size = 0;
    uint64_t readv0, readv1 = 0;
    uint64_t ddr_detect_pattern[] = { 0x1234567887654321, 0x1122334455667788 };

    gd->ram_base = CONFIG_MEM_BASE_ADDR;
    gd->ram_size = 0x8000000;

    g_dram_base = CONFIG_MEM_BASE_ADDR;
    g_dram_size = 128 * 1024 * 1024;

    writeq(ddr_detect_pattern[0], CONFIG_MEM_BASE_ADDR);
    writeq(ddr_detect_pattern[0], CONFIG_MEM_BASE_ADDR + 8);
    flush_dcache_range(CONFIG_MEM_BASE_ADDR, CONFIG_MEM_BASE_ADDR + CONFIG_SYS_CACHELINE_SIZE);

    for (ddr_size = 128 * 1024 * 1024; ddr_size <= (u64)(1 * 1024 * 1024 * 1024); ddr_size = ddr_size << 1) {
        invalidate_dcache_range(ddr_size, ddr_size + CONFIG_SYS_CACHELINE_SIZE);
        readv0 = readq(ddr_size);
        if (readv0 == ddr_detect_pattern[0]) {
            writeq(ddr_detect_pattern[1], ddr_size + 8);
            flush_dcache_range(ddr_size, ddr_size + CONFIG_SYS_CACHELINE_SIZE);
            invalidate_dcache_range(CONFIG_MEM_BASE_ADDR, CONFIG_MEM_BASE_ADDR + CONFIG_SYS_CACHELINE_SIZE);
            readv1 = readq(CONFIG_MEM_BASE_ADDR + 8);
            if (readv1 == ddr_detect_pattern[1]) {
                break;
            }
        }
    }

    gd->ram_size = ddr_size;
    g_dram_size  = ddr_size;

    return ddr_size;
}

int dram_init(void)
{
    detect_ddr_size();

    return 0;
}
