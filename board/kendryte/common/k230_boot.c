/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
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
#include <asm/asm.h>
#include <asm/io.h>
#include <asm/spl.h>
#include <asm/types.h>
#include <command.h>
#include <common.h>
#include <cpu_func.h>
#include <dm/device-internal.h>
#include <gzip.h>
#include <image.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <lmb.h>
#include <mmc.h>
#include <nand.h>
#include <spi.h>
#include <spi_flash.h>
#include <spl.h>
#include <stdint.h>
#include <stdio.h>

#include "board_common.h"

#ifdef CONFIG_AUTO_DETECT_DDR_SIZE
    #include <asm/global_data.h>
    DECLARE_GLOBAL_DATA_PTR;

    #ifndef CONFIG_MEM_TOTAL_SIZE
        #define CONFIG_MEM_TOTAL_SIZE (gd->ram_size)
    #endif
#else
    #define CONFIG_MEM_TOTAL_SIZE (128 * 1024 * 1024)
#endif

static int k230_boot_check_and_get_plain_data(firmware_head_s* pfh, ulong* pplain_addr);

struct k230_ddr_size_tag_st {
    u32 flage;
    u32 shash;
    u32 resver;
    u32 ddr_size;
};

static uint32_t shash_len(const char* s, int len)
{
    uint32_t v = 5381;
    int      i = 0;

    for (i = 0; i < len; i++) {
        v = (v << 5) + v + s[i];
    }
    return v;
}

void k230_boot_write_ddr_size_for_rtsmart(u64 ddr_size)
{
    struct k230_ddr_size_tag_st size_tag;

    size_tag.flage    = 0x5a5a5a5a;
    size_tag.shash    = 0;
    size_tag.resver   = 0;
    size_tag.ddr_size = ddr_size;
    size_tag.shash    = shash_len((const unsigned char*)&size_tag, sizeof(size_tag));

    memcpy(CONFIG_RTSMART_OPENSIB_MEMORY_SIZE - sizeof(size_tag), &size_tag, sizeof(size_tag));
    flush_cache(CONFIG_RTSMART_OPENSIB_MEMORY_SIZE - sizeof(size_tag), sizeof(size_tag));
}

static int k230_boot_reset_big_hard_and_run(ulong core_run_addr)
{
    printf("Jump to big hart\n");

    k230_boot_write_ddr_size_for_rtsmart(gd->ram_size);

    writel(core_run_addr, (void*)0x91102104ULL);
    writel(0x10001000, (void*)0x9110100cULL);
    writel(0x10001, (void*)0x9110100cULL);
    writel(0x10000, (void*)0x9110100cULL);

    return 0;
}

unsigned long k230_get_encrypted_image_load_addr(void)
{
    unsigned long addr = CONFIG_MEM_BASE_ADDR + CONFIG_MEM_TOTAL_SIZE - (CONFIG_MEM_TOTAL_SIZE / 3);
    return addr & ~(4096-1);
}

unsigned long k230_get_encrypted_image_decrypt_addr(void)
{
    unsigned long addr = CONFIG_MEM_BASE_ADDR + CONFIG_MEM_TOTAL_SIZE - ((CONFIG_MEM_TOTAL_SIZE / 3) * 2);
    return addr & ~(4096-1);
}

static int k230_boot_decomp_to_load_addr(image_header_t* pUh, ulong des_len, ulong data, ulong* plen)
{
    int   ret               = 0;
    ulong img_load_addr     = (ulong)image_get_load(pUh);
    int   img_compress_algo = image_get_comp(pUh);

    printf("image: %s load to 0x%lx, compress=%d src=0x%lx len=0x%lx\n", image_get_name(pUh), img_load_addr,
           img_compress_algo, data, *plen);

    if (IH_COMP_GZIP == img_compress_algo) {
        if (0x00 != (ret = gunzip((void*)img_load_addr, des_len, (void*)data, plen))) {
            printf("unzip fialed ret =%x\n", ret);
            return -1;
        }
    } else if (IH_COMP_NONE == img_compress_algo) {
        memmove((void*)img_load_addr, (void*)data, *plen);
    } else {
        printf("Error: Unsupport compress algo.\n");
        return -2;
    }

    flush_cache(img_load_addr, *plen);

    return ret;
}

