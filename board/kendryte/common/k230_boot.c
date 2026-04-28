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

#include <u-boot/crc.h>

#include "board_common.h"
#include "secure_boot_config_autogen.h"

#include "k230_atag.h"

#include "kendryte/pufs/pufs_ecp/pufs_ecp.h"
#include "kendryte/pufs/pufs_hmac/pufs_hmac.h"
#include "kendryte/pufs/pufs_rt/pufs_rt.h"
#include "kendryte/pufs/pufs_sm2/pufs_sm2.h"
#include "kendryte/pufs/pufs_sp38a/pufs_sp38a.h"
#include "kendryte/pufs/pufs_sp38d/pufs_sp38d.h"

/* TOC (Table of Contents) 定义 */
#define K230_TOC_OFFSET        0xe0000
#define K230_TOC_SECTOR        (K230_TOC_OFFSET / 512)
#define K230_TOC_MAX_ENTRIES   16
#define K230_TOC_ENTRY_SIZE    64

#define OTA_META_MAGIC         0x4f544156u  /* 'OTAV' */

#define K230_BOOT_CORE_SHIFT        (0x1)
#define K230_BOOT_CORE_MASK         (0x3 << K230_BOOT_CORE_SHIFT) 
#define K230_BOOT_FLAG_SHIFT        (0x0)
#define K230_BOOT_FLAG_MASK         (0x1 << K230_BOOT_FLAG_SHIFT) 

struct k230_toc_entry {
    char name[32];
    uint64_t offset;
    uint64_t size;
    uint8_t load;
    uint8_t boot;
    uint8_t pading[10];
    uint32_t load_addr;
} __attribute__((packed, aligned(64)));

struct k230_toc {
	uint32_t entry_count;
	struct k230_toc_entry entries[K230_TOC_MAX_ENTRIES];
};

static struct k230_toc toc;
static struct blk_desc *pblk_desc;
static uint64_t rtapp_load_addr, rtapp_size, rttapp_loaded = 0;

#define K230_DISABLE_NONE_SECURITY_MASK	0x1U
#define K230_GCM_IV_LEN			12U
#define K230_GCM_TAG_LEN		16U
#define K230_SM4_IV_LEN			16U
#define K230_GCM_UPDATE_CHUNK_SIZE	0x10000U
#define K230_DOWNSTREAM_AES_KEY_SLOT	OTPKEY_3
#define K230_DOWNSTREAM_RSA_HASH_SLOT	OTPKEY_8
#define K230_DOWNSTREAM_SM4_KEY_SLOT	OTPKEY_5
#define K230_DOWNSTREAM_SM2_HASH_SLOT	OTPKEY_9

struct ota_slot_meta {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    uint32_t reserved;
    uint8_t  padding[512 - 16];
} __attribute__((packed));

unsigned long k230_get_encrypted_image_load_addr(void)
{
    unsigned long addr = g_dram_base + g_dram_size - ((g_dram_size / 3) * 2);
    return addr & ~(4096-1);
}

unsigned long k230_get_encrypted_image_decrypt_addr(void)
{
	unsigned long addr = g_dram_base + (g_dram_size / 2);

	return addr & ~(4096 - 1);
}

unsigned long k230_get_rttapp_load_addr(void)
{
    unsigned long addr = g_dram_base + g_dram_size - (g_dram_size / 3);
    return addr & ~(4096-1);
}

static bool k230_calc_range_end(ulong start, ulong size, ulong *end)
{
    ulong max_value = ~0UL;

    if (end == NULL)
        return false;

    if (size > (max_value - start))
        return false;

    *end = start + size;
    return true;
}

static bool k230_ranges_overlap(ulong start_a, ulong size_a,
                ulong start_b, ulong size_b)
{
    ulong end_a;
    ulong end_b;

    if ((size_a == 0) || (size_b == 0))
        return false;

    if (!k230_calc_range_end(start_a, size_a, &end_a) ||
        !k230_calc_range_end(start_b, size_b, &end_b))
        return true;

    return (start_a < end_b) && (start_b < end_a);
}

static ulong k230_get_encrypted_image_buffer_size(void)
{
    ulong load_addr = k230_get_encrypted_image_load_addr();
    ulong decrypt_addr = k230_get_encrypted_image_decrypt_addr();

    if (decrypt_addr <= load_addr)
        return 0;

    return decrypt_addr - load_addr;
}

