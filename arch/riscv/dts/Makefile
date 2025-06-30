# SPDX-License-Identifier: GPL-2.0+

dtb-$(CONFIG_TARGET_AX25_AE350) += ae350_32.dtb ae350_64.dtb
dtb-$(CONFIG_TARGET_MICROCHIP_ICICLE) += microchip-mpfs-icicle-kit.dtb
dtb-$(CONFIG_TARGET_QEMU_VIRT) += qemu-virt32.dtb qemu-virt64.dtb
dtb-$(CONFIG_TARGET_OPENPITON_RISCV64) += openpiton-riscv64.dtb
dtb-$(CONFIG_TARGET_SIFIVE_UNLEASHED) += hifive-unleashed-a00.dtb
dtb-$(CONFIG_TARGET_SIFIVE_UNMATCHED) += hifive-unmatched-a00.dtb
dtb-$(CONFIG_TARGET_SIPEED_MAIX) += k210-maix-bit.dtb

dtb-$(CONFIG_TARGET_K230_BURNTOOL) += k230_burntool_mmc.dtb # just for burntool
dtb-$(CONFIG_TARGET_K230_BURNTOOL) += k230_burntool_spi_nand.dtb # just for burntool
dtb-$(CONFIG_TARGET_K230_BURNTOOL) += k230_burntool_spi_nor.dtb # just for burntool

dtb-$(CONFIG_TARGET_K230_CANMV) += k230_canmv.dtb
dtb-$(CONFIG_TARGET_K230_CANMV_V3P0) += k230_canmv_v3p0.dtb
dtb-$(CONFIG_TARGET_K230_CANMV_V3P0) += k230_canmv_lckfb.dtb	# lckfb use same target.
dtb-$(CONFIG_TARGET_K230_CANMV_01STUDIO) += k230_canmv_01studio.dtb
dtb-$(CONFIG_TARGET_K230D_CANMV_BPI_ZERO) += k230d_canmv_bpi_zero.dtb
dtb-$(CONFIG_TARGET_K230D_CANMV_JUNROC_AI_CAM) += k230d_canmv_junroc_ai_cam.dtb
dtb-$(CONFIG_TARGET_K230_CANMV_DONGSHANPI) += k230_canmv_dongshanpi.dtb
dtb-$(CONFIG_TARGET_K230D_CANMV_ATK_DNK230D) += k230d_canmv_atk_dnk230d.dtb
dtb-$(CONFIG_TARGET_K230_EVB) += k230_evb.dtb
dtb-$(CONFIG_TARGET_K230_CANMV_RTT_EVB) += k230_canmv_rtt_evb.dtb


include $(srctree)/scripts/Makefile.dts

targets += $(dtb-y)

DTC_FLAGS += -R 4 -p 0x1000

PHONY += dtbs
dtbs: $(addprefix $(obj)/, $(dtb-y))
	@:

clean-files := *.dtb
