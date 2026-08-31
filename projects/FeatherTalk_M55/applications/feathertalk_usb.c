#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include <board.h>
#include "board_storage.h"
#include "feathertalk_storage.h"
#include "feathertalk_usb.h"
#include "feathertalk_usb_uac.h"
#ifdef FEATHERTALK_USING_UI_SHELL
#include "ui/feathertalk_ui.h"
#include "ui/feathertalk_ui_preferences_store.h"
#endif

static ft_usb_status_t s_usb_status =
{
    .role = FT_USB_ROLE_DEVICE,
    .function = FT_USB_FUNCTION_NONE,
    .host_supported = false,
#ifdef FEATHERTALK_USING_USB_MSC
    .storage_supported = true,
#else
    .storage_supported = false,
#endif
#ifdef FEATHERTALK_USING_USB_UAC
    .audio_supported = true,
#else
    .audio_supported = false,
#endif
};

#if defined(FEATHERTALK_USING_USB_MSC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_MSC)

#include "usbd_core.h"
#include "usbd_msc.h"

#define FT_USB_BUS_ID             0U
#define FT_USB_MSC_IN_EP          0x81U
#define FT_USB_MSC_OUT_EP         0x02U
#define FT_USB_VENDOR_ID          0xFFFFU
#define FT_USB_PRODUCT_ID         0xF501U
#define FT_USB_MAX_POWER_MA       100U
#define FT_USB_CONFIGURATION_SIZE (9U + MSC_DESCRIPTOR_LEN)
#define FT_USB_MSC_LUN_COUNT      2U
#define FT_USB_LUN_FLASH          0U
#define FT_USB_LUN_SD             1U

#ifdef CONFIG_USB_HS
#define FT_USB_MSC_MAX_PACKET 512U
#else
#define FT_USB_MSC_MAX_PACKET 64U
#endif

typedef struct
{
    rt_device_t device;
    struct rt_device_blk_geometry geometry;
    const char *name;
} ft_usb_lun_t;

static ft_usb_lun_t s_luns[FT_USB_MSC_LUN_COUNT];
static struct usbd_interface s_msc_interface;
static uint32_t s_read_operations[FT_USB_MSC_LUN_COUNT];
static uint32_t s_write_operations[FT_USB_MSC_LUN_COUNT];
static uint32_t s_read_bytes[FT_USB_MSC_LUN_COUNT];
static uint32_t s_write_bytes[FT_USB_MSC_LUN_COUNT];
static uint32_t s_read_max_transfer[FT_USB_MSC_LUN_COUNT];
static uint32_t s_write_max_transfer[FT_USB_MSC_LUN_COUNT];

static const uint8_t s_device_descriptor[] =
{
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00,
                               FT_USB_VENDOR_ID, FT_USB_PRODUCT_ID,
                               0x0100, 0x01)
};

static const uint8_t s_config_descriptor[] =
{
    USB_CONFIG_DESCRIPTOR_INIT(FT_USB_CONFIGURATION_SIZE, 0x01, 0x01,
                               USB_CONFIG_BUS_POWERED,
                               FT_USB_MAX_POWER_MA),
    MSC_DESCRIPTOR_INIT(0x00, FT_USB_MSC_OUT_EP, FT_USB_MSC_IN_EP,
                        FT_USB_MSC_MAX_PACKET, 0x02)
};

static const uint8_t s_device_qualifier_descriptor[] =
{
    0x0A, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

static const char *s_string_descriptors[] =
{
    (const char[]){0x09, 0x04},
    "FeatherRepository",
    "FeatherTalk Flash + SD Storage",
    "FTALK-MSC-0001",
};

static const uint8_t *usb_device_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_device_descriptor;
}

static const uint8_t *usb_config_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_config_descriptor;
}

static const uint8_t *usb_qualifier_descriptor_cb(uint8_t speed)
{
    RT_UNUSED(speed);
    return s_device_qualifier_descriptor;
}