static int k230_get_firmware_payload_limit(const char *toc_name,
                       uint64_t partition_size,
                       ulong *payload_limit)
{
    ulong buffer_size;

    if (payload_limit == NULL)
        return -1;

    buffer_size = k230_get_encrypted_image_buffer_size();
    if ((partition_size <= sizeof(firmware_head_s)) ||
        (buffer_size <= sizeof(firmware_head_s))) {
        printf("%s: invalid bounds for %s part=0x%llx buf=0x%lx\n",
               __func__, toc_name, partition_size, buffer_size);
        return -1;
    }

    *payload_limit = min_t(ulong,
                   (ulong)(partition_size - sizeof(firmware_head_s)),
                   buffer_size - sizeof(firmware_head_s));
    return 0;
}

static int k230_validate_image_layout(image_header_t *pUh, ulong src_data,
                      ulong src_len, ulong dst_len)
{
    ulong dram_end;
    ulong img_load_addr;
    ulong img_end_addr;

    if (pUh == NULL)
        return -1;

    img_load_addr = (ulong)image_get_load(pUh);

    if (!k230_calc_range_end(g_dram_base, g_dram_size, &dram_end) ||
        !k230_calc_range_end(img_load_addr, dst_len, &img_end_addr)) {
        printf("%s: address range overflow for %s\n", __func__, image_get_name(pUh));
        return -1;
    }

    if ((img_load_addr < g_dram_base) || (img_end_addr > dram_end)) {
        printf("%s: %s load range [0x%lx, 0x%lx) exceeds dram [0x%llx, 0x%lx)\n",
               __func__, image_get_name(pUh), img_load_addr, img_end_addr,
               g_dram_base, dram_end);
        return -1;
    }

    if ((image_get_comp(pUh) == IH_COMP_GZIP) &&
        k230_ranges_overlap(img_load_addr, dst_len, src_data, src_len)) {
        printf("%s: %s load range [0x%lx, 0x%lx) overlaps compressed source [0x%lx, 0x%lx)\n",
               __func__, image_get_name(pUh), img_load_addr, img_end_addr,
               src_data, src_data + src_len);
        return -1;
    }

    return 0;
}

