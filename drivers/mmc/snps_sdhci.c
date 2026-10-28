// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 IBM Corp.
 * Eddie James <eajames@linux.ibm.com>
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <malloc.h>
#include <sdhci.h>
#include <dm/device_compat.h>
#include <linux/delay.h>

#define SNPS_SDHCI_MIN_FREQ 400000

struct snps_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

#define K230_MMC0_BASE			0x91580000UL
#define K230_MMC_CONFIG_REG		0x91213410UL
#define K230_MMC_CONFIG_SD_MODE		0x3

#define DWC_MSHC_PTR_VENDOR1		0x500
#define DWC_MSHC_CTRL_R			(DWC_MSHC_PTR_VENDOR1 + 0x8)
#define DWC_MSHC_EMMC_CTRL_R		(DWC_MSHC_PTR_VENDOR1 + 0x2c)
#define DWC_MSHC_CARD_IS_EMMC		BIT(0)

#ifdef MMC_SUPPORTS_TUNING
#define DWC_MSHC_AT_CTRL_R		(DWC_MSHC_PTR_VENDOR1 + 0x40)
#define DWC_MSHC_AT_STAT_R		(DWC_MSHC_PTR_VENDOR1 + 0x44)
#define SDHCI_TUNE_AT_EN BIT(0)
#define SDHCI_TUNE_CI_SEL BIT(1)
#define SDHCI_TUNE_SWIN_TH_EN BIT(2)
#define SDHCI_TUNE_RPT_TUNE_ERR BIT(3)
#define SDHCI_TUNE_SW_TUNE_EN BIT(4)
#define SDHCI_TUNE_WIN_EDGE_SEL_MASK (0xf << 8)
#define SDHCI_TUNE_CLK_STOP_EN_MASK BIT(16)
#define SDHCI_TUNE_PRE_CHANGE_DLY_LSB 17
#define SDHCI_TUNE_PRE_CHANGE_DLY_MASK \
	(0x3 << SDHCI_TUNE_PRE_CHANGE_DLY_LSB)
#define SDHCI_TUNE_POST_CHANGE_DLY_LSB 19
#define SDHCI_TUNE_POST_CHANGE_DLY_MASK \
	(0x3 << SDHCI_TUNE_POST_CHANGE_DLY_LSB)
#define SDHCI_TUNE_SWIN_TH_VAL_LSB 24
#define SDHCI_TUNE_SWIN_TH_VAL_MASK \
	(0xffU << SDHCI_TUNE_SWIN_TH_VAL_LSB)
#define SDHCI_TUNE_PRE_CHANGE_DLY_VAL 0x1
#define SDHCI_TUNE_POST_CHANGE_DLY_VAL 0x3
#define SDHCI_TUNE_SWIN_TH_VAL 0x9
#define SDHCI_TUNING_LOOP_COUNT 128
#define SDHCI_TUNING_CMD_TIMEOUT_MS 50
#define SDHCI_TUNING_TOTAL_TIMEOUT_MS 2000
#endif

#define DWC_MSHC_PTR_PHY_REGS		0x300
#define DWC_MSHC_PHY_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x0)
#define PAD_SN_LSB 20
#define PAD_SN_MASK 0xF
#define PAD_SN_DEFAULT ((0x8 & PAD_SN_MASK) << PAD_SN_LSB)
#define PAD_SP_LSB 16
#define PAD_SP_MASK 0xF
#define PAD_SP_DEFAULT ((0x9 & PAD_SP_MASK) << PAD_SP_LSB)
#define PHY_PWRGOOD BIT(1)
#define PHY_RSTN BIT(0)

#define DWC_MSHC_CMDPAD_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x4)
#define DWC_MSHC_DATPAD_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x6)
#define DWC_MSHC_CLKPAD_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x8)
#define DWC_MSHC_STBPAD_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0xa)
#define DWC_MSHC_RSTNPAD_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0xc)
#define TXSLEW_N_LSB 9
#define TXSLEW_P_LSB 5
#define WEAKPULL_EN_LSB 3
#define RXSEL_LSB 0