static const char *usb_string_descriptor_cb(uint8_t speed, uint8_t index)
{
    RT_UNUSED(speed);
    if (index >= sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]))
        return RT_NULL;
    return s_string_descriptors[index];
}

static const struct usb_descriptor s_usb_descriptor =
{
    .device_descriptor_callback = usb_device_descriptor_cb,
    .config_descriptor_callback = usb_config_descriptor_cb,
    .device_quality_descriptor_callback = usb_qualifier_descriptor_cb,
    .string_descriptor_callback = usb_string_descriptor_cb,
};

static void usb_device_event_handler(uint8_t busid, uint8_t event)
{
    RT_UNUSED(busid);
    switch (event)
    {
    case USBD_EVENT_CONNECTED:
        s_usb_status.connected = true;
        break;
    case USBD_EVENT_CONFIGURED:
        s_usb_status.connected = true;
        s_usb_status.configured = true;
        break;
    case USBD_EVENT_DISCONNECTED:
    case USBD_EVENT_DEINIT:
        s_usb_status.connected = false;
        s_usb_status.configured = false;
        break;
    default:
        break;
    }
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun,
                      uint32_t *block_num, uint32_t *block_size)
{
    RT_UNUSED(busid);
    if (lun >= FT_USB_MSC_LUN_COUNT)
    {
        if (block_num != RT_NULL) *block_num = 0U;
        if (block_size != RT_NULL) *block_size = 512U;
        return;
    }
    if (block_num != RT_NULL)
        *block_num = s_luns[lun].geometry.sector_count;
    if (block_size != RT_NULL)
        *block_size = s_luns[lun].geometry.bytes_per_sector;
}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector,
                         uint8_t *buffer, uint32_t length)
{
    rt_size_t blocks;
    RT_UNUSED(busid);
    if (lun >= FT_USB_MSC_LUN_COUNT || s_luns[lun].device == RT_NULL ||
        buffer == RT_NULL || s_luns[lun].geometry.bytes_per_sector == 0U ||
        (length % s_luns[lun].geometry.bytes_per_sector) != 0U)
        return -1;
    blocks = length / s_luns[lun].geometry.bytes_per_sector;
    if (rt_device_read(s_luns[lun].device, sector, buffer, blocks) != blocks)
        return -1;
    s_read_operations[lun]++;
    s_read_bytes[lun] += length;
    if (length > s_read_max_transfer[lun]) s_read_max_transfer[lun] = length;
    return 0;
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector,
                          uint8_t *buffer, uint32_t length)
{
    rt_size_t blocks;
    RT_UNUSED(busid);
    if (lun >= FT_USB_MSC_LUN_COUNT || s_luns[lun].device == RT_NULL ||
        buffer == RT_NULL || s_luns[lun].geometry.bytes_per_sector == 0U ||
        (length % s_luns[lun].geometry.bytes_per_sector) != 0U)
        return -1;
    blocks = length / s_luns[lun].geometry.bytes_per_sector;
    if (rt_device_write(s_luns[lun].device, sector, buffer, blocks) != blocks)
        return -1;
    /* The NOR driver uses an erase-block cache.  Commit it before reporting
     * SCSI WRITE success so unplugging the cable cannot strand host data only
     * in M55 RAM. */
    if (lun == FT_USB_LUN_FLASH &&
        rt_device_control(s_luns[lun].device, RT_DEVICE_CTRL_BLK_SYNC,
                          RT_NULL) != RT_EOK)
        return -1;
    s_write_operations[lun]++;
    s_write_bytes[lun] += length;
    if (length > s_write_max_transfer[lun]) s_write_max_transfer[lun] = length;
    return 0;
}