static int k230_boot_decomp_to_load_addr(image_header_t* pUh, ulong des_len, ulong data, ulong* plen)
{
    int   ret               = 0;
    ulong img_load_addr     = (ulong)image_get_load(pUh);
    int   img_compress_algo = image_get_comp(pUh);

    // printf("image: %s load to 0x%lx, compress=%d src=0x%lx len=0x%lx\n", image_get_name(pUh), img_load_addr,
    //        img_compress_algo, data, *plen);

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

static int k230_boot_check_and_get_plain_data(firmware_head_s *pfh,
                      const char *toc_name,
                      ulong *pplain_addr)
{
    pufs_dgst_st md;
    ulong plain_addr = 0;
    uint32_t outlen = 0;
    uint32_t otp_msc = 0;
    uint32_t otp_firmware_version = 0;
    uint32_t cur_firmware_version = 0;
    const uint8_t *cipher_data = (const uint8_t *)(pfh + 1);

    if (K230_IMAGE_MAGIC_NUM != pfh->magic) {
        printf("magic error 0x%08X != 0x%08X \n", K230_IMAGE_MAGIC_NUM, pfh->magic);
        return 1;
    }

    if (pufs_read_otp((uint8_t *)&otp_msc, OTP_BLOCK_PRODUCT_MISC_BYTES,
              OTP_BLOCK_PRODUCT_MISC_ADDR) != SUCCESS) {
        printf("otp product misc read error\n");
        return 5;
    }

    if ((otp_msc & K230_DISABLE_NONE_SECURITY_MASK) &&
        (pfh->crypto_type == NONE_SECURITY)) {
        printf("none security disabled by otp\n");
        return 6;
    }

    if (NONE_SECURITY == pfh->crypto_type) {
        if (SUCCESS != cb_pufs_hash(&md, cipher_data, pfh->length, SHA_256)) {
            printf("sha256 error\n");
            return 2;
        }

        if (memcmp(md.dgst, pfh->verify.none_sec.signature, SHA256_SUM_LEN)) {
            printf("sha256 error");
            return 2;
        }

        plain_addr = (ulong)cipher_data;
    } else if (INTERNATIONAL_SECURITY == pfh->crypto_type) {
        uint8_t puk_hash_otp[SHA256_SUM_LEN];
        const uint8_t *gcm_tag;
        const uint8_t *gcm_iv;
        uint32_t final_outlen = 0;
        uint32_t total_outlen = 0;
        uint32_t remaining_len;
        const uint8_t *gcm_input;
        uint8_t *gcm_output;

        if (pufs_read_otp(puk_hash_otp, sizeof(puk_hash_otp),
                  (K230_DOWNSTREAM_RSA_HASH_SLOT - OTPKEY_0) * OTP_KEY_LEN) != SUCCESS) {
            printf("otp rsa hash read error\n");
            return 7;
        }

        if (SUCCESS != cb_pufs_hash(&md, (const uint8_t *)&pfh->verify, 256 + 4, SHA_256)) {
            printf("rsa pubkey hash error\n");
            return 8;
        }

        if (memcmp(md.dgst, puk_hash_otp, sizeof(puk_hash_otp))) {
            printf("rsa pubkey hash mismatch\n");
            return 9;
        }

        if (pfh->length < (K230_GCM_IV_LEN + K230_GCM_TAG_LEN)) {
            printf("gcm payload too short\n");
            return 10;
        }

        gcm_iv = cipher_data;
        gcm_input = cipher_data + K230_GCM_IV_LEN;
        remaining_len = pfh->length - K230_GCM_IV_LEN - K230_GCM_TAG_LEN;
        gcm_tag = cipher_data + pfh->length - K230_GCM_TAG_LEN;
        if (cb_pufs_rsa_p1v15_verify(pfh->verify.rsa.signature,
                         RSA2048,
                         pfh->verify.rsa.n,
                         pfh->verify.rsa.e,
                         gcm_tag,
                         K230_GCM_TAG_LEN) != SUCCESS) {
            printf("rsa signature verify error\n");
            return 11;
        }

        plain_addr = k230_get_encrypted_image_decrypt_addr();
        if (cb_pufs_dec_gcm_init(AES, OTPKEY, K230_DOWNSTREAM_AES_KEY_SLOT,
                     256, gcm_iv, 12) != SUCCESS) {
            printf("gcm init error\n");
            return 12;
        }

        if (cb_pufs_dec_gcm_update(NULL, NULL, NULL, 0) != SUCCESS) {
            printf("gcm aad update error\n");
            return 12;
        }

        gcm_output = (uint8_t *)plain_addr;
        while (remaining_len > 0) {
            uint32_t chunk_len = remaining_len;

            if (chunk_len > K230_GCM_UPDATE_CHUNK_SIZE)
                chunk_len = K230_GCM_UPDATE_CHUNK_SIZE;

            if (cb_pufs_dec_gcm_update(gcm_output, &outlen,
                           gcm_input, chunk_len) != SUCCESS) {
                printf("gcm data update error off=0x%x len=0x%x\n",
                       total_outlen, chunk_len);
                return 12;
            }

            total_outlen += outlen;
            gcm_input += chunk_len;
            gcm_output += outlen;
            remaining_len -= chunk_len;
        }

        outlen = total_outlen;

        if (cb_pufs_dec_gcm_final(gcm_output, &final_outlen, gcm_tag,
                      K230_GCM_TAG_LEN) != SUCCESS) {
            printf("gcm final error\n");
            return 12;
        }

        outlen = total_outlen + final_outlen;
    } else if (CHINESE_SECURITY == pfh->crypto_type) {
        uint8_t puk_hash_otp[SHA256_SUM_LEN];
        pufs_ec_point_st puk = { .qlen = 32 };
        pufs_ecdsa_sig_st sig = { .qlen = 32 };
        const uint8_t *sm4_iv;
        const uint8_t *sm4_input;
        uint32_t sm4_input_len;

        if (pufs_read_otp(puk_hash_otp, sizeof(puk_hash_otp),
                  (K230_DOWNSTREAM_SM2_HASH_SLOT - OTPKEY_0) * OTP_KEY_LEN) != SUCCESS) {
            printf("otp sm2 hash read error\n");
            return 13;
        }

        if (SUCCESS != cb_pufs_hash(&md, (const uint8_t *)&pfh->verify,
                    sizeof(pfh->verify.sm2) - sizeof(pfh->verify.sm2.r) - sizeof(pfh->verify.sm2.s),
                    SM3)) {
            printf("sm2 pubkey hash error\n");
            return 14;
        }

        if (memcmp(md.dgst, puk_hash_otp, sizeof(puk_hash_otp))) {
            printf("sm2 pubkey hash mismatch\n");
            return 15;
        }

        memcpy(puk.x, pfh->verify.sm2.pukx, puk.qlen);
        memcpy(puk.y, pfh->verify.sm2.puky, puk.qlen);
        memcpy(sig.r, pfh->verify.sm2.r, sig.qlen);
        memcpy(sig.s, pfh->verify.sm2.s, sig.qlen);

        if (cb_pufs_sm2_verify(sig,
                      cipher_data,
                      pfh->length,
                      pfh->verify.sm2.id,
                      pfh->verify.sm2.idlen,
                      puk) != SUCCESS) {
            printf("sm2 signature verify error\n");
            return 16;
        }

        if (pfh->length < K230_SM4_IV_LEN) {
            printf("sm4 payload too short\n");
            return 17;
        }

        sm4_iv = cipher_data;
        sm4_input = cipher_data + K230_SM4_IV_LEN;
        sm4_input_len = pfh->length - K230_SM4_IV_LEN;

        plain_addr = k230_get_encrypted_image_decrypt_addr();
        if (cb_pufs_dec_cbc((uint8_t *)plain_addr, &outlen,
                    sm4_input, sm4_input_len,
                    SM4, OTPKEY, K230_DOWNSTREAM_SM4_KEY_SLOT, 128,
                    sm4_iv, 0) != SUCCESS) {
            printf("sm4 decrypt error\n");
            return 17;
        }
    } else {
        printf("error crypto type =0x%x\n", pfh->crypto_type);
        return 4;
    }

    if (pufs_read_otp((uint8_t *)&otp_firmware_version, OTP_BLOCK_VERSION_BYTES,
              OTP_BLOCK_VERSION_ADDR) != SUCCESS) {
        printf("otp version read error\n");
        return 18;
    }

    cur_firmware_version = *(uint32_t *)plain_addr;
    if (cur_firmware_version < otp_firmware_version) {
        printf("firmware rollback detected cur=0x%x otp=0x%x\n",
               cur_firmware_version, otp_firmware_version);
        return 19;
    }

    if (pplain_addr)
        *pplain_addr = plain_addr;

    return 0;
}

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

#endif

#if defined(CONFIG_MMC)
static int k230_mmc_init(void)
{
    int ret = 0;

    if (!pblk_desc) {
        struct mmc *mmc = NULL;

        if (mmc_init_device(g_boot_medium - BOOT_MEDIUM_SDIO0)) {
            ret = -1;
            goto out;
        }

        mmc = find_mmc_device(g_boot_medium - BOOT_MEDIUM_SDIO0);

        if (NULL == mmc) {
            ret = -2;
            goto out;
        }

        if (mmc_init(mmc)) {
            ret = -3;
            goto out;
        }

        pblk_desc = mmc_get_blk_desc(mmc);
        if (NULL == pblk_desc) {
            ret = -4;
            goto out;
        }
    }

out:
    return ret;
}
#endif

static void *k230_read_toc(void)
{
    int ret = 0;
    ulong blk_count;
    void *toc_buf;

    memset(&toc, 0, sizeof(toc));

    if (K230_TOC_ENTRY_SIZE != sizeof(struct k230_toc_entry)) {
        printf("%s: struct k230_toc_entry(%lu) must aligned to 64B\n",
               __func__, sizeof(struct k230_toc_entry));
        ret = -1;
        goto out;
    }
    toc_buf = (void *)k230_get_encrypted_image_load_addr();

#if defined(CONFIG_MMC)
    if ((BOOT_MEDIUM_SDIO0 == g_boot_medium) || (BOOT_MEDIUM_SDIO1 == g_boot_medium)) {
        ret = k230_mmc_init();
        if (0 != ret) {
            printf("%s: k230_mmc_init fail: %d\n", __func__, ret);
            goto out;
        }

        blk_count = (K230_TOC_MAX_ENTRIES * K230_TOC_ENTRY_SIZE + 511) / 512;

        if (blk_dread(pblk_desc, K230_TOC_SECTOR, blk_count, toc_buf) != blk_count) {
            printf("%s: Error: Failed to read TOC from MMC\n", __func__);
            ret = -1;
            goto out;
        }
    } else
#endif

#if defined(CONFIG_MTD_SPI_NAND)
    if (g_boot_medium == BOOT_MEDIUM_NANDFLASH) {
        static struct mtd_info *mtd;
        struct mtd_oob_ops io_op = {};
        size_t len = K230_TOC_MAX_ENTRIES * K230_TOC_ENTRY_SIZE;
        size_t blocksize = 0;
        size_t amount_loaded = 0;
        size_t end = 0;
        ulong off = 0;
        u_char* buf = (u_char*)toc_buf;

        mtd = get_mtd_by_name(SPINAND_NAME);
        if (IS_ERR_OR_NULL(mtd)) {
            printf("k230_load_sys_from_spi_nand error\n");
            ret = -1;
            goto out;

        }
        blocksize = mtd->erasesize;
        off = K230_TOC_OFFSET;
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
                    printf("%s: read firmware head error\n", __func__);
                    ret = -1;
                    put_mtd_device(mtd);
                    goto out;
                }

                off += io_op.retlen;
                amount_loaded += io_op.retlen;
            }
        }
    } else
