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
#include <common.h>
#include <asm/types.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <cpu_func.h>
#include <fdt_support.h>
#include <asm/io.h>
#include <command.h>
#include <spl.h>
#include <asm/cache.h>
#include <linux/delay.h>

#include <kendryte/k230_platform.h>

/*
 * CPU1_CLK_CFG: big core clock enabled from PLL0 undivided (1.6 GHz), its two
 * bus clock dividers at /2; bit 31 latches the dividers. The core only starts
 * in k230_run_system().
 */
#define K230_CPU1_CLK_CFG	(SYSCTL_CLK_BASE_ADDR + 0x04)
#define K230_CPU1_CLK_1600MHZ	0x80199805U

/*
 * AI_CLK_CFG: KPU core clock. [0] gate, [2] source 0=PLL1/2 1=PLL3/2,
 * [5:3] divider - 1, bit 31 latches the divider. It boots at PLL3/2 / 8
 * (100 MHz); RT-Smart's DVFS default profile is PLL3/2 / 1 (800 MHz) on
 * the KPU rail's fixed 0.8 V.
 */
#define K230_AI_CLK_CFG		(SYSCTL_CLK_BASE_ADDR + 0x08)
#define K230_AI_CLK_SRC_PLL3_DIV2	BIT(2)
#define K230_AI_CLK_DIV_MASK	(0x7 << 3)
#define K230_CLK_DIV_LATCH	BIT(31)

/*
 * Power-domain controller entry for the always-on PMU domain: [5] isolation
 * enable (reset 1), [4] power mode, [3:0] self-clearing NOC idle and
 * power-up/off triggers.
 */
#define K230_PMU_PWR_LPI_CTL	(SYSCTL_PWR_BASE_ADDR + 0x158)

/*
 * STC system timestamp counters. STCn_CFG: [0] enable, [1] load the init
 * value (W1T), [2] clear (W1T), [4:3] mode 0=27 MHz 1=1 MHz 2=90 kHz;
 * STC0_CFG also [5]/[6] PDMA interrupt for audio 0/1 capture.
 */
#define K230_STC0_CFG		(SYSCTL_STC_BASE_ADDR + 0x00)
#define K230_STC2_CFG		(SYSCTL_STC_BASE_ADDR + 0x20)
#define K230_STC3_CFG		(SYSCTL_STC_BASE_ADDR + 0x30)
#define K230_STC_EN		BIT(0)
#define K230_STC_MODE_1MHZ	(1 << 3)
#define K230_STC0_AUD0_CAP_EN	BIT(5)
#define K230_STC0_AUD1_CAP_EN	BIT(6)

int g_boot_medium = 0xFF;

/*
 * XuanTie C908 extension CSRs; bit names from the C908 user manual. OpenSBI
 * and RT-Smart rewrite MCCR2 and MHINT later with slightly different values
 * (0xe0410009, 0x16e30c).
 */
static inline void k230_hart_csr_init(void)
{
	/* MCOR: invalidate both L1 caches (CACHE_SEL=3, INV) and the branch
	 * predictors (BHT, BTB, indirect predictor).
	 */
	csr_write(CSR_MCOR, 0x70013);
	/* MCCR2: enable the L2 cache (L2EN, bit 3) with its prefetch and
	 * timing settings.
	 */
	csr_write(CSR_MCCR2, 0xe0000009);
	/* MHCR: enable I/D caches, write-allocate, write-back, return stack,
	 * branch prediction, BTB, indirect branch prediction, write burst and
	 * L0 BTB.
	 */
	csr_write(CSR_MHCR, 0x11ff);
	/* MXSTATUS: misaligned access (MM), user cache operations (UCME),
	 * CLINT supervisor interrupts (CLINTEE), page attribute extension
	 * (MAEE) and the T-Head instruction extensions (THEADISAEE).
	 */
	csr_write(CSR_MXSTATUS, 0x638000);
	/* MHINT: L1 data and instruction prefetch, prefetch distance and
	 * I-cache way prediction.
	 */
	csr_write(CSR_MHINT, 0x6e30c);
}

