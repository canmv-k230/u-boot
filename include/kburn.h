#ifndef __KD_BURNER_H__
#define __KD_BURNER_H__

#include <common.h>

/*

1. 指定存储类型，去初始化对应设备，并获取对应的信息，大小，blk blk_size等

2. 支持擦除

3. 支持写入

4. 支持读取

*/

#if defined(CONFIG_KBURN_OVER_USB) && (0x00 != CONFIG_KBURN_OVER_USB)

#define KBURN_USB_INTF_CLASS        (0xFF)
#define KBURN_USB_INTF_SUBCLASS     (0x02)
#define KBURN_USB_INTF_PROTOCOL     (0x00)

#define KBURN_USB_EP_BUFFER_SZIE    (4096)
#define KBURN_USB_BUFFER_SIZE       (KBURN_USB_EP_BUFFER_SZIE * 2)

#endif

enum KBURN_MEDIA_TYPE {
    KBURN_MEDIA_NONE = 0x00,
    KBURN_MEDIA_MMC = 0x01,
    KBURN_MEDIA_SPI_NAND = 0x02,
    KBURN_MEDIA_SPI_NOR = 0x03,
    KBURN_MEDIA_OTP = 0x04,  
};

struct kburn_medium_info {
    u64 capacity;
    u64 blk_size;
    u64 erase_size;
    uint8_t wp;
    uint8_t valid;
};

struct kburn {
    enum KBURN_MEDIA_TYPE type;
    struct kburn_medium_info medium_info;
    void *dev_priv;

	int (*get_medium_info)(struct kburn *burn);

	int (*read_medium)(struct kburn *burn,
			u64 offset, void *buf, u64 *len);

	int (*write_medium)(struct kburn *burn,
			u64 offset, void *buf, u64 *len);

    int (*erase_medium)(struct kburn *burn, u64 offset, u64 *len);

    int (*destory)(struct kburn *burn);
};

struct kburn * kburn_probe_media(enum KBURN_MEDIA_TYPE type, uint8_t index);

int kburn_get_medium_info(struct kburn *burn);

int kburn_read_medium(struct kburn *burn, u64 offset, void *buf, u64 *len);

int kburn_write_medium(struct kburn *burn, u64 offset, void *buf, u64 *len);

int kburn_erase_medium(struct kburn *burn, u64 offset, u64 *len);

void kburn_destory(struct kburn *burn);

#if defined (CONFIG_KBURN_EMMC)
struct kburn *kburn_mmc_probe(uint8_t index);
#endif // CONFIG_KBURN_EMMC

#endif // __KD_BURNER_H__