#endif
    {
        ret = -1;
        printf("%s: Error, unsupport media type %d\n", __func__, g_boot_medium);
    }

out:
    if (0 != ret) {
        return NULL;
    }

    return toc_buf;
}

#if 0
static void dump_toc_info(void)
{
    printf("\n");
    printf("=============== K230 TOC (Table of Contents) ===============\n");
    printf("TOC Location: 0x%08x (sector %u)\n", K230_TOC_OFFSET, K230_TOC_SECTOR);
    printf("Total Entries: %u\n", toc.entry_count);
    printf("------------------------------------------------------------\n");
    printf("%-16s %-12s %-12s %-6s %-6s\n",
           "Name", "Offset", "Size", "Load", "Boot");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < toc.entry_count; i++) {
        struct k230_toc_entry *entry = &toc.entries[i];

        printf("%-16s  0x%08lx    0x%08lx %-6s   0x%x\n",
               entry->name,
               entry->offset,
               entry->size,
               entry->load ? "YES" : "NO",
               entry->boot);
    }

    printf("============================================================\n");
    printf("\n");
}
#endif

static int k230_parse_toc(void *toc_buf)
{
    struct k230_toc_entry *src = toc_buf;

	toc.entry_count = 0;
	for (int i = 0; i < K230_TOC_MAX_ENTRIES; i++) {
        struct k230_toc_entry *dst = &toc.entries[toc.entry_count];

		if (src->name[0] == '\0') {
			break;
		}

		memcpy(dst, src, sizeof(struct k230_toc_entry));

		toc.entry_count++;
        src ++;
	}

#if 0
    dump_toc_info();
#endif

	return (toc.entry_count > 0) ? 0 : -7;
}