static int k230_boot_rtt_uimage(image_header_t* pUh)
{
    int   ret  = 0;
    ulong len  = image_get_size(pUh);
    ulong data = image_get_data(pUh);

    image_multi_getimg(pUh, 0, &data, &len);

    if (0x00 == (ret = k230_boot_decomp_to_load_addr(pUh, 0x6000000, data, &len))) {
        k230_boot_reset_big_hard_and_run(image_get_load(pUh));
        while (1) {
            asm volatile("wfi");
        }
    }

    printf("Boot RT-Smart failed. %d\n", ret);

    return ret;
}

static int k230_boot_uboot_uimage(image_header_t* pUh)
{
    void (*uboot)(ulong hart, void* dtb);

    int   ret  = 0;
    ulong len  = image_get_data_size(pUh);
    ulong data = image_get_data(pUh);

    if (0x00 == (ret = k230_boot_decomp_to_load_addr(pUh, 0x6000000, data, &len))) {
        icache_disable();
        dcache_disable();

        asm volatile(".long 0x0170000b\n" ::: "memory");
        uboot = (void (*)(ulong, void*))(ulong)image_get_load(pUh);
        uboot(0, 0);
    }

    return ret;
}

#if defined(CONFIG_MMC)
__weak ulong get_blk_start_by_boot_firmre_type(en_boot_sys_t sys)
{
    ulong blk_s = IMG_PART_NOT_EXIT;

    switch (sys) {
    case BOOT_SYS_RTT:
        blk_s = RTT_SYS_IN_IMG_OFF_SEC;
        break;
    case BOOT_SYS_UBOOT:
        blk_s = UBOOT_SYS_IN_IMG_OFF_SEC;
        break;
    default:
        break;
    }

    return blk_s;
}

static int k230_load_sys_from_mmc_or_sd(en_boot_sys_t sys, ulong buff)
{
    int   ret       = 0;
    ulong data_sect = 0;

    struct mmc*             mmc       = NULL;
    static struct blk_desc* pblk_desc = NULL;

    firmware_head_s* pfh   = (firmware_head_s*)buff;
    ulong            blk_s = get_blk_start_by_boot_firmre_type(sys);

    if (IMG_PART_NOT_EXIT == blk_s) {
        return IMG_PART_NOT_EXIT;
    }

    if (NULL == pblk_desc) {
        if (mmc_init_device(g_boot_medium - BOOT_MEDIUM_SDIO0)) {
            return 1;
        }

        mmc = find_mmc_device(g_boot_medium - BOOT_MEDIUM_SDIO0);

        if (NULL == mmc) {
            return 2;
        }

        if (mmc_init(mmc)) {
            return 3;
        }

        pblk_desc = mmc_get_blk_desc(mmc);
        if (NULL == pblk_desc) {
            return 3;
        }
    }

    ret = blk_dread(pblk_desc, blk_s, HD_BLK_NUM, (char*)buff);
    if (ret != HD_BLK_NUM) {
        return 4;
    }

    if (pfh->magic != K230_IMAGE_MAGIC_NUM) {
        printf("pfh->magic 0x%x != 0x%x blk=0x%lx buff=0x%lx  ", pfh->magic, K230_IMAGE_MAGIC_NUM, blk_s, buff);
        return 5;
    }

    data_sect = DIV_ROUND_UP(pfh->length + sizeof(*pfh), BLKSZ) - HD_BLK_NUM;

    ret = blk_dread(pblk_desc, blk_s + HD_BLK_NUM, data_sect, (char*)buff + HD_BLK_NUM * BLKSZ);

    if (ret != data_sect) {
        return 6;
    }

    return 0;
}
#endif // CONFIG_MMC

#if defined(CONFIG_MTD_SPI_NAND)

#define SPINAND_NAME "spi-nand0"

