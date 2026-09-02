#include <rtdbg.h>
#include <rtthread.h>
#include <rtdevice.h>
#include "drv_touch.h"

#define DBG_TAG "ST7102"
#define DBG_LVL DBG_INFO

static struct rt_i2c_client ST7102_client;
#define TOUCH_SLAVE_ADDRESS 0x55
#define POLLING_INTERVAL_MS 10
static rt_uint8_t s_touch_max_points = ST7102_MAX_TOUCH;

static rt_err_t ST7102_write_reg8(struct rt_i2c_client *dev, rt_uint16_t reg, rt_uint8_t value)
{
    rt_uint8_t data[3];
    struct rt_i2c_msg msg;

    msg.addr = dev->client_addr;
    msg.flags = RT_I2C_WR;
    msg.buf = data;

    /* ST7123 uses a 16-bit register address for every I2C transaction,
     * including report-page registers below 0x0100. */
    data[0] = (rt_uint8_t)((reg >> 8) & 0xFF);
    data[1] = (rt_uint8_t)(reg & 0xFF);
    data[2] = value;
    msg.len = 3;

    return (rt_i2c_transfer(dev->bus, &msg, 1) == 1) ? RT_EOK : -RT_ERROR;
}

static rt_err_t ST7102_read_regs(struct rt_i2c_client *dev, const rt_uint8_t *reg, rt_uint8_t reg_len, rt_uint8_t *data, rt_uint8_t len)
{
    struct rt_i2c_msg msgs[2];

    msgs[0].addr = dev->client_addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = (rt_uint8_t *)reg;
    msgs[0].len = reg_len;

    msgs[1].addr = dev->client_addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = data;
    msgs[1].len = len;

    if (rt_i2c_transfer(dev->bus, msgs, 2) == 2)
    {
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}

static rt_err_t ST7102_read_reg16(struct rt_i2c_client *dev, rt_uint16_t reg,
                                  rt_uint8_t *data, rt_uint8_t len);

static rt_err_t ST7102_get_info(struct rt_i2c_client *dev, struct rt_touch_info *info)
{
    rt_uint8_t raw[5];
    if (info == RT_NULL ||
        ST7102_read_reg16(dev, ST7102_MAX_X_Coord_High,
                          raw, sizeof(raw)) != RT_EOK)
        return -RT_ERROR;
    info->range_x = ((raw[0] & 0x3FU) << 8) | raw[1];
    info->range_y = ((raw[2] & 0x3FU) << 8) | raw[3];
    info->point_num = raw[4];
    if (info->point_num == 0U || info->point_num > ST7102_MAX_TOUCH)
        return -RT_ERROR;
    s_touch_max_points = info->point_num;
    return RT_EOK;
}

static rt_err_t ST7102_soft_reset(struct rt_i2c_client *dev)
{
    if (ST7102_write_reg8(dev, ST7102_Device_Control, 0x01U) != RT_EOK)
    {
        LOG_E("soft reset failed");
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int16_t pre_x[ST7102_MAX_TOUCH] = {-1, -1, -1, -1, -1};
static int16_t pre_y[ST7102_MAX_TOUCH] = {-1, -1, -1, -1, -1};
static int16_t pre_w[ST7102_MAX_TOUCH] = {-1, -1, -1, -1, -1};
static rt_uint8_t s_tp_dowm[ST7102_MAX_TOUCH];
static rt_uint8_t s_release_candidate[ST7102_MAX_TOUCH];
static struct rt_touch_data *read_data;
static st7102_touch_diagnostics_t s_touch_diagnostics;

#define ST7102_REPORT_HEADER_LEN       4U
#define ST7102_REPORT_POINT_LEN        7U
#define ST7102_REPORT_POINT0_REG       0x0014U
#define ST7102_RELEASE_CONFIRM_FRAMES 3U
#define ST7102_POSITION_DEADBAND       3

static void ST7102_touch_up(void *buf, int8_t id)
{
    read_data = (struct rt_touch_data *)buf;

    if (s_tp_dowm[id] == 1)
    {
        s_tp_dowm[id] = 0;
        read_data[id].event = RT_TOUCH_EVENT_UP;
    }
    else
    {
        read_data[id].event = RT_TOUCH_EVENT_NONE;
    }
    read_data[id].width = pre_w[id];
    read_data[id].x_coordinate = pre_x[id];
    read_data[id].y_coordinate = pre_y[id];
    read_data[id].track_id = id;

    pre_x[id] = -1;
    pre_y[id] = -1;
    pre_w[id] = -1;
    s_release_candidate[id] = 0U;
}

static rt_err_t ST7102_read_reg16(struct rt_i2c_client *dev, rt_uint16_t reg,
                                  rt_uint8_t *data, rt_uint8_t len)
{
    rt_uint8_t address[2];
    address[0] = (rt_uint8_t)((reg >> 8) & 0xFF);
    address[1] = (rt_uint8_t)(reg & 0xFF);
    return ST7102_read_regs(dev, address, sizeof(address), data, len);
}

static void ST7102_touch_down(void *buf, int8_t id, int16_t x, int16_t y, int16_t w)
{
    read_data = (struct rt_touch_data *)buf;

    if (s_tp_dowm[id] == 1)
    {
        read_data[id].event = RT_TOUCH_EVENT_MOVE;
    }
    else
    {
        read_data[id].event = RT_TOUCH_EVENT_DOWN;
        s_tp_dowm[id] = 1;
    }

    read_data[id].width = w;
    read_data[id].x_coordinate = x;
    read_data[id].y_coordinate = y;
    read_data[id].track_id = id;

    pre_x[id] = x;
    pre_y[id] = y;
    pre_w[id] = w;
}

static int16_t ST7102_position_filter(int16_t previous, int16_t current)
{
    int16_t difference;
    if (previous < 0) return current;
    difference = current >= previous ? current - previous : previous - current;
    return difference <= ST7102_POSITION_DEADBAND ? previous : current;
}

static rt_size_t ST7102_report_held_touches(void *buf, rt_size_t max_points)
{
    rt_size_t id;
    rt_size_t touch_num = 0;

    if (max_points > ST7102_MAX_TOUCH) max_points = ST7102_MAX_TOUCH;
    read_data = (struct rt_touch_data *)buf;
    for (id = 0; id < max_points; id++)
    {
        if (s_tp_dowm[id] != 0U)
        {
            if (++s_release_candidate[id] < ST7102_RELEASE_CONFIRM_FRAMES)
            {
                read_data[id].event = RT_TOUCH_EVENT_MOVE;
                read_data[id].width = pre_w[id];
                read_data[id].x_coordinate = pre_x[id];
                read_data[id].y_coordinate = pre_y[id];
                read_data[id].track_id = id;
                touch_num++;
            }
            else
            {
                s_touch_diagnostics.release_reports++;
                ST7102_touch_up(buf, (int8_t)id);
            }
        }
        else
        {
            read_data[id].event = RT_TOUCH_EVENT_NONE;
        }
    }
    if (touch_num > 0U) s_touch_diagnostics.held_reports++;
    return touch_num;
}

static void ST7102_release_all_touches(void *buf, rt_size_t max_points)
{
    rt_size_t id;

    if (max_points > ST7102_MAX_TOUCH) max_points = ST7102_MAX_TOUCH;
    for (id = 0; id < max_points; id++)
    {
        if (s_tp_dowm[id] != 0U) s_touch_diagnostics.release_reports++;
        ST7102_touch_up(buf, (int8_t)id);
    }
    for (; id < ST7102_MAX_TOUCH; id++)
    {
        ST7102_touch_up(buf, (int8_t)id);
    }
}

rt_uint8_t read_buf[8 * ST7102_MAX_TOUCH] = {0};
#define LED_RED GET_PIN(16, 7)
static rt_size_t ST7102_read_point(struct rt_touch_device *touch, void *buf, rt_size_t read_num)
{
    rt_pin_write(LED_RED, PIN_HIGH);
    rt_uint8_t touch_num = 0;
    rt_uint8_t advanced_info = 0U;
    rt_size_t max_points = s_touch_max_points;
    rt_size_t coordinate_bytes;

    int16_t input_x = 0;
    int16_t input_y = 0;
    static uint16_t count = 0;

    (void)touch;
    if (max_points == 0U || max_points > ST7102_MAX_TOUCH)
        max_points = ST7102_MAX_TOUCH;
    if (read_num < max_points) max_points = read_num;

    /* Sitronix report protocol is a two-stage read.  Every register address is
     * 16-bit: first read Advanced Touch Info at 0x0010, then, only when bit 3
     * (With Coord) is set, read all supported point slots from 0x0014 through
     * the final slot.  Reading that final coordinate register acknowledges the
     * frame and clears INT.  Register 0x0010 is read-only and must not be
     * cleared with a write. */
    if (ST7102_read_reg16(&ST7102_client, ST7102_READ_STATUS,
                          &advanced_info, 1U) != RT_EOK)
    {
        touch_num = ST7102_report_held_touches(buf, max_points);
        goto exit_;
    }

    rt_memset(read_buf, 0, sizeof(read_buf));
    read_buf[0] = advanced_info;
    if ((advanced_info & 0x08U) == 0U)
    {
        /* A successfully read Advanced Touch Info without With Coord is a
         * definitive release report.  Keeping the previous coordinates here
         * leaves LVGL permanently pressed after the first tap and makes the UI
         * appear to hang.  Only transport failures use the short held state. */
        ST7102_release_all_touches(buf, max_points);
        touch_num = 0U;
        goto exit_;
    }

    coordinate_bytes = max_points * ST7102_REPORT_POINT_LEN;
    if (ST7102_read_reg16(&ST7102_client, ST7102_REPORT_POINT0_REG,
                          &read_buf[ST7102_REPORT_HEADER_LEN],
                          (rt_uint8_t)coordinate_bytes) != RT_EOK)
    {
        touch_num = ST7102_report_held_touches(buf, max_points);
        goto exit_;
    }
    s_touch_diagnostics.coordinate_frames++;

    for (count = 0; count < max_points; count++)
    {
        if ((read_buf[0x04 + count * 7] & 0x80U) != 0U)
        {
            input_x = (read_buf[(7 * count) + 0x04] & 0x3F) << 8 | read_buf[(7 * count) + 0x05];
            input_y = (read_buf[(7 * count) + 0x06] & 0x3F) << 8 | read_buf[(7 * count) + 0x07];
            input_x = ST7102_position_filter(pre_x[count], input_x);
            input_y = ST7102_position_filter(pre_y[count], input_y);
            s_release_candidate[count] = 0U;
            ST7102_touch_down(buf, count, input_x, input_y, 0);
            touch_num++;
            s_touch_diagnostics.press_reports++;
        }
        else
        {
            if (s_tp_dowm[count] != 0U &&
                ++s_release_candidate[count] < ST7102_RELEASE_CONFIRM_FRAMES)
            {
                ST7102_touch_down(buf, count, pre_x[count], pre_y[count], pre_w[count]);
                touch_num++;
                s_touch_diagnostics.held_reports++;
            }
            else
            {
                if (s_tp_dowm[count] != 0U) s_touch_diagnostics.release_reports++;
                ST7102_touch_up(buf, count);
            }
        }
    }

    for (; count < ST7102_MAX_TOUCH; count++)
    {
        ST7102_touch_up(buf, count);
    }
#ifdef BSP_LVGL_TOUCH_DEBUG
    if (read_buf[0] != 0)
    {
        static rt_tick_t last_debug_tick;
        rt_tick_t now = rt_tick_get();
        if ((rt_tick_t)(now - last_debug_tick) >= rt_tick_from_millisecond(100))
        {
            rt_kprintf("st7102 status=0x%02x p0=0x%02x irq=%d x=%d y=%d touch=%d\n",
                       read_buf[0], read_buf[0x09], input_x, input_y,
                       rt_pin_read(ST7102_IRQ_PIN), touch_num);
            last_debug_tick = now;
        }
    }
#endif
exit_:
    return touch_num;
}

void ST7102_get_diagnostics(st7102_touch_diagnostics_t *diagnostics)
{
    if (diagnostics != RT_NULL) *diagnostics = s_touch_diagnostics;
}

static rt_err_t ST7102_control(struct rt_touch_device *touch, int cmd, void *arg)
{
    if (cmd == RT_TOUCH_CTRL_GET_INFO)
    {
        return ST7102_get_info(&ST7102_client, arg);
    }
    return RT_EOK;
}

static struct rt_touch_ops ST7102_touch_ops =
{
    .touch_readpoint = ST7102_read_point,
    .touch_control = ST7102_control,
};

int rt_hw_ST7102_init(const char *name, struct rt_touch_config *cfg)
{
    struct rt_touch_device *touch_device = RT_NULL;
    struct rt_touch_info info;

    touch_device = (struct rt_touch_device *)rt_malloc(sizeof(struct rt_touch_device));
    if (touch_device == RT_NULL)
    {
        LOG_E("touch device malloc fail");
        return -RT_ERROR;
    }
    rt_memset((void *)touch_device, 0, sizeof(struct rt_touch_device));

    /* Hardware initialization */
    rt_pin_mode(*(rt_uint8_t *)cfg->user_data, PIN_MODE_OUTPUT); /* Reset Pin */
    rt_pin_mode(cfg->irq_pin.pin, PIN_MODE_OUTPUT);
    rt_pin_write(cfg->irq_pin.pin, PIN_LOW);
    rt_pin_write(*(rt_uint8_t *)cfg->user_data, PIN_LOW);
    rt_thread_mdelay(10);

    rt_pin_write(*(rt_uint8_t *)cfg->user_data, PIN_HIGH);
    rt_thread_mdelay(20);
    rt_pin_mode(cfg->irq_pin.pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_write(cfg->irq_pin.pin, PIN_HIGH);
    rt_thread_mdelay(30);

    ST7102_client.client_addr = TOUCH_SLAVE_ADDRESS;
    ST7102_client.bus = (struct rt_i2c_bus_device *)rt_device_find(cfg->dev_name);

    if (ST7102_client.bus == RT_NULL)
    {
        LOG_E("Can't find %s device", cfg->dev_name);
        rt_free(touch_device);
        return -RT_ERROR;
    }

    if (rt_device_open((rt_device_t)ST7102_client.bus, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("open %s device failed", cfg->dev_name);
        rt_free(touch_device);
        return -RT_ERROR;
    }

    rt_memset(&info, 0, sizeof(info));
    if (ST7102_get_info(&ST7102_client, &info) != RT_EOK)
    {
        /* A touch-panel fault must not stop display/UI startup.  Keep polling
         * the report register with conservative geometry so the controller
         * can recover and the shell remains available for diagnostics. */
        LOG_W("controller information unavailable; using safe defaults");
        info.range_x = 480U;
        info.range_y = 800U;
        info.point_num = ST7102_MAX_TOUCH;
        s_touch_max_points = ST7102_MAX_TOUCH;
    }

    /* Register touch device */
    touch_device->info.type = RT_TOUCH_TYPE_CAPACITANCE;
    touch_device->info.vendor = RT_TOUCH_VENDOR_GT;
    touch_device->info.range_x = info.range_x;
    touch_device->info.range_y = info.range_y;
    touch_device->info.point_num = info.point_num;
    rt_memcpy(&touch_device->config, cfg, sizeof(struct rt_touch_config));
    touch_device->ops = &ST7102_touch_ops;
    rt_hw_touch_register(touch_device, name, RT_DEVICE_FLAG_INT_RX, RT_NULL);

    return RT_EOK;
}

rt_err_t ST7102_get_single_touch(rt_int16_t *touch_x, rt_int16_t *touch_y)
{
    struct rt_touch_data touch_data[ST7102_MAX_TOUCH];
    rt_size_t touch_num;

    if (ST7102_client.bus == RT_NULL)
    {
        LOG_E("ST7102 i2c bus not initialized");
        return -RT_ERROR;
    }

    /* Poll the complete controller status table on every LVGL input sample.
     * INT is not a reliable data-ready gate on the current Edgi-Talk wiring:
     * treating its high idle level as "no touch" suppresses real frames. */

    rt_memset(touch_data, 0, sizeof(touch_data));

    touch_num = ST7102_read_point(RT_NULL, touch_data, ST7102_MAX_TOUCH);

    if (touch_num > 0)
    {
        *touch_x = touch_data[0].x_coordinate;
        *touch_y = touch_data[0].y_coordinate;

        // rt_kprintf("Single touch: X=%d, Y=%d\n", *touch_x, *touch_y);
        return RT_EOK;
    }
    else
    {
        return -RT_ERROR;
    }
}

rt_err_t ST7102_get_resolution(rt_uint16_t *width, rt_uint16_t *height)
{
    struct rt_touch_info info;
    rt_err_t result;
    if (width == RT_NULL || height == RT_NULL || ST7102_client.bus == RT_NULL)
        return -RT_EINVAL;
    rt_memset(&info, 0, sizeof(info));
    result = ST7102_get_info(&ST7102_client, &info);
    if (result != RT_EOK || info.range_x < 2U || info.range_y < 2U)
        return -RT_ERROR;
    *width = info.range_x;
    *height = info.range_y;
    return RT_EOK;
}

int rt_hw_ST7102_port(void)
{
    struct rt_touch_config cfg;
    rt_uint8_t rst_pin;

    rst_pin = ST7102_RST_PIN;
    cfg.dev_name = "i2c1";
    cfg.irq_pin.pin = ST7102_IRQ_PIN;
    cfg.irq_pin.mode = PIN_MODE_INPUT_PULLDOWN;
    cfg.user_data = &rst_pin;

    return rt_hw_ST7102_init("ST7102", &cfg);
}

int soft_reset_test(void)
{
    ST7102_soft_reset(&ST7102_client);
    return 0;
}
MSH_CMD_EXPORT(soft_reset_test, Demo);

static int st7102_probe(void)
{
    struct rt_touch_info info;
    st7102_touch_diagnostics_t diagnostics;
    rt_err_t result;
    rt_uint8_t report_page[16];
    rt_uint8_t advanced[4];
    rt_uint8_t legacy_page[16];
    rt_uint8_t legacy_reg = 0U;

    rt_memset(&info, 0, sizeof(info));
    rt_memset(&diagnostics, 0, sizeof(diagnostics));
    rt_memset(report_page, 0, sizeof(report_page));
    rt_memset(advanced, 0, sizeof(advanced));
    rt_memset(legacy_page, 0, sizeof(legacy_page));
    result = ST7102_get_info(&ST7102_client, &info);
    (void)ST7102_read_reg16(&ST7102_client, 0x0000U,
                            report_page, sizeof(report_page));
    (void)ST7102_read_reg16(&ST7102_client, 0x0010U,
                            advanced, sizeof(advanced));
    (void)ST7102_read_regs(&ST7102_client, &legacy_reg, 1U,
                           legacy_page, sizeof(legacy_page));
    ST7102_get_diagnostics(&diagnostics);
    rt_kprintf("ST7102 probe: i2c=%s irq=%d range=%ux%u points=%u "
               "frames=%lu held=%lu press=%lu release=%lu\n",
               result == RT_EOK ? "ok" : "error",
               rt_pin_read(ST7102_IRQ_PIN),
               (unsigned int)info.range_x,
               (unsigned int)info.range_y,
               (unsigned int)info.point_num,
               (unsigned long)diagnostics.coordinate_frames,
               (unsigned long)diagnostics.held_reports,
               (unsigned long)diagnostics.press_reports,
               (unsigned long)diagnostics.release_reports);
    rt_kprintf("ST7102 raw: fw=%02x status=%02x control=%02x "
               "sensing=%02x%02x revision=%02x%02x%02x%02x adv=%02x/%02x/%02x/%02x\n",
               report_page[0], report_page[1], report_page[2],
               report_page[0x0A], report_page[0x0B],
               report_page[0x0C], report_page[0x0D],
               report_page[0x0E], report_page[0x0F],
               advanced[0], advanced[1], advanced[2], advanced[3]);
    rt_kprintf("ST7102 legacy-8bit raw: "
               "%02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               legacy_page[0], legacy_page[1], legacy_page[2], legacy_page[3],
               legacy_page[4], legacy_page[5], legacy_page[6], legacy_page[7],
               legacy_page[8], legacy_page[9], legacy_page[10], legacy_page[11],
               legacy_page[12], legacy_page[13], legacy_page[14], legacy_page[15]);
    return result;
}
MSH_CMD_EXPORT(st7102_probe, Probe the ST7102 touch controller);