static int k230_read_meta_block(ulong offset, void *buf)
{
#if defined(CONFIG_MMC)
    if ((BOOT_MEDIUM_SDIO0 == g_boot_medium) ||
        (BOOT_MEDIUM_SDIO1 == g_boot_medium)) {
        lbaint_t sector;
        lbaint_t cnt = 1;

        sector = offset / pblk_desc->blksz;
        if (blk_dread(pblk_desc, sector, cnt, buf) != cnt) {
            printf("%s: read meta block fail, off=0x%lx\n",
                   __func__, offset);
            return -1;
        }

        return 0;
    }
#endif

    return -1;
}

static ulong k230_get_ota_meta_offset(char slot)
{
    ulong base = 0;
    ulong span = 0;

    for (int i = 0; i < toc.entry_count; i++) {
        struct k230_toc_entry *e = &toc.entries[i];

        if (!strncmp(e->name, "ota_meta", sizeof(e->name))) {
            base = (ulong)e->offset;
            span = (ulong)e->size / 2;
            break;
        }
    }

    if (!base) {
        printf("K230 boot: no 'ota_meta' entry in TOC\n");
        return 0;
    }

    if (!span) {
        printf("K230 boot: ota_meta partition size invalid\n");
        return 0;
    }

    if (slot == 'B')
        base += span;

    return base;
}