#define DWC_MSHC_COMMDL_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x1c)
#define DWC_MSHC_SDCLKDL_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x1d)
#define DWC_MSHC_SDCLKDL_DC		(DWC_MSHC_PTR_PHY_REGS + 0x1e)
#define DWC_MSHC_SMPLDL_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x20)
#define DWC_MSHC_SDCLKDL_EXTDLY_EN	BIT(0)
#define DWC_MSHC_SDCLKDL_UPDATE		BIT(4)
#define DWC_MSHC_RX_EXTDLY_EN		BIT(0)
#define DWC_MSHC_EXT_DELAY_OFFSET	128
#define DWC_MSHC_MAX_DELAY		255
#define DWC_MSHC_HS_SDR_MAX_HZ		52000000U

#define DWC_MSHC_ATDL_CNFG		(DWC_MSHC_PTR_PHY_REGS + 0x21)

#define DWC_MSHC_PHY_PAD_SD_CLK                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (2 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_SD_DAT                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (2 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_SD_STB                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (2 << WEAKPULL_EN_LSB) |  \
	 (2 << RXSEL_LSB))

#define DWC_MSHC_PHY_PAD_EMMC_CLK                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_EMMC_DAT                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_EMMC_STB                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (2 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))

static bool k230_sdhci_uses_1p8v(struct sdhci_host *host)
{
#ifdef CONFIG_MMC_AUTO_DETECT_VOLTAGE
	u32 cfg;

	if (host->ioaddr != (void *)K230_MMC0_BASE)
		return false;

	cfg = readl((void __iomem *)K230_MMC_CONFIG_REG);
	cfg = cpu_to_be32(cfg);

	return (cfg & K230_MMC_CONFIG_SD_MODE) != K230_MMC_CONFIG_SD_MODE;
#else
	return dev_read_bool(host->mmc->dev, "1-8-v");
#endif
}

static void dwcmshc_phy_pad_config(struct sdhci_host *host)
{
	bool is_1p8v = k230_sdhci_uses_1p8v(host);
	u16 clk_ctrl;
	u16 clk_pad;
	u16 data_pad;
	u16 strobe_pad;

	/* Disable the card clock */
	clk_ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	data_pad = is_1p8v ? DWC_MSHC_PHY_PAD_EMMC_DAT :
			       DWC_MSHC_PHY_PAD_SD_DAT;
	clk_pad = is_1p8v ? DWC_MSHC_PHY_PAD_EMMC_CLK :
			      DWC_MSHC_PHY_PAD_SD_CLK;
	strobe_pad = is_1p8v ? DWC_MSHC_PHY_PAD_EMMC_STB :
				 DWC_MSHC_PHY_PAD_SD_STB;

	sdhci_writew(host, data_pad, DWC_MSHC_CMDPAD_CNFG);
	sdhci_writew(host, data_pad, DWC_MSHC_DATPAD_CNFG);
	sdhci_writew(host, clk_pad, DWC_MSHC_CLKPAD_CNFG);
	sdhci_writew(host, strobe_pad, DWC_MSHC_STBPAD_CNFG);
	sdhci_writew(host, data_pad, DWC_MSHC_RSTNPAD_CNFG);
}