static int usb_lun_open(uint8_t lun, const char *device_name)
{
    int result;

    if (lun >= FT_USB_MSC_LUN_COUNT || device_name == RT_NULL)
        return -RT_EINVAL;
    s_luns[lun].name = device_name;
    s_luns[lun].device = rt_device_find(device_name);
    if (s_luns[lun].device == RT_NULL) return -RT_ENOENT;
    result = rt_device_open(s_luns[lun].device, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        s_luns[lun].device = RT_NULL;
        return result;
    }
    rt_memset(&s_luns[lun].geometry, 0, sizeof(s_luns[lun].geometry));
    result = rt_device_control(s_luns[lun].device,
                               RT_DEVICE_CTRL_BLK_GETGEOME,
                               &s_luns[lun].geometry);
    if (result != RT_EOK ||
        s_luns[lun].geometry.bytes_per_sector == 0U ||
        s_luns[lun].geometry.sector_count == 0U)
    {
        (void)rt_device_close(s_luns[lun].device);
        rt_memset(&s_luns[lun], 0, sizeof(s_luns[lun]));
        return result == RT_EOK ? -RT_ERROR : result;
    }
    return RT_EOK;
}

static void usb_lun_close(uint8_t lun)
{
    if (lun >= FT_USB_MSC_LUN_COUNT) return;
    if (s_luns[lun].device != RT_NULL)
    {
        (void)rt_device_control(s_luns[lun].device,
                                RT_DEVICE_CTRL_BLK_SYNC, RT_NULL);
        (void)rt_device_close(s_luns[lun].device);
    }
    rt_memset(&s_luns[lun], 0, sizeof(s_luns[lun]));
}

static int usb_ui_storage_freeze(void)
{
#ifdef FEATHERTALK_USING_UI_SHELL
    int result = ft_preferences_store_freeze();
    if (result != RT_EOK) return result;
    result = feathertalk_ui_media_freeze();
    if (result != RT_EOK) ft_preferences_store_thaw();
    return result;
#else
    return RT_EOK;
#endif
}

static int usb_ui_storage_thaw(void)
{
#ifdef FEATHERTALK_USING_UI_SHELL
    int result = feathertalk_ui_media_thaw();
    ft_preferences_store_thaw();
    return result;
#else
    return RT_EOK;
#endif
}