static int k230_meta_read_slot(char slot, uint32_t *pver, bool *pvalid)
{
    struct ota_slot_meta *meta;
    uint32_t crc_cal, crc_img;
    ulong off;
    int ret = 0;

    if ((NULL == pver) || (NULL == pvalid)) {
        ret = -1;
        printf("invalid para\n");
        goto out;
    }

    off = k230_get_ota_meta_offset(slot);
    if (!off) {
        ret = -1;
        goto out;
    }

    meta = (struct ota_slot_meta *)k230_get_encrypted_image_load_addr();
    ret = k230_read_meta_block(off, meta);
    if (ret) {
        goto out;
    }

    if (meta->magic != OTA_META_MAGIC) {
        ret = -1;
        goto out;
    }

    crc_img = meta->crc32;
    meta->crc32 = 0;
    crc_cal = crc32(0, (const unsigned char *)meta, sizeof(*meta));
    if (crc_cal != crc_img) {
        printf("WARN: meta slot %c crc mismatch: calc=0x%08x img=0x%08x\n",
               slot, crc_cal, crc_img);
        ret = -1;
        goto out;
    }

    *pver = meta->version;
    *pvalid = true;

out:
    return ret;
}

static char k230_select_boot_slot(void)
{
    uint32_t ver_a = 0, ver_b = 0;
    bool valid_a = false, valid_b = false;
    char active = 'A';

    if (k230_meta_read_slot('A', &ver_a, &valid_a))
        valid_a = 0;
    if (k230_meta_read_slot('B', &ver_b, &valid_b))
        valid_b = 0;

    if (!valid_a && !valid_b) {
        goto out;
    }

    if (valid_a && !valid_b) {
        active = 'A';
    } else if (!valid_a && valid_b) {
        active = 'B';
    } else {
        if (ver_a == ver_b)
            active = 'A';
        else if (ver_a > ver_b)
            active = 'A';
        else
            active = 'B';

        if ((ver_a > ver_b ? (ver_a - ver_b) : (ver_b - ver_a)) > 1) {
            printf("WARN: slot version gap too large: A=0x%x B=0x%x\n",
                   ver_a, ver_b);
        }
    }

out:
    return active;
}

static uint get_gunzip_dest_length(const u8 *zipped_data, uint zipped_len)
{
    if (zipped_data == NULL || zipped_len < 4) {
        return 0;
    }
    
    uint original_len;
    memcpy(&original_len, zipped_data + zipped_len - 4, 4);
    
    return original_len;
}