static int dwcmshc_phy_delay_config(struct sdhci_host *host)
{
	u32 tx_delay_line = 0xb0;
	u32 rx_delay_line = 0xd;
	u32 sdclkdl_dc;
	u8 sdclkdl_cfg;
	u8 sdclkdl_update;

	dev_read_u32(host->mmc->dev, "tx_delay_line", &tx_delay_line);
	dev_read_u32(host->mmc->dev, "rx_delay_line", &rx_delay_line);
	if (tx_delay_line > DWC_MSHC_MAX_DELAY ||
	    rx_delay_line > DWC_MSHC_MAX_DELAY) {
		dev_err(host->mmc->dev,
			"invalid PHY delays: tx=%u rx=%u\n",
			tx_delay_line, rx_delay_line);
		return -EINVAL;
	}

	if (tx_delay_line >= DWC_MSHC_EXT_DELAY_OFFSET) {
		sdclkdl_cfg = DWC_MSHC_SDCLKDL_EXTDLY_EN;
		sdclkdl_dc = tx_delay_line - DWC_MSHC_EXT_DELAY_OFFSET;
	} else {
		sdclkdl_cfg = 0;
		sdclkdl_dc = tx_delay_line;
	}

	/* Latch the command and clock delay settings while they are disabled. */
	sdhci_writeb(host, 1, DWC_MSHC_COMMDL_CNFG);
	sdclkdl_update = sdclkdl_cfg | DWC_MSHC_SDCLKDL_UPDATE;
	sdhci_writeb(host, sdclkdl_update, DWC_MSHC_SDCLKDL_CNFG);
	sdhci_writeb(host, sdclkdl_dc & 0x7f, DWC_MSHC_SDCLKDL_DC);
	sdhci_writeb(host, sdclkdl_cfg, DWC_MSHC_SDCLKDL_CNFG);
	sdhci_writeb(host, rx_delay_line, DWC_MSHC_SMPLDL_CNFG);

	return 0;
}