static int usb_storage_start(void)
{
    const char *flash_device_name = RT_NULL;
    const char *sd_device_name = RT_NULL;
    int result;

    result = usb_ui_storage_freeze();
    if (result != RT_EOK) return result;
    result = board_flash_storage_export_begin(&flash_device_name);
    if (result != RT_EOK)
    {
        (void)usb_ui_storage_thaw();
        return result;
    }
    result = board_sdcard_export_begin(&sd_device_name);
    if (result != RT_EOK)
    {
        (void)board_flash_storage_export_end();
        (void)usb_ui_storage_thaw();
        return result;
    }
    result = usb_lun_open(FT_USB_LUN_FLASH, flash_device_name);
    if (result != RT_EOK)
    {
        (void)board_sdcard_export_end();
        (void)board_flash_storage_export_end();
        (void)usb_ui_storage_thaw();
        return result;
    }
    result = usb_lun_open(FT_USB_LUN_SD, sd_device_name);
    if (result != RT_EOK)
    {
        usb_lun_close(FT_USB_LUN_FLASH);
        (void)board_sdcard_export_end();
        (void)board_flash_storage_export_end();
        (void)usb_ui_storage_thaw();
        return result;
    }

    usbd_desc_register(FT_USB_BUS_ID, &s_usb_descriptor);
    usbd_add_interface(FT_USB_BUS_ID,
                       usbd_msc_init_intf(FT_USB_BUS_ID, &s_msc_interface,
                                         FT_USB_MSC_OUT_EP,
                                         FT_USB_MSC_IN_EP));
    result = usbd_initialize(FT_USB_BUS_ID, USBHS_BASE,
                             usb_device_event_handler);
    if (result != 0)
    {
        usb_lun_close(FT_USB_LUN_SD);
        usb_lun_close(FT_USB_LUN_FLASH);
        (void)board_sdcard_export_end();
        (void)board_flash_storage_export_end();
        (void)usb_ui_storage_thaw();
        return -RT_ERROR;
    }

    s_usb_status.function = FT_USB_FUNCTION_STORAGE;
    s_usb_status.active = true;
    s_usb_status.flash_present = true;
    s_usb_status.sd_present = true;
    s_usb_status.lun_count = FT_USB_MSC_LUN_COUNT;
    s_usb_status.flash_block_size =
        s_luns[FT_USB_LUN_FLASH].geometry.bytes_per_sector;
    s_usb_status.flash_block_count =
        s_luns[FT_USB_LUN_FLASH].geometry.sector_count;
    s_usb_status.sd_block_size = s_luns[FT_USB_LUN_SD].geometry.bytes_per_sector;
    s_usb_status.sd_block_count = s_luns[FT_USB_LUN_SD].geometry.sector_count;
    /* Backward-compatible aliases continue to describe the removable SD LUN. */
    s_usb_status.block_size = s_usb_status.sd_block_size;
    s_usb_status.block_count = s_usb_status.sd_block_count;
    s_usb_status.last_error = RT_EOK;
    rt_memset(s_read_operations, 0, sizeof(s_read_operations));
    rt_memset(s_write_operations, 0, sizeof(s_write_operations));
    rt_memset(s_read_bytes, 0, sizeof(s_read_bytes));
    rt_memset(s_write_bytes, 0, sizeof(s_write_bytes));
    rt_memset(s_read_max_transfer, 0, sizeof(s_read_max_transfer));
    rt_memset(s_write_max_transfer, 0, sizeof(s_write_max_transfer));
    rt_kprintf("FeatherTalk USB MSC started: LUN0 %s %lu x %lu, "
               "LUN1 %s %lu x %lu\n",
               flash_device_name,
               (unsigned long)s_usb_status.flash_block_count,
               (unsigned long)s_usb_status.flash_block_size,
               sd_device_name,
               (unsigned long)s_usb_status.sd_block_count,
               (unsigned long)s_usb_status.sd_block_size);
    return RT_EOK;
}

static int usb_storage_stop(void)
{
    int result = RT_EOK;

    if (s_usb_status.active)
        (void)usbd_deinitialize(FT_USB_BUS_ID);
    usb_lun_close(FT_USB_LUN_SD);
    usb_lun_close(FT_USB_LUN_FLASH);
    if (board_sdcard_is_exported())
    {
        int mount_result = board_sdcard_export_end();
        if (result == RT_EOK) result = mount_result;
    }
    if (board_flash_storage_is_exported())
    {
        int mount_result = board_flash_storage_export_end();
        if (result == RT_EOK) result = mount_result;
    }
    {
        int thaw_result = usb_ui_storage_thaw();
        if (result == RT_EOK) result = thaw_result;
    }
    s_usb_status.function = FT_USB_FUNCTION_NONE;
    s_usb_status.active = false;
    s_usb_status.connected = false;
    s_usb_status.configured = false;
    s_usb_status.block_size = 0U;
    s_usb_status.block_count = 0U;
    s_usb_status.lun_count = 0U;
    s_usb_status.flash_block_size = 0U;
    s_usb_status.flash_block_count = 0U;
    s_usb_status.sd_block_size = 0U;
    s_usb_status.sd_block_count = 0U;
    return result;
}

#endif /* FEATHERTALK_USING_USB_MSC */