static uint _k230_load_img(const char *toc_name, uint64_t offset,
               uint64_t partition_size)
{
    int ret = 0;
    ulong src_len, src_data, dst_len, plain_addr = 0;
    ulong max_payload_len = 0;
    image_header_t *pUh = NULL;
    ulong buff = k230_get_encrypted_image_load_addr();
    firmware_head_s *pfh = (firmware_head_s*)buff;
    bool is_rtapp = false;

    ret = k230_get_firmware_payload_limit(toc_name, partition_size,
                          &max_payload_len);
    if (ret) {
        ret = -1;
        goto out;
    }

#if defined(CONFIG_MMC)
    if ((BOOT_MEDIUM_SDIO0 == g_boot_medium) || (BOOT_MEDIUM_SDIO1 == g_boot_medium)) {
        ulong blk_s = offset / BLKSZ;
        ulong data_sect = 0;

        if (IMG_PART_NOT_EXIT == blk_s) {
            printf("%s: invalid blk num\n", __func__);
            ret = -1;
            goto out;
        }

        if (NULL == pblk_desc) {
            printf("%s: mmc is not init\n", __func__);
            ret = -1;
            goto out;
        }

    	ret = blk_dread(pblk_desc, blk_s, HD_BLK_NUM, (char*)buff);
    	if (ret != HD_BLK_NUM) {
    	    printf("%s: blk_dread fail: %d\n", __func__, ret);
    	    ret = -1;
    	    goto out;
    	}

        if (pfh->magic != K230_IMAGE_MAGIC_NUM) {
            printf("%s: pfh->magic 0x%x != 0x%x blk=0x%lx buff=0x%lx\n",
                   __func__, pfh->magic, K230_IMAGE_MAGIC_NUM, blk_s, buff);
            ret = -1;
            goto out;
        }

        if (pfh->length > max_payload_len) {
            printf("%s: %s payload 0x%x exceeds limit 0x%lx (part=0x%llx)\n",
                   __func__, toc_name, pfh->length, max_payload_len,
                   partition_size);
            ret = -1;
            goto out;
        }

        data_sect = DIV_ROUND_UP(pfh->length + sizeof(*pfh), BLKSZ) - HD_BLK_NUM;
        ret = blk_dread(pblk_desc, blk_s + HD_BLK_NUM, data_sect, (char*)buff + HD_BLK_NUM * BLKSZ);
        if (ret != data_sect) {
            printf("%s: blk_dread failed: %d\n", __func__, ret);
            ret = -1;
            goto out;
        }

    } else
#endif
#if defined(CONFIG_MTD_SPI_NAND)
    if (g_boot_medium == BOOT_MEDIUM_NANDFLASH) {
        u_char*          buf           = (u_char*)buff;
        size_t           blocksize     = 0;
        size_t           amount_loaded = 0;
        size_t           end           = 0;
        ulong            off           = offset;

        static struct mtd_info* mtd;
        struct mtd_oob_ops      io_op = {};

        if (IMG_PART_NOT_EXIT == off) {
            return IMG_PART_NOT_EXIT;
        }

        mtd = get_mtd_by_name(SPINAND_NAME);
        if (IS_ERR_OR_NULL(mtd)) {
            printf("%s: get_mtd_by_name fail\n", __func__);
            ret = -1;
            goto out;
        }

        blocksize = mtd->erasesize;

        end = off + sizeof(*pfh);

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
                    printf("%s: read firmware head error\n", __func__);
                    put_mtd_device(mtd);
                    ret = -1;
                    goto out;
                }

                off += io_op.retlen;
                amount_loaded += io_op.retlen;
            }
        }

        if (K230_IMAGE_MAGIC_NUM != pfh->magic) {
            printf("%s: pfh->magic 0x%x != 0x%x off=0x%lx buff=0x%lx ",
                   __func__, pfh->magic, K230_IMAGE_MAGIC_NUM, off, buff);
            put_mtd_device(mtd);
            ret = -1;
            goto out;
        }

        if (pfh->length > max_payload_len) {
            printf("%s: %s payload 0x%x exceeds limit 0x%lx (part=0x%llx)\n",
                   __func__, toc_name, pfh->length, max_payload_len,
                   partition_size);
            put_mtd_device(mtd);
            ret = -1;
            goto out;
        }

        if (pfh->length > (mtd->writesize - sizeof(*pfh))) {
            end = off + pfh->length - (mtd->writesize - sizeof(*pfh));
            while (off < end) {
                if (mtd_is_aligned_with_block_size(mtd, off) && mtd_block_isbad(mtd, off)) {
                    off += blocksize;
                } else {
                    io_op.datbuf = &buf[amount_loaded];
                    if (mtd_read_oob(mtd, off, &io_op)) {
                        printf("%s: read firmware error\n", __func__);
                        put_mtd_device(mtd);
                        ret = -1;
                        goto out;
                    }

                    off += io_op.retlen;
                    amount_loaded += io_op.retlen;
                }
            }
        }
    } else
#endif
    {
        ret = -1;
        printf("%s: Error, unsupport media type %d\n", __func__, g_boot_medium);
        goto out;
    }

    ret = k230_boot_check_and_get_plain_data((firmware_head_s*)buff, toc_name, &plain_addr);
    if (ret) {
        printf("%s: decrypt image failed: %d\n", __func__, ret);
        ret = -1;
        goto out;
    }

    pUh = (image_header_t*)(plain_addr + 4);
    if (!image_check_magic(pUh)) {
        printf("%s: bad magic\n", __func__);
        ret = -1;
        goto out;
    }

    src_len  = image_get_size(pUh);
    src_data = image_get_data(pUh);

    if (IH_TYPE_MULTI == image_get_type(pUh)) {
        image_multi_getimg(pUh, 0, &src_data, &src_len);
    }

    if (image_get_comp(pUh) == IH_COMP_GZIP) {
        dst_len = get_gunzip_dest_length((u8 *)src_data, src_len);
        if (0 == dst_len) {
            dst_len = 0x6000000;
        }
    } else {
        dst_len = src_len;
    }

    is_rtapp = (strncmp(image_get_name(pUh), "rtapp", 32) == 0);
    if (is_rtapp) {
        rtapp_size = dst_len;
        rtapp_load_addr = k230_get_rttapp_load_addr();
        image_set_load(pUh, rtapp_load_addr);
    }

    ret = k230_validate_image_layout(pUh, src_data, src_len, dst_len);
    if (ret) {
        printf("%s: invalid image layout for %s\n", __func__, image_get_name(pUh));
        ret = -1;
        goto out;
    }

    if (is_rtapp) {
        rttapp_loaded = 1;
    }

    ret = k230_boot_decomp_to_load_addr(pUh, dst_len, src_data, &src_len);
    if (ret) {
        printf("%s: decomp_to_load_addr fail: %d\n", __func__, ret);
        ret = -1;
    }