#ifdef MMC_SUPPORTS_TUNING
static int k230_sdhci_set_delay_mode(struct sdhci_host *host)
{
	u32 tx_delay = 0xb0;
	u32 rx_delay = 0xd;
	u8 tx, rx, drift;
	u8 next_tx, next_rx, next_drift;
	u16 clk;
	bool variable_only;

	if (host->ioaddr != (void *)K230_MMC0_BASE ||
	    !host->mmc->version || IS_SD(host->mmc))
		return 0;
	dev_read_u32(host->mmc->dev, "tx_delay_line", &tx_delay);
	dev_read_u32(host->mmc->dev, "rx_delay_line", &rx_delay);
	if (tx_delay > DWC_MSHC_MAX_DELAY || rx_delay > DWC_MSHC_MAX_DELAY)
		return -EINVAL;
	variable_only = host->mmc->selected_mode == MMC_HS_200 &&
			host->mmc->clock > DWC_MSHC_HS_SDR_MAX_HZ;
	tx = sdhci_readb(host, DWC_MSHC_SDCLKDL_CNFG);
	rx = sdhci_readb(host, DWC_MSHC_SMPLDL_CNFG);
	drift = sdhci_readb(host, DWC_MSHC_ATDL_CNFG);
	next_tx = tx & ~DWC_MSHC_SDCLKDL_EXTDLY_EN;
	if (!variable_only && tx_delay >= DWC_MSHC_EXT_DELAY_OFFSET)
		next_tx |= DWC_MSHC_SDCLKDL_EXTDLY_EN;
	next_rx = rx & ~DWC_MSHC_RX_EXTDLY_EN;
	if (!variable_only)
		next_rx |= rx_delay & DWC_MSHC_RX_EXTDLY_EN;
	next_drift = (drift & ~DWC_MSHC_RX_EXTDLY_EN) |
		     (next_rx & DWC_MSHC_RX_EXTDLY_EN);
	if (tx == next_tx && rx == next_rx && drift == next_drift)
		return 0;

	/*
	 * Fixed delay extensions are for low clocks. Preserve variable taps
	 * and routing, gate the card clock while switching ranges, and hold
	 * TX update for at least 12.5 ns.
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_writew(host, clk & ~SDHCI_CLOCK_CARD_EN, SDHCI_CLOCK_CONTROL);
	udelay(1);
	sdhci_writeb(host, tx | DWC_MSHC_SDCLKDL_UPDATE, DWC_MSHC_SDCLKDL_CNFG);
	udelay(1);
	sdhci_writeb(host, next_tx | DWC_MSHC_SDCLKDL_UPDATE, DWC_MSHC_SDCLKDL_CNFG);
	sdhci_writeb(host, next_rx, DWC_MSHC_SMPLDL_CNFG);
	sdhci_writeb(host, next_drift, DWC_MSHC_ATDL_CNFG);
	udelay(1);
	sdhci_writeb(host, next_tx, DWC_MSHC_SDCLKDL_CNFG);
	udelay(1);
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
	return 0;
}

static int k230_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	unsigned int timeout = 100;

	sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET);
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & mask) {
		if (!timeout--) {
			dev_err(host->mmc->dev, "reset 0x%x timed out\n", mask);
			return -ETIMEDOUT;
		}
		udelay(1000);
	}

	return 0;
}

static int k230_sdhci_wait_bus_idle(struct sdhci_host *host)
{
	ulong start = get_timer(0);

	while (sdhci_readl(host, SDHCI_PRESENT_STATE) &
	       (SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT)) {
		if (get_timer(start) >= 100)
			return -ETIMEDOUT;
		udelay(1);
	}

	return 0;
}

static int k230_sdhci_config_tuning_engine(struct sdhci_host *host)
{
	u16 clk;
	u16 ctrl2;
	u16 stopped_clk;
	u32 val;
	unsigned int timeout;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	stopped_clk = clk & ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, stopped_clk, SDHCI_CLOCK_CONTROL);

	/* AT_CTRL threshold fields require SAMPLE_CLK_SEL to be clear. */
	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~(SDHCI_CTRL_EXEC_TUNING | SDHCI_CTRL_TUNED_CLK);
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);
	/*
	 * Sampling and drift-monitor delay lines must have matching phase
	 * ranges. Both use the TX reference as input (INPSEL=3); retain the
	 * sampling line's fixed 128-stage extension in the drift path too.
	 */
	sdhci_writeb(host, 0xc | (sdhci_readb(host, DWC_MSHC_SMPLDL_CNFG) &
			       DWC_MSHC_RX_EXTDLY_EN), DWC_MSHC_ATDL_CNFG);
	val = sdhci_readl(host, DWC_MSHC_AT_CTRL_R);
	val &= ~(SDHCI_TUNE_CI_SEL | SDHCI_TUNE_RPT_TUNE_ERR |
		 SDHCI_TUNE_SW_TUNE_EN | SDHCI_TUNE_WIN_EDGE_SEL_MASK |
		 SDHCI_TUNE_PRE_CHANGE_DLY_MASK |
		 SDHCI_TUNE_POST_CHANGE_DLY_MASK |
		 SDHCI_TUNE_SWIN_TH_VAL_MASK);
	val |= SDHCI_TUNE_AT_EN | SDHCI_TUNE_SWIN_TH_EN |
		SDHCI_TUNE_CLK_STOP_EN_MASK |
		(SDHCI_TUNE_PRE_CHANGE_DLY_VAL <<
		 SDHCI_TUNE_PRE_CHANGE_DLY_LSB) |
		(SDHCI_TUNE_POST_CHANGE_DLY_VAL <<
		 SDHCI_TUNE_POST_CHANGE_DLY_LSB) |
		(SDHCI_TUNE_SWIN_TH_VAL << SDHCI_TUNE_SWIN_TH_VAL_LSB);
	/*
	 * The variable delay chain can take 11.8 ns, exceeding two cycles
	 * at 200 MHz. Allow four cycles with PRE_CHANGE_DLY=3.
	 */
	if (!(sdhci_readb(host, DWC_MSHC_SMPLDL_CNFG) & DWC_MSHC_RX_EXTDLY_EN)) {
		val &= ~SDHCI_TUNE_PRE_CHANGE_DLY_MASK;
		val |= 3U << SDHCI_TUNE_PRE_CHANGE_DLY_LSB;
	}
	sdhci_writel(host, val, DWC_MSHC_AT_CTRL_R);
	/* AT_STAT contains phase codes, not sticky errors. Its center code is
	 * writable only with SW_TUNE_EN, which is disabled above. */

	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
	if (!(clk & SDHCI_CLOCK_INT_EN))
		return 0;

	timeout = 200;
	while (!(sdhci_readw(host, SDHCI_CLOCK_CONTROL) &
		 SDHCI_CLOCK_INT_STABLE)) {
		if (!timeout--)
			return -ETIMEDOUT;
		udelay(100);
	}

	return 0;
}

