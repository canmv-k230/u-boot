#include <common.h>
#include <log.h>
#include <malloc.h>

#include "kburn.h"

struct kburn * kburn_probe_media(enum KBURN_MEDIA_TYPE type, uint8_t index)
{
    struct kburn * kburn = NULL;

#if defined (CONFIG_KBURN_EMMC)
    if(KBURN_MEDIA_MMC == type) {
        kburn = kburn_mmc_probe(index);
    } else
#endif // CONFIG_KBURN_EMMC
    {
        printf("kburn probe not support type %x\n", type);
    }

    if (NULL == kburn) {
        printf("kburn probe failed, type %x\n", type);
    }

    return kburn;
}

int kburn_get_medium_info(struct kburn *burn)
{
    if((NULL == burn) || (NULL == burn->get_medium_info)) {
        pr_err("invalid arg\n");
        return -1;
    }
    return burn->get_medium_info(burn);
}

int kburn_read_medium(struct kburn *burn, u64 offset, void *buf, u64 *len)
{
    if((NULL == burn) || (NULL == burn->read_medium)) {
        pr_err("invalid arg\n");
        return -1;
    }
    return burn->read_medium(burn, offset, buf, len);
}

int kburn_write_medium(struct kburn *burn, u64 offset, void *buf, u64 *len)
{
    if((NULL == burn) || (NULL == burn->write_medium)) {
        pr_err("invalid arg\n");
        return -1;
    }
    return burn->write_medium(burn, offset, buf, len);
}

int kburn_erase_medium(struct kburn *burn, u64 offset, u64 *len)
{
    if((NULL == burn) || (NULL == burn->erase_medium)) {
        pr_err("invalid arg\n");
        return -1;
    }
    return burn->erase_medium(burn, offset, len);
}

void kburn_destory(struct kburn *burn)
{
    if((NULL == burn) || (NULL == burn->destory)) {
        pr_err("invalid arg\n");
        return;
    }

    if(0x00 != burn->destory(burn)) {
        pr_err("destory kburn failed.\n");
    }

    free(burn);
}