/*
 * Called by U-Boot proper's go, bootm and booti just before jumping to the
 * next image; SPL's direct boot does not use it. Writes back and invalidates
 * the L1 and L2 caches, then re-enables them through the hart CSRs, so the
 * next image starts with clean caches.
 */
int cleanup_before_linux(void)
{
	cache_flush();
	icache_disable();
	dcache_disable();

	/* l2cache.ciall: clean and invalidate the whole L2 cache. */
	asm volatile(".long 0x0170000b\n":::"memory");

	k230_hart_csr_init();
	csr_write(CSR_SMPEN, 0x1);

	return 0;
}

void harts_early_init(void)
{
	u32 ai_clk;

	/* SOC_BOOT_CTL[1:0]: boot medium strap, as boot_medium_e (NOR, NAND,
	 * SDIO0, SDIO1).
	 */
	g_boot_medium = readl((volatile void __iomem *)(SYSCTL_BOOT_BASE_ADDR + 0x40)) & 0x03;

	/* System controller, by block: clock, power, timer. */

	/* Big core clock; see K230_CPU1_CLK_CFG. */
	writel(K230_CPU1_CLK_1600MHZ, (volatile void __iomem *)K230_CPU1_CLK_CFG);

	/*
	 * KPU core clock to 800 MHz, the DVFS default, so RT-Smart finds it
	 * already there. The gate is left as it is.
	 */
	ai_clk = readl((volatile void __iomem *)K230_AI_CLK_CFG);
	ai_clk &= ~K230_AI_CLK_DIV_MASK;
	ai_clk |= K230_AI_CLK_SRC_PLL3_DIV2 | K230_CLK_DIV_LATCH;
	writel(ai_clk, (volatile void __iomem *)K230_AI_CLK_CFG);

	/* Release the isolation of the PMU domain (pmu_iso_en, reset 1), so the
	 * PMU and RTC block at 0x91000000 can be reached. The other fields are
	 * triggers where 0 means no operation.
	 */
	writel(0x0, (volatile void __iomem *)K230_PMU_PWR_LPI_CTL);

	/* STC2 and STC3 count at 27 MHz, the timebase; RT-Smart's
	 * clint_timer_init() enables them the same way. STC0 counts at 1 MHz
	 * with the audio capture timestamps. Neither clear nor load is set, so
	 * a running count is kept.
	 */
	writel(K230_STC_EN, (volatile void __iomem *)K230_STC2_CFG);
	writel(K230_STC_EN, (volatile void __iomem *)K230_STC3_CFG);
	writel(K230_STC_EN | K230_STC_MODE_1MHZ | K230_STC0_AUD0_CAP_EN |
	       K230_STC0_AUD1_CAP_EN, (volatile void __iomem *)K230_STC0_CFG);

	/* Interconnect and peripheral configuration. */

	/*
	 * The SDK does not document the following registers; the values are
	 * kept as delivered. 0x91301e8c is in the same block as the KPU
	 * bandwidth registers RT-Smart's gnne driver writes (0x91301d8c and
	 * 0x91301d90), but is a different register.
	 */
	writel(0x2, (volatile void __iomem *)0x91301e8c);

	/*
	 * Hi_sys_config block at 0x91585000, which also holds the OPI SPI and
	 * USB PHY controls: two instances of the same register pair.
	 */
	writel(0x33881B, (volatile void __iomem *)0x915850b0);
	writel(0x5e66a0, (volatile void __iomem *)0x915850b4);

	writel(0x33881B, (volatile void __iomem *)0x915850b8);
	writel(0x5e66a0, (volatile void __iomem *)0x915850bc);

	/* This hart: memory protection, then caches and extensions. */

#ifndef CONFIG_KBURN_OTP
	/* PMP entries 0 and 1: lock 4 KiB at 0x91213000 and 0x91214000 in the
	 * Security block as read-only (NAPOT, R, locked, so also for M-mode).
	 * The OTP burner build needs write access and skips this.
	 */
	csr_write(pmpaddr0, 0x24484dff);
	csr_write(pmpaddr1, 0x244851ff);
	csr_write(pmpcfg0, 0x9999);
#endif // CONFIG_KBURN_OTP

	k230_hart_csr_init();
}