static void k230_sdhci_reset_tuning(struct sdhci_host *host)
{
	u16 clk;
	u16 ctrl2;
	u32 val;

	/* AT_EN may only change with the card clock stopped. */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_writew(host, clk & ~SDHCI_CLOCK_CARD_EN, SDHCI_CLOCK_CONTROL);
	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~(SDHCI_CTRL_EXEC_TUNING | SDHCI_CTRL_TUNED_CLK);
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);
	val = sdhci_readl(host, DWC_MSHC_AT_CTRL_R);
	sdhci_writel(host, val & ~(SDHCI_TUNE_AT_EN | SDHCI_TUNE_RPT_TUNE_ERR),
		     DWC_MSHC_AT_CTRL_R);
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

static int k230_sdhci_send_tuning_cmd(struct sdhci_host *host, u8 opcode)
{
	u32 stat;
	u16 block_size;
	u16 block_size_reg;
	u16 cmd;
	u16 flags;
	ulong start;
	int ret;

	ret = k230_sdhci_wait_bus_idle(host);
	if (ret)
		return ret;

	block_size = host->mmc->bus_width == 8 ? 128 : 64;
	block_size_reg = SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, block_size);
	sdhci_writel(host, SDHCI_INT_ALL_MASK, SDHCI_INT_STATUS);
	sdhci_writew(host, block_size_reg, SDHCI_BLOCK_SIZE);
	sdhci_writew(host, 1, SDHCI_BLOCK_COUNT);
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);
	sdhci_writel(host, 0, SDHCI_ARGUMENT);

	flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
		SDHCI_CMD_DATA;
	cmd = SDHCI_MAKE_CMD(opcode, flags);
	sdhci_writew(host, cmd, SDHCI_COMMAND);

	start = get_timer(0);
	do {
		stat = sdhci_readl(host, SDHCI_INT_STATUS);
		if (stat & SDHCI_INT_ERROR) {
			if (stat & (SDHCI_INT_TIMEOUT | SDHCI_INT_DATA_TIMEOUT))
				return -ETIMEDOUT;

			return -EIO;
		}
		if (get_timer(start) >= SDHCI_TUNING_CMD_TIMEOUT_MS)
			return -ETIMEDOUT;
	} while (!(stat & SDHCI_INT_DATA_AVAIL));

	/*
	 * The tuning engine consumes the pattern; DATA_AVAIL is completion,
	 * not a request to drain the normal PIO FIFO.
	 */

	return 0;
}

static int k230_sdhci_execute_tuning(struct mmc *mmc, u8 opcode)
{
	struct sdhci_host *host = mmc->priv;
	u32 old_int_enable;
	u32 old_signal_enable;
	u32 at_stat;
	u16 ctrl2;
	ulong start;
	bool tuning_done = false;
	int ret;
	unsigned int attempts = 0;

	if (opcode != MMC_CMD_SEND_TUNING_BLOCK_HS200)
		return -EINVAL;
	if (mmc->selected_mode != MMC_HS_200)
		return 0;

	old_int_enable = sdhci_readl(host, SDHCI_INT_ENABLE);
	old_signal_enable = sdhci_readl(host, SDHCI_SIGNAL_ENABLE);

	ret = k230_sdhci_config_tuning_engine(host);
	if (ret) {
		dev_err(mmc->dev, "failed to configure HS200 tuning: %d\n", ret);
		k230_sdhci_reset_tuning(host);
		goto restore_interrupts;
	}

	/* SDHCI hardware tuning reports only Buffer Read Ready. Missing replies
	 * are bounded by the 50 ms per-command wait, not just the overall limit. */
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_SIGNAL_ENABLE);
	sdhci_writel(host, SDHCI_INT_ALL_MASK, SDHCI_INT_STATUS);

	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~SDHCI_CTRL_TUNED_CLK;
	ctrl2 |= SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

	start = get_timer(0);
	ret = -ETIMEDOUT;
	while (attempts < SDHCI_TUNING_LOOP_COUNT) {
		if (get_timer(start) >= SDHCI_TUNING_TOTAL_TIMEOUT_MS)
			break;

		attempts++;
		ret = k230_sdhci_send_tuning_cmd(host, opcode);
		if (ret)
			break;

		ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl2 & SDHCI_CTRL_EXEC_TUNING)) {
			tuning_done = true;
			ret = ctrl2 & SDHCI_CTRL_TUNED_CLK ? 0 : -EIO;
			break;
		}
	}

	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	at_stat = sdhci_readl(host, DWC_MSHC_AT_STAT_R);
	if (!tuning_done && !ret)
		ret = -ETIMEDOUT;

	if (!ret)
		ret = k230_sdhci_reset(host, SDHCI_RESET_DATA);

	if (!ret) {
		dev_dbg(mmc->dev, "HS200 tuning passed after %u attempts\n",
			attempts);
	} else {
		k230_sdhci_reset_tuning(host);
		k230_sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
		dev_err(mmc->dev,
			"HS200 tuning failed: %d, attempts=%u, hostctl2=0x%04x, at_stat=0x%08x\n",
			ret, attempts, ctrl2, at_stat);
	}