void ft_usb_get_status(ft_usb_status_t *status)
{
    ft_storage_volume_info_t volume;
    board_flash_storage_info_t flash_info;
    ft_usb_uac_status_t uac;
    if (status == RT_NULL) return;

    ft_usb_refresh();
    s_usb_status.flash_present =
        board_flash_storage_get_info(&flash_info) == RT_EOK &&
        flash_info.present;
    if (s_usb_status.function == FT_USB_FUNCTION_STORAGE &&
        s_usb_status.active)
        s_usb_status.sd_present = board_sdcard_export_present() == RT_TRUE;
    else
        s_usb_status.sd_present =
            ft_storage_get_volume(FT_STORAGE_SD_MOUNT_PATH, &volume) == RT_EOK;
    ft_usb_uac_get_status(&uac);
    if (s_usb_status.function == FT_USB_FUNCTION_AUDIO)
    {
        s_usb_status.active = uac.active;
        s_usb_status.connected = uac.connected;
        s_usb_status.configured = uac.configured;
        s_usb_status.uac_output_streaming = uac.output_streaming;
        s_usb_status.uac_input_streaming = uac.input_streaming;
        s_usb_status.uac_format_pending = uac.format_pending;
        s_usb_status.uac_output_sample_rate = uac.output_sample_rate;
        s_usb_status.uac_output_sample_bits = uac.output_sample_bits;
        s_usb_status.uac_output_channels = uac.output_channels;
        s_usb_status.uac_input_sample_rate = uac.input_sample_rate;
        s_usb_status.uac_input_sample_bits = uac.input_sample_bits;
        s_usb_status.uac_input_channels = uac.input_channels;
        s_usb_status.uac_host_update_count = uac.host_update_count;
        s_usb_status.uac_device_update_count = uac.device_update_count;
        s_usb_status.uac_sync_generation = uac.sync_generation;
        s_usb_status.uac_host_to_device_bytes = uac.host_to_device_bytes;
        s_usb_status.uac_device_to_host_bytes = uac.device_to_host_bytes;
        s_usb_status.uac_output_overruns = uac.output_overruns;
        s_usb_status.uac_input_underruns = uac.input_underruns;
        s_usb_status.last_error = uac.last_error;
    }
    *status = s_usb_status;
}

int ft_usb_set_function(ft_usb_function_t function)
{
    int result;

    if (function == s_usb_status.function) return RT_EOK;
    if (function > FT_USB_FUNCTION_AUDIO) return -RT_EINVAL;

    result = RT_EOK;

#if defined(FEATHERTALK_USING_USB_MSC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_MSC)
    if (s_usb_status.function == FT_USB_FUNCTION_STORAGE)
    {
        result = usb_storage_stop();
        if (result != RT_EOK) goto done;
    }
#endif

#ifdef FEATHERTALK_USING_USB_UAC
    if (s_usb_status.function == FT_USB_FUNCTION_AUDIO)
    {
        result = ft_usb_uac_stop();
        if (result != RT_EOK) goto done;
        s_usb_status.function = FT_USB_FUNCTION_NONE;
        s_usb_status.active = false;
    }
#endif

    if (function == FT_USB_FUNCTION_NONE) goto done;
    if (function == FT_USB_FUNCTION_STORAGE)
    {
#if defined(FEATHERTALK_USING_USB_MSC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_MSC)
        result = usb_storage_start();
#else
        result = -RT_ENOSYS;
#endif
        goto done;
    }
    if (function == FT_USB_FUNCTION_AUDIO)
    {
#ifdef FEATHERTALK_USING_USB_UAC
        result = ft_usb_uac_start();
        if (result == RT_EOK)
        {
            s_usb_status.function = FT_USB_FUNCTION_AUDIO;
            s_usb_status.active = true;
            s_usb_status.lun_count = 0U;
        }
#else
        result = -RT_ENOSYS;
#endif
    }

done:
    if (function == FT_USB_FUNCTION_NONE && result == RT_EOK)
    {
        s_usb_status.function = FT_USB_FUNCTION_NONE;
        s_usb_status.active = false;
        s_usb_status.connected = false;
        s_usb_status.configured = false;
    }
    s_usb_status.last_error = result;
    return result;
}

int ft_usb_set_uac_output_format(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels)
{
    return ft_usb_uac_set_output_format(sample_rate, sample_bits, channels,
                                        s_usb_status.function ==
                                            FT_USB_FUNCTION_AUDIO);
}

