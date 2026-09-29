/* ft_gatt_hash.c - M8.x: GATT Database Hash 计算 (Robust Caching, 0x2B2A)
 *
 * 算法对齐 btstack att_db_util.c 官方实现 (att_db_util_hash_get_next):
 *   Database Hash = AES-CMAC(key = 128bit zero, message = 结构属性字节序列)
 *   - 从 profile_data[1] 开始 (跳过 ATT DB Version 字节)
 *   - 仅以下 16-bit UUID 结构属性参与:
 *       with_value:    Primary/Secondary/Included Service(0x2800-02),
 *                      Characteristic(0x2803), Char Extended Properties(0x2900)
 *                      -> 贡献 [4..size) = handle(2)+uuid(2)+value
 *       without_value: User Description(0x2901), CCCD(0x2902),
 *                      Server Config(0x2903), Aggregate/Format(0x2904-05)
 *                      -> 贡献 [4..8) = handle(2)+uuid(2)
 *   - 特征值属性 (含 Database Hash 值本身) 与 UUID128 属性不参与
 *   - 权限字段不参与
 *
 * CMAC 用 btstack_crypto generator 模式逐字节喂 (异步完成回调置 s_hash_valid)。
 */
#include <rtthread.h>
#include <string.h>

#include "btstack_crypto.h"
#include "btstack_util.h"
#include "ft_gatt.h"    /* profile_data: static const, 本单元私有拷贝 (字节一致) */

#define FT_GATT_HASH_LEN      16U
#define FT_GATT_HASH_KEY_ZERO {0}

/* GATT 声明/描述符 UUID16 (bluetooth_gatt.h 同名常量, 硬编码避免头文件依赖) */
#define FT_UUID_PRIMARY_SERVICE            0x2800u
#define FT_UUID_SECONDARY_SERVICE          0x2801u
#define FT_UUID_INCLUDE_SERVICE            0x2802u
#define FT_UUID_CHARACTERISTIC             0x2803u
#define FT_UUID_CHAR_EXTENDED_PROPERTIES   0x2900u
#define FT_UUID_CHAR_USER_DESCRIPTION      0x2901u
#define FT_UUID_CCCD                       0x2902u
#define FT_UUID_SERVER_CHAR_CONFIGURATION  0x2903u
#define FT_UUID_CHAR_AGGREGATE_FORMAT      0x2904u
#define FT_UUID_CHAR_FORMAT                0x2905u

#define FT_ATT_PROPERTY_UUID128 0x0200u

static btstack_crypto_aes128_cmac_t s_cmac_request;
static uint8_t  s_hash[FT_GATT_HASH_LEN];
static volatile rt_bool_t s_hash_valid;
static rt_bool_t s_started;

/* generator 游标状态 (与 att_db_util_hash_att_ptr 同构) */
static const uint8_t *s_ptr;
static uint16_t s_off;
static uint16_t s_avail;

static void ft_gatt_hash_done(void *arg)
{
    (void)arg;
    /* btstack_crypto 输出为标准 CMAC 摘要序; 规范 (Core Supp Part G 3.3.2,
     * compile_gatt.py 生成占位值时同样 "reverse hash to get little endian")
     * 要求特征值按 little-endian 八位组序发送 -> 反转一次 */
    for (int i = 0; i < FT_GATT_HASH_LEN / 2; i++)
    {
        uint8_t tmp = s_hash[i];
        s_hash[i] = s_hash[FT_GATT_HASH_LEN - 1U - i];
        s_hash[FT_GATT_HASH_LEN - 1U - i] = tmp;
    }
    s_hash_valid = RT_TRUE;
    rt_kprintf("[HASH] database hash ready\n");
}

static rt_bool_t ft_hash_include_with_value(uint16_t uuid16)
{
    switch (uuid16)
    {
    case FT_UUID_PRIMARY_SERVICE:
    case FT_UUID_SECONDARY_SERVICE:
    case FT_UUID_INCLUDE_SERVICE:
    case FT_UUID_CHARACTERISTIC:
    case FT_UUID_CHAR_EXTENDED_PROPERTIES:
        return RT_TRUE;
    default:
        return RT_FALSE;
    }
}

static rt_bool_t ft_hash_include_without_value(uint16_t uuid16)
{
    switch (uuid16)
    {
    case FT_UUID_CHAR_USER_DESCRIPTION:
    case FT_UUID_CCCD:
    case FT_UUID_SERVER_CHAR_CONFIGURATION:
    case FT_UUID_CHAR_AGGREGATE_FORMAT:
    case FT_UUID_CHAR_FORMAT:
        return RT_TRUE;
    default:
        return RT_FALSE;
    }
}

static void ft_hash_fetch_next(void)
{
    for (;;)
    {
        uint16_t size = little_endian_read_16(s_ptr, 0);
        if (size == 0U)
        {
            s_avail = 0U;
            return;
        }
        uint16_t flags = little_endian_read_16(s_ptr, 2);
        if ((flags & FT_ATT_PROPERTY_UUID128) == 0U)
        {
            uint16_t uuid16 = little_endian_read_16(s_ptr, 6);
            if (ft_hash_include_with_value(uuid16))
            {
                s_off = 4U;
                s_avail = size - 4U;
                return;
            }
            if (ft_hash_include_without_value(uuid16))
            {
                s_off = 4U;
                s_avail = 4U;
                return;
            }
        }
        s_ptr += size;
    }
}

static uint8_t ft_hash_get_next(void)
{
    if (s_avail == 0U)
    {
        ft_hash_fetch_next();
    }
    uint8_t next = s_ptr[s_off++];
    s_avail--;
    if (s_avail == 0U)
    {
        s_ptr += little_endian_read_16(s_ptr, 0);
    }
    return next;
}

static uint8_t ft_hash_get(uint16_t offset)
{
    (void)offset;
    return ft_hash_get_next();
}

void ft_gatt_hash_start(void)
{
    if (s_started) return;
    s_started = RT_TRUE;

    /* 先统计参与哈希的总字节 (与 fetch 规则一致), 供日志核对 */
    uint32_t total = 0;
    const uint8_t *p = profile_data + 1;   /* 跳过 ATT DB Version */
    for (;;)
    {
        uint16_t size = little_endian_read_16(p, 0);
        if (size == 0U) break;
        uint16_t flags = little_endian_read_16(p, 2);
        if ((flags & FT_ATT_PROPERTY_UUID128) == 0U)
        {
            uint16_t uuid16 = little_endian_read_16(p, 6);
            if (ft_hash_include_with_value(uuid16))
            {
                total += size - 4U;
            }
            else if (ft_hash_include_without_value(uuid16))
            {
                total += 4U;
            }
        }
        p += size;
    }

    /* generator 游标初始化 */
    s_ptr = profile_data + 1;
    s_off = 0U;
    s_avail = 0U;

    static const uint8_t zero_key[16] = FT_GATT_HASH_KEY_ZERO;
    btstack_crypto_aes128_cmac_generator(&s_cmac_request, zero_key,
                                         (uint16_t)total,
                                         &ft_hash_get,
                                         s_hash, &ft_gatt_hash_done, RT_NULL);
    rt_kprintf("[HASH] computing over %lu bytes\n", (unsigned long)total);
}

uint16_t ft_gatt_hash_read(uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    if (!s_hash_valid) return 0;
    if (offset >= FT_GATT_HASH_LEN) return 0;
    uint16_t avail = FT_GATT_HASH_LEN - offset;
    if (buffer == RT_NULL || buffer_size < avail) return 0;
    memcpy(buffer, &s_hash[offset], avail);
    return avail;
}