restore_interrupts:
	/* Acknowledge stale tuning status before restoring normal IRQs. */
	sdhci_writel(host, 0, SDHCI_SIGNAL_ENABLE);
	sdhci_writel(host, SDHCI_INT_ALL_MASK, SDHCI_INT_STATUS);
	sdhci_writel(host, old_int_enable, SDHCI_INT_ENABLE);
	sdhci_writel(host, old_signal_enable, SDHCI_SIGNAL_ENABLE);

	return ret;
}
#endif

static int dwcmshc_phy_init(struct sdhci_host *host)
{
	u32 reg;
	unsigned int timeout = 15000;
	int ret;

	/* reset phy */
	sdhci_writew(host, 0, DWC_MSHC_PHY_CNFG);

	/* Disable the clock */
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	dwcmshc_phy_pad_config(host);
	ret = dwcmshc_phy_delay_config(host);
	if (ret)
		return ret;

	/* Wait max 150 ms */
	while (1) {
		reg = sdhci_readl(host, DWC_MSHC_PHY_CNFG);
		if (reg & PHY_PWRGOOD)
			break;
		if (!timeout) {
			dev_err(host->mmc->dev, "PHY power-good timed out\n");
			return -ETIMEDOUT;
		}
		timeout--;

		udelay(10);
	}

	reg = PAD_SN_DEFAULT | PAD_SP_DEFAULT;
	sdhci_writel(host, reg, DWC_MSHC_PHY_CNFG);

	/* de-assert the phy */
	reg |= PHY_RSTN;
	sdhci_writel(host, reg, DWC_MSHC_PHY_CNFG);

	return 0;
}

static void k230_sdhci_set_control_reg(struct sdhci_host *host)
{
#ifdef CONFIG_MMC_SDHCI_SNPS
	/*
	 * Card identification precedes bus-width/timing selection. The eMMC
	 * engine (including CMD21) needs CARD_IS_EMMC; keep reset bits intact.
	 */
	if (host->ioaddr == (void *)K230_MMC0_BASE && host->mmc->version) {
		u16 ctrl = sdhci_readw(host, DWC_MSHC_EMMC_CTRL_R);

		if (IS_SD(host->mmc))
			ctrl &= ~DWC_MSHC_CARD_IS_EMMC;
		else
			ctrl |= DWC_MSHC_CARD_IS_EMMC;
		sdhci_writew(host, ctrl, DWC_MSHC_EMMC_CTRL_R);
	}

	/* Keep the existing SDIO-path command-conflict setting. */
	if (host->ioaddr != (void *)K230_MMC0_BASE)
		sdhci_writeb(host, 0, DWC_MSHC_CTRL_R);

	if (k230_sdhci_uses_1p8v(host)) {
		u16 ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		ctrl |= SDHCI_CTRL_VDD_180;
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
	}
#endif

	sdhci_set_uhs_timing(host);
	/*
	 * DWC uses eMMC mode encodings once CARD_IS_EMMC is set. Encoding 2
	 * (generic SDHCI's HS52 mapping) is reserved; HS SDR uses encoding 1.
	 */
	if (host->ioaddr == (void *)K230_MMC0_BASE &&
	    host->mmc->version && !IS_SD(host->mmc) &&
	    (host->mmc->selected_mode == MMC_HS ||
	     host->mmc->selected_mode == MMC_HS_52)) {
		u16 ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		ctrl &= ~SDHCI_CTRL_UHS_MASK;
		ctrl |= SDHCI_CTRL_UHS_SDR25;
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
	}
}

