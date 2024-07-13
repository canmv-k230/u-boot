#include <common.h>
#include <log.h>
#include <malloc.h>
#include <errno.h>
#include <div64.h>
#include <dm.h>
#include <mmc.h>

#include "kburn.h"

#define MMC_OP_READ     0x01
#define MMC_OP_WRITE    0x02
#define MMC_OP_ERASE    0x03

struct kburn_mmc_priv {
    struct mmc *mmc;
    struct blk_desc *desc;
    int dev_num;
};

static int mmc_get_medium_info(struct kburn *burn)
{
    struct kburn_mmc_priv *priv = (struct kburn_mmc_priv *)(burn->dev_priv);

	struct mmc *mmc = priv->mmc;
    struct blk_desc *desc = priv->desc;

	// if (NULL == (mmc = find_mmc_device(priv->dev_num))) {
	// 	pr_err("Device MMC %d - not found!", priv->dev_num);
	// 	return 1;
	// }

    // if (NULL == (desc = mmc_get_blk_desc(mmc))) {
    //     pr_err("Device MMC %d - get desc failed", priv->dev_num);
    //     return 2;
    // }

    burn->medium_info.valid = 1;
    burn->medium_info.wp = mmc_getwp(mmc);

    burn->medium_info.capacity = (u64)desc->lba * (u64)desc->blksz;
    burn->medium_info.erase_size = (u64)mmc->erase_grp_size * (u64)desc->blksz;
    burn->medium_info.blk_size = desc->blksz;

    pr_info("Device MMC %d capacity %lld, erase size %lld, blk_sz %lld\n", \
        priv->dev_num, burn->medium_info.capacity, burn->medium_info.erase_size, burn->medium_info.blk_size);

    return 0;
}

static u64 mmc_op(int op, struct kburn *burn, u64 offset, void *buf, u64 *len)
{
    struct kburn_mmc_priv *priv = (struct kburn_mmc_priv *)(burn->dev_priv);

	// struct mmc *mmc = priv->mmc;
    struct blk_desc *desc = priv->desc;

	u32 blk_start, blk_count, n = 0;

	// if (NULL == (mmc = find_mmc_device(priv->dev_num))) {
	// 	pr_err("Device MMC %d - not found!", priv->dev_num);
	// 	return 1;
	// }

	// if (mmc_getwp(mmc) == 1) {
	// 	pr_err("Error: card is write protected!\n");
	// 	return 2;
	// }

    // if (NULL == (desc = mmc_get_blk_desc(mmc))) {
    //     pr_err("Device MMC %d - get desc failed", priv->dev_num);
    //     return 3;
    // }

	/*
	 * We must ensure that we work in lba_blk_size chunks, so ALIGN
	 * this value.
	 */
	*len = ALIGN(*len, desc->blksz);

	blk_start = (u32)lldiv(offset, desc->blksz);
	blk_count = *len / desc->blksz;

	if (blk_start + blk_count > desc->lba) {
		puts("Request would exceed designated area!\n");
		return 4;
	}

    if(MMC_OP_READ == op) {
        n = blk_dread(desc, blk_start, blk_count, buf);
    } else if(MMC_OP_WRITE == op) {
        n = blk_dwrite(desc, blk_start, blk_count, buf);
    } else if(MMC_OP_ERASE == op) {
	    n = blk_derase(desc, blk_start, blk_count);
    }

	if (n != blk_count) {
		pr_err("MMC operation failed");

		return 5;
	}

    return 0;
}

static int mmc_read_medium(struct kburn *burn, u64 offset, void *buf, u64 *len)
{
	return mmc_op(MMC_OP_READ, burn, offset, buf, len);
}

static int mmc_write_medium(struct kburn *burn, u64 offset, void *buf, u64 *len)
{
	return mmc_op(MMC_OP_WRITE, burn, offset, buf, len);
}

static int mmc_erase_medium(struct kburn *burn, u64 offset, u64 *len)
{
	return mmc_op(MMC_OP_ERASE, burn, offset, NULL, len);
}

static int mmc_destory(struct kburn *burn)
{
    return 0;
}

#if defined (CONFIG_DM_MMC)
struct kburn *kburn_mmc_probe(uint8_t index)
{
	struct udevice *dev;

    struct mmc *mmc;
    struct blk_desc *desc;

    struct kburn *burner;
    struct kburn_mmc_priv *priv;

    uint8_t idx = 0;

	for (uclass_first_device(UCLASS_MMC, &dev); dev; uclass_next_device(&dev)) {
        if((0xFF != index) && ((idx++) != index)) {
            continue;
        }

        mmc = mmc_get_mmc_dev(dev);
        if(mmc && (0x00 == mmc_init(mmc))) {
            break;
        }
    }

    if (NULL == mmc) {
        pr_err("can not find mmc device\n");
        return NULL;
    }

    if (NULL == (desc = mmc_get_blk_desc(mmc))) {
        pr_err("Device MMC %d - get desc failed", mmc_get_blk_desc(mmc)->devnum);
        return NULL;
    }

    // we find a mmc device, and init it.
    burner = memalign(CONFIG_SYS_CACHELINE_SIZE, sizeof(*burner) + sizeof(*priv));
    if(NULL == burner) {
        pr_err("memaligin failed\n");
        return NULL;
    }
    memset(burner, 0, sizeof(*burner));
    priv = (struct kburn_mmc_priv *)((char *)burner + sizeof(*burner));

    priv->mmc = mmc;
    priv->desc = desc;
    priv->dev_num = mmc_get_blk_desc(mmc)->devnum;

    pr_info("probe mmc succ, dev %d\n", priv->dev_num);

    burner->type = KBURN_MEDIA_MMC;
    burner->dev_priv = (void *)priv;

	burner->get_medium_info = mmc_get_medium_info;
	burner->read_medium = mmc_read_medium;
	burner->write_medium = mmc_write_medium;
    burner->erase_medium = mmc_erase_medium;
    burner->destory = mmc_destory;

    return burner;
}
#else
#error "Need CONFIG_DM_MMC"
#endif