__weak ulong get_nand_start_by_boot_firmre_type(en_boot_sys_t sys)
{
    ulong blk_s = IMG_PART_NOT_EXIT;
    switch (sys) {
    case BOOT_SYS_RTT:
        blk_s = RTT_SYS_IN_SPI_NAND_OFF;
        break;
    case BOOT_SYS_UBOOT:
        blk_s = UBOOT_SYS_IN_SPI_NAND_OFF;
        break;
    default:
        break;
    }
    return blk_s;
}

static struct mtd_info* get_mtd_by_name(const char* name)
{
    struct mtd_info* mtd;

    mtd_probe_devices();

    mtd = get_mtd_device_nm(name);
    if (IS_ERR_OR_NULL(mtd)) {
        printf("MTD device %s not found, ret %ld\n", name, PTR_ERR(mtd));
    }

    return mtd;
}

static inline bool mtd_is_aligned_with_block_size(struct mtd_info* mtd, u64 size)
{
    return !do_div(size, mtd->erasesize);
}

static int k230_load_sys_from_spi_nand(en_boot_sys_t sys, ulong buff)
{
    int ret = 0;

    u_char*          buf           = (u_char*)buff;
    firmware_head_s* pfh           = (firmware_head_s*)buff;
    size_t           len           = sizeof(*pfh);
    size_t           blocksize     = 0;
    size_t           amount_loaded = 0;
    size_t           end           = 0;
    ulong            off           = 0;

    static struct mtd_info* mtd;
    struct mtd_oob_ops      io_op = {};

    if (IMG_PART_NOT_EXIT == off) {
        return IMG_PART_NOT_EXIT;
    }

    mtd = get_mtd_by_name(SPINAND_NAME);
    if (IS_ERR_OR_NULL(mtd)) {
        printf("k230_load_sys_from_spi_nand error\n");
        return 1;
    }
    blocksize = mtd->erasesize;

    off = get_nand_start_by_boot_firmre_type(sys);
    end = off + len;

    io_op.mode   = MTD_OPS_AUTO_OOB;
    io_op.len    = mtd->writesize;
    io_op.ooblen = 0;
    io_op.oobbuf = NULL;

    amount_loaded = 0;
    while (off < end) {
        if (mtd_is_aligned_with_block_size(mtd, off) && mtd_block_isbad(mtd, off)) {
            off += blocksize;
        } else {
            io_op.datbuf = &buf[amount_loaded];
            if (mtd_read_oob(mtd, off, &io_op)) {
                printf("read firmware head error\n");
                ret = 2;
                goto out_put_mtd;
            }

            off += io_op.retlen;
            amount_loaded += io_op.retlen;
        }
    }

    if (K230_IMAGE_MAGIC_NUM != pfh->magic) {
        printf("pfh->magic 0x%x != 0x%x off=0x%lx buff=0x%lx ", pfh->magic, K230_IMAGE_MAGIC_NUM, off, buff);
        ret = 5;
        goto out_put_mtd;
    }

    ret = 6;
    if (pfh->length > (mtd->writesize - sizeof(*pfh))) {
        end = off + pfh->length - (mtd->writesize - sizeof(*pfh));
        while (off < end) {
            if (mtd_is_aligned_with_block_size(mtd, off) && mtd_block_isbad(mtd, off)) {
                off += blocksize;
            } else {
                io_op.datbuf = &buf[amount_loaded];
                if (mtd_read_oob(mtd, off, &io_op)) {
                    printf("read firmware error\n");
                    goto out_put_mtd;
                }

                off += io_op.retlen;
                amount_loaded += io_op.retlen;
            }
        }
    }

#if 0
    printf("firmware header:\n");
    for (size_t i = 0; i < sizeof(*pfh); i++) {
        printf("%02x ", buf[i]);

        if (15 == (i % 16)) {
            printf("\n");
        }
    }
    printf("\n");
#endif

    ret = 0;

out_put_mtd:
    put_mtd_device(mtd);

    return ret;
}
#endif