static int k230_sdhci_set_ios_post(struct sdhci_host *host)
{
	/* Generic U-Boot 2022.10 omits MMC_HS_52 from the HISPD mode list. */
	if (host->ioaddr == (void *)K230_MMC0_BASE &&
	    host->mmc->version && !IS_SD(host->mmc) &&
	    host->mmc->selected_mode == MMC_HS_52) {
		u8 ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);

		sdhci_writeb(host, ctrl | SDHCI_CTRL_HISPD, SDHCI_HOST_CONTROL);
	}
#ifdef MMC_SUPPORTS_TUNING
	return k230_sdhci_set_delay_mode(host);
#else
	return 0;
#endif
}

static const struct sdhci_ops k230_host_ops = {
	.set_control_reg = k230_sdhci_set_control_reg,
	.set_ios_post = k230_sdhci_set_ios_post,
#ifdef MMC_SUPPORTS_TUNING
	.platform_execute_tuning = k230_sdhci_execute_tuning,
#endif
	.deferred_probe = dwcmshc_phy_init,
};

static int snps_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct snps_sdhci_plat *plat = dev_get_plat(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	struct mmc_config *cfg = &plat->cfg;
	bool have_emmc_phy;
	u32 max_clk;
	struct clk clk;
	int ret;

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret)
		return ret;

	host->name = dev->name;
	host->ioaddr = (void *)devfdt_get_addr(dev);

	max_clk = clk_get_rate(&clk);
	if (IS_ERR_VALUE(max_clk)) {
		ret = max_clk;
		goto err;
	}

	host->max_clk = max_clk;
	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	host->mmc->priv = host;
	upriv->mmc = host->mmc;

	have_emmc_phy = dev_read_bool(dev, "have-emmc-phy");
	if (have_emmc_phy) {
		host->ops = &k230_host_ops;

		/* Supply fixed eMMC voltage before capabilities are filtered. */
		if (k230_sdhci_uses_1p8v(host)) {
			host->quirks |= SDHCI_QUIRK_BROKEN_VOLTAGE;
			host->voltages |= MMC_VDD_165_195;
		}
	}

	ret = sdhci_setup_cfg(cfg, host, cfg->f_max, SNPS_SDHCI_MIN_FREQ);
	if (ret)
		goto err;

	ret = sdhci_probe(dev);
	if (ret)
		goto err;

	return 0;

err:
	clk_disable(&clk);
	clk_free(&clk);
	return ret;
}

static int snps_sdhci_bind(struct udevice *dev)
{
	struct snps_sdhci_plat *plat = dev_get_plat(dev);

	mmc_of_parse(dev, &plat->cfg);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id snps_sdhci_ids[] = {
	{ .compatible = "snps,dwcmshc-sdhci" },
	{ .compatible = "snps,dwcmshc-sdhci-k230" },
	{ }
};

U_BOOT_DRIVER(snps_sdhci_drv) = {
	.name		= "snps_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= snps_sdhci_ids,
	.ops		= &sdhci_ops,
	.bind		= snps_sdhci_bind,
	.probe		= snps_sdhci_probe,
	.priv_auto	= sizeof(struct sdhci_host),
	.plat_auto	= sizeof(struct snps_sdhci_plat),
};