out:
    if (0 != ret) {
        return INVALID_LOAD_ADDR;
    }

    return image_get_load(pUh);
}

static int k230_load_img(void)
{
    uint l_addr;
    char slot = k230_select_boot_slot();
    int cnt = 0;

retry:
    cnt++;

    for (int i = 0; i < toc.entry_count; i++) {
        struct k230_toc_entry *e = &toc.entries[i];
        bool _boot = false;

        if (!e->load)
            continue;

        if (e->boot) {
            e->boot &= ~K230_BOOT_FLAG_MASK;
            _boot = true;
        }

        if (0 == strncmp(e->name, "rtt_a", sizeof(e->name)) ||
            0 == strncmp(e->name, "rtapp_a", sizeof(e->name))) {
            if (slot == 'B') {
                continue;
            }
        } else if (0 == strncmp(e->name, "rtt_b", sizeof(e->name)) ||
                   0 == strncmp(e->name, "rtapp_b", sizeof(e->name))) {
            if (slot == 'A') {
                continue;
            }
        }

        l_addr = _k230_load_img(e->name, e->offset, e->size);
        if (l_addr == INVALID_LOAD_ADDR) {
            printf("%s: load %s failed on slot %c, try slot %c\n",
                   __func__, e->name, slot, ((slot == 'A') ? 'B': 'A'));

            if (cnt == 1) {
                slot = (slot == 'A') ? 'B' : 'A';
                goto retry;
            }

            /* 第二次尝试仍失败，放弃启动 */
            return -1;
        }

        e->load_addr = l_addr;

        if (_boot) {
            e->boot |= K230_BOOT_FLAG_MASK;
        }
    }

    return 0;
}

static void k230_boot_core(int core, ulong run_addr)
{
    switch (core) {
    case 0: {
        void (*run)(ulong hart, void* dtb);

        icache_disable();
        dcache_disable();

        asm volatile(".long 0x0170000b\n" ::: "memory");
        run = (void (*)(ulong, void*))run_addr;
        run(0, 0);

        break;
    }
    case 1: {

        writel(run_addr, (void*)0x91102104ULL);
        writel(0x10001000, (void*)0x9110100cULL);
        writel(0x10001, (void*)0x9110100cULL);
        writel(0x10000, (void*)0x9110100cULL);

        break;
    }
    default:
        printf("invalid core num boot to\n");
        break;
    }

}

static void k230_setup_user_tag(void)
{
    setup_start_tag();
    setup_k230_ddr_size_tag(g_dram_size);
    if(rttapp_loaded) {
        setup_k230_rtapp_tag(rtapp_size, rtapp_load_addr);
    }
    setup_end_tag();
}

static void k230_boot_img(void)
{
    struct k230_toc_entry *core0_entry = NULL;
    struct k230_toc_entry *core1_entry = NULL;

    for (int i = 0; i < toc.entry_count; i++) {
        struct k230_toc_entry *e = &toc.entries[i];

        if (e->boot & K230_BOOT_FLAG_MASK) {
            int core_num = (e->boot & K230_BOOT_CORE_MASK) >> K230_BOOT_CORE_SHIFT;

            if (core_num == 0) {
                core0_entry = e;
            } else if (core_num == 1) {
                core1_entry = e;
            }
        }
    }

    if (core1_entry) {
        k230_setup_user_tag();
        k230_boot_core(1, core1_entry->load_addr);
    }

    if (core0_entry) {
        k230_boot_core(0, core0_entry->load_addr);
    }

    while (1) {
        asm volatile("wfi");
    }
}

int k230_run_system(void)
{
    int ret = 0;
    void *toc_buf;

    toc_buf = k230_read_toc();
    if (NULL == toc_buf) {
        printf("%s: k230_read_toc fail\n", __func__);
        ret = -1;
        goto out;
    }

    ret = k230_parse_toc(toc_buf);
    if (0 != ret) {
        printf("%s: not toc exit: %d\n", __func__, ret);
        goto out;
    }

    ret = k230_load_img();
    if (0 != ret) {
        printf("%s: k230_load_img fail: %d\n", __func__, ret);
        goto out;
    }

    k230_boot_img();

out:
    return ret;
}