static int k230_img_load_sys_from_dev(en_boot_sys_t sys, ulong buff)
{
    int ret = 0;

#if defined(CONFIG_MMC)
    if ((BOOT_MEDIUM_SDIO0 == g_boot_medium) || (BOOT_MEDIUM_SDIO1 == g_boot_medium)) {
        ret = k230_load_sys_from_mmc_or_sd(sys, buff);
    } else
#endif // CONFIG_MMC

#if defined(CONFIG_MTD_SPI_NAND)
        if (g_boot_medium == BOOT_MEDIUM_NANDFLASH) {
        ret = k230_load_sys_from_spi_nand(sys, buff);
    } else
#endif // CONFIG_MTD_SPI_NAND

    {
        ret = -1;
        printf("Error, unsupport media type %d\n", g_boot_medium);
    }

    return ret;
}

static int k230_img_load_boot_sys_auot_boot(en_boot_sys_t sys)
{
    int ret = 0;

    if (0x00 != (ret = k230_img_load_boot_sys(BOOT_SYS_RTT))) {
        printf("Error, Autoboot RT-Smart failed. %d\n", ret);
    }

    return ret;
}

int k230_img_boot_sys_bin(firmware_head_s* fhBUff)
{
    int ret = 0;

    image_header_t* pUh        = NULL;
    const char*     image_name = NULL;
    ulong           plain_addr = 0;

    ret = k230_boot_check_and_get_plain_data((firmware_head_s*)fhBUff, &plain_addr);
    if (ret) {
        printf("decrypt image failed.");
        return ret;
    }

    pUh = (image_header_t*)(plain_addr + 4);
    if (!image_check_magic(pUh)) {
        printf("bad magic \n");
        return -3;
    }

    image_name = image_get_name(pUh);

    if (0x00 == strcmp(image_name, "rtt")) {
        ret = k230_boot_rtt_uimage(pUh);
    } else if (0x00 == strcmp(image_name, "uboot")) {
        ret = k230_boot_uboot_uimage(pUh);
    } else {
        printf("Error, Unsupport image type %s\n", image_name);
        return -4;
    }

    return ret;
}

int k230_img_load_boot_sys(en_boot_sys_t sys)
{
    int   ret           = 0;
    ulong img_load_addr = k230_get_encrypted_image_load_addr();

    if (sys == BOOT_SYS_AUTO) {
        ret = k230_img_load_boot_sys_auot_boot(sys);
    } else {
        if (0x00 == (ret = k230_img_load_sys_from_dev(sys, img_load_addr))) {
            if (0x00 != (ret = k230_img_boot_sys_bin((firmware_head_s*)img_load_addr))) {
                printf("Error, boot image failed.%d\n", ret);
            }
        } else {
            printf("Error, load image failed.%d\n", ret);
        }
    }

    return ret;
}

static int k230_boot_check_and_get_plain_data(firmware_head_s* pfh, ulong* pplain_addr)
{
    uint8_t sha256[SHA256_SUM_LEN];

    if (K230_IMAGE_MAGIC_NUM != pfh->magic) {
        printf("magic error 0x%08X != 0x%08X \n", K230_IMAGE_MAGIC_NUM, pfh->magic);
        return 1;
    }

    if (NONE_SECURITY == pfh->crypto_type) {
        sha256_csum_wd((const uint8_t*)(pfh + 1), pfh->length, sha256, CHUNKSZ_SHA256);

        if (memcmp(sha256, pfh->verify.none_sec.signature, SHA256_SUM_LEN)) {
            printf("sha256 error");
            return 2;
        }

        if (pplain_addr) {
            *pplain_addr = (ulong)pfh + sizeof(*pfh);
        }

#if 0
        printf("calc sha256:");
        for (int i = 0; i < SHA256_SUM_LEN; i++) {
            printf("%02X", sha256[i]);
        }
        printf("\nfirmware sha256:");
        for (int i = 0; i < SHA256_SUM_LEN; i++) {
            printf("%02X", pfh->verify.none_sec.signature[i]);
        }
        printf("\n");
#endif

        return 0;
    } else if ((CHINESE_SECURITY == pfh->crypto_type) || (INTERNATIONAL_SECURITY == pfh->crypto_type)
               || (GCM_ONLY == pfh->crypto_type)) {
        printf("error, not support encrypted firmware\n");
        return 3;
    } else {
        printf("error crypto type =0x%x\n", pfh->crypto_type);
        return 4;
    }

    // never reach
    return -1;
}