bool ft_usb_uac_output_supported(uint32_t sample_rate, uint8_t sample_bits,
                                 uint8_t channels)
{
    return ft_usb_uac_output_format_supported(sample_rate, sample_bits,
                                               channels);
}

bool ft_usb_uac_input_supported(uint32_t sample_rate, uint8_t sample_bits,
                                uint8_t channels)
{
    return ft_usb_uac_input_format_supported(sample_rate, sample_bits,
                                              channels);
}

void ft_usb_refresh(void)
{
#if defined(FEATHERTALK_USING_USB_MSC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_MSC)
    if (s_usb_status.function == FT_USB_FUNCTION_STORAGE &&
        !board_sdcard_export_present())
    {
        s_usb_status.last_error = -RT_ENOENT;
        (void)usb_storage_stop();
    }
#endif
}

#ifdef RT_USING_FINSH
static uint32_t usb_reg_read(uint32_t offset)
{
    return *(volatile uint32_t *)(USBHS_BASE + offset);
}

static void usb_shell_print_dwc2_regs(void)
{
    const uint32_t out_ep2 = 0x0B00U + 2U * 0x20U;

    rt_kprintf("DWC2 GINTSTS=%08lx GINTMSK=%08lx DSTS=%08lx\n",
               (unsigned long)usb_reg_read(0x0014U),
               (unsigned long)usb_reg_read(0x0018U),
               (unsigned long)usb_reg_read(0x0808U));
    rt_kprintf("DWC2 DOEPMSK=%08lx DAINT=%08lx DAINTMSK=%08lx\n",
               (unsigned long)usb_reg_read(0x0814U),
               (unsigned long)usb_reg_read(0x0818U),
               (unsigned long)usb_reg_read(0x081CU));
    rt_kprintf("DWC2 OUT2 CTL=%08lx INT=%08lx TSIZ=%08lx DMA=%08lx\n",
               (unsigned long)usb_reg_read(out_ep2 + 0x00U),
               (unsigned long)usb_reg_read(out_ep2 + 0x08U),
               (unsigned long)usb_reg_read(out_ep2 + 0x10U),
               (unsigned long)usb_reg_read(out_ep2 + 0x14U));
    rt_kprintf("DWC2 ISO OUT re-arms=%lu\n",
               (unsigned long)usbd_dwc2_get_iso_out_rearm_count(0U));
}

