#include <common.h>
#include <log.h>
#include <malloc.h>
#include <errno.h>
#include <div64.h>

#include <dm/uclass.h>

#include <spi_flash.h>

#include <kburn.h>

struct kburn_sf_priv {
    struct udevice *flash;
    int dev_num;
};

static int sf_get_medium_info(struct kburn *burn)
{
    struct kburn_sf_priv *priv = (struct kburn_sf_priv *)(burn->dev_priv);
    struct spi_flash *flash = dev_get_uclass_priv(priv->flash);

    burn->medium_info.valid = 1;
    burn->medium_info.wp = 0;

    burn->medium_info.capacity = flash->size;
    burn->medium_info.erase_size = flash->erase_size;
    burn->medium_info.blk_size = flash->page_size;
    burn->medium_info.timeout_ms = 5000;
    burn->medium_info.type = KBURN_MEDIA_SPI_NOR;

    pr_info("Device SPI-FLASH %d capacity %lld, erase size %lld, blk_sz %lld\n", \
        priv->dev_num, burn->medium_info.capacity, burn->medium_info.erase_size, burn->medium_info.blk_size);

    return 0;
}

static int sf_read_medium(struct kburn *burn, u64 offset, void *buf, u64 *len)
{
    pr_err("TODO");
    return -1;
}

static int sf_write_medium(struct kburn *burn, u64 offset, const void *buf, u64 *len)
{
    struct kburn_sf_priv *priv = (struct kburn_sf_priv *)(burn->dev_priv);
    struct spi_flash *flash = dev_get_uclass_priv(priv->flash);
    int rc;

    pr_debug("sf write offset %lld, size %lld\n", offset, *len);

    if(0x00 == (offset % flash->erase_size)) {
        if(0x00 != (rc = spi_flash_erase(flash, (u32)offset, flash->erase_size))) {
            pr_err("sf write, erase %lld failed, %d\n", offset, flash->erase_size);
        }
    }

    return spi_flash_write(flash, (u32)offset, (size_t)*len, buf);
}

static int sf_erase_medium(struct kburn *burn, u64 offset, u64 *len)
{
    struct kburn_sf_priv *priv = (struct kburn_sf_priv *)(burn->dev_priv);
    struct spi_flash *flash = dev_get_uclass_priv(priv->flash);

    pr_info("erase medium start, offset %lld, size %lld\n", offset, *len);

    int rc = spi_flash_erase(flash, (u32)offset, (size_t)*len);

    pr_info("erase medium done, result %d\n", rc);

    return rc;
}

static int sf_destory(struct kburn *burn)
{
    return 0;
}

#if defined (CONFIG_DM_SPI_FLASH)
struct kburn *kburn_sf_probe(uint8_t bus)
{
    struct udevice *dev, *spiflash;

    struct kburn *burner;
    struct kburn_sf_priv *priv;

    (void)bus;

    if(0x00 != uclass_first_device_err(UCLASS_SPI_FLASH, &spiflash)) {
        pr_err("can not found spi flash device");
        return NULL;
    }
    dev = dev_get_parent(spiflash);

    // we find a sf device, and init it.
    burner = memalign(CONFIG_SYS_CACHELINE_SIZE, sizeof(*burner) + sizeof(*priv));
    if(NULL == burner) {
        pr_err("memaligin failed\n");
        return NULL;
    }

    memset(burner, 0, sizeof(*burner));
    priv = (struct kburn_sf_priv *)((char *)burner + sizeof(*burner));
    priv->flash = spiflash;
    priv->dev_num = dev_seq(dev);

    burner->type = KBURN_MEDIA_SPI_NOR;
    burner->dev_priv = (void *)priv;

	burner->get_medium_info = sf_get_medium_info;
	burner->read_medium = sf_read_medium;
	burner->write_medium = sf_write_medium;
    burner->erase_medium = sf_erase_medium;
    burner->destory = sf_destory;

    return burner;
}
#else
#error "Need CONFIG_DM_SPI_FLASH"
#endif