static void usb_shell_print_status(void)
{
    ft_usb_status_t status;
    ft_usb_uac_status_t uac;

    ft_usb_get_status(&status);
    rt_kprintf("USB role=device function=%s active=%u connected=%u configured=%u "
               "flash=%u sd=%u luns=%u error=%d\n",
               status.function == FT_USB_FUNCTION_STORAGE ? "storage" :
               status.function == FT_USB_FUNCTION_AUDIO ? "audio" : "none",
               status.active ? 1U : 0U,
               status.connected ? 1U : 0U,
               status.configured ? 1U : 0U,
               status.flash_present ? 1U : 0U,
               status.sd_present ? 1U : 0U,
               status.lun_count,
               status.last_error);
#if defined(FEATHERTALK_USING_USB_MSC) && \
    defined(RT_USING_CHERRYUSB) && defined(RT_CHERRYUSB_DEVICE) && \
    defined(RT_CHERRYUSB_DEVICE_MSC)
    if (status.function == FT_USB_FUNCTION_STORAGE)
    {
        uint8_t lun;
        for (lun = 0U; lun < FT_USB_MSC_LUN_COUNT; lun++)
        {
            rt_kprintf("LUN%u %-13s %lu x %lu read=%lu/%luB max=%lu "
                       "write=%lu/%luB max=%lu\n",
                       lun,
                       lun == FT_USB_LUN_FLASH ? "internal-flash" : "sd-card",
                       (unsigned long)s_luns[lun].geometry.sector_count,
                       (unsigned long)s_luns[lun].geometry.bytes_per_sector,
                       (unsigned long)s_read_operations[lun],
                       (unsigned long)s_read_bytes[lun],
                       (unsigned long)s_read_max_transfer[lun],
                       (unsigned long)s_write_operations[lun],
                       (unsigned long)s_write_bytes[lun],
                       (unsigned long)s_write_max_transfer[lun]);
        }
        rt_kprintf("USB MSC transfer buffer=%u bytes\n",
                   (unsigned int)CONFIG_USBDEV_MSC_MAX_BUFSIZE);
    }
#endif
    if (status.function == FT_USB_FUNCTION_AUDIO)
    {
        ft_usb_uac_get_status(&uac);
        rt_kprintf("UAC2 out: %lu Hz %u-bit %u-ch stream=%u host->device=%luKiB overruns=%lu\n",
                   (unsigned long)status.uac_output_sample_rate,
                   status.uac_output_sample_bits,
                   status.uac_output_channels,
                   status.uac_output_streaming ? 1U : 0U,
                   (unsigned long)(status.uac_host_to_device_bytes / 1024U),
                   (unsigned long)status.uac_output_overruns);
        rt_kprintf("UAC2 in : %lu Hz %u-bit %u-ch stream=%u device->host=%luKiB underruns=%lu\n",
                   (unsigned long)status.uac_input_sample_rate,
                   status.uac_input_sample_bits,
                   status.uac_input_channels,
                   status.uac_input_streaming ? 1U : 0U,
                   (unsigned long)(status.uac_device_to_host_bytes / 1024U),
                   (unsigned long)status.uac_input_underruns);
        rt_kprintf("UAC2 sync generation=%lu host-updates=%lu device-updates=%lu pending=%u\n",
                   (unsigned long)status.uac_sync_generation,
                   (unsigned long)status.uac_host_update_count,
                   (unsigned long)status.uac_device_update_count,
                   status.uac_format_pending ? 1U : 0U);
        rt_kprintf("UAC2 diag cb=%lu ring=%luB read=%luB wake=%lu open=%lu "
                   "write=%lu/%luB worker=%u\n",
                   (unsigned long)uac.output_callback_count,
                   (unsigned long)uac.output_ring_used,
                   (unsigned long)uac.output_ring_read_bytes,
                   (unsigned long)uac.output_worker_wakeups,
                   (unsigned long)uac.output_sound_open_count,
                   (unsigned long)uac.output_sound_write_calls,
                   (unsigned long)uac.output_sound_write_bytes,
                   uac.output_worker_state);
    }
}

static int feather_usb(int argc, char **argv)
{
    int result = RT_EOK;

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0))
    {
        usb_shell_print_status();
        return RT_EOK;
    }
    if (argc == 2 && strcmp(argv[1], "regs") == 0)
    {
        usb_shell_print_dwc2_regs();
        return RT_EOK;
    }
    if (argc == 2 && strcmp(argv[1], "storage") == 0)
        result = ft_usb_set_function(FT_USB_FUNCTION_STORAGE);
    else if (argc == 2 && strcmp(argv[1], "audio") == 0)
        result = ft_usb_set_function(FT_USB_FUNCTION_AUDIO);
    else if (argc == 2 && strcmp(argv[1], "stop") == 0)
        result = ft_usb_set_function(FT_USB_FUNCTION_NONE);
    else if (argc == 5 && strcmp(argv[1], "format") == 0)
        result = ft_usb_set_uac_output_format(
            (uint32_t)strtoul(argv[2], RT_NULL, 10),
            (uint8_t)strtoul(argv[3], RT_NULL, 10),
            (uint8_t)strtoul(argv[4], RT_NULL, 10));
    else
    {
        rt_kprintf("Usage: feather_usb [status|regs|storage|audio|stop|format <rate> <bits> <channels>]\n");
        return -RT_EINVAL;
    }

    usb_shell_print_status();
    return result;
}
MSH_CMD_EXPORT(feather_usb, Control FeatherTalk USB Device MSC and UAC2);
#endif
