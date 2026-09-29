/* ft_gatt_hash.h - M8.x: GATT Database Hash (0x2B2A, Robust Caching)
 *
 * 规范: Core Spec Supplement / GATT 3.3.2 —— Database Hash = 对
 * Server Configuration 之前所有属性 (句柄 0x0001 起) 的 AES-CMAC
 * (key=128bit 零)。Android Robust Caching 依赖它判定缓存有效性。
 *
 * 实现: 对齐 btstack att_db_util.c 官方算法 —— 仅结构属性参与哈希
 * (Service/Characteristic 声明贡献 handle+uuid+value, CCCD 等描述符
 * 贡献 handle+uuid; 特征值属性与 UUID128 属性不参与), btstack_crypto
 * generator CMAC, 结果按规范 little-endian 序写入 0x2B2A 特征值缓冲。
 * 表内容编译期固定 -> hash 固定, 缓存命中语义正确。
 * 验证: 板上计算结果与 compile_gatt.py 生成表时独立计算的嵌入值逐字节一致
 * (77c8ab66c4cdfb26047711ed31e9547e @ 2026-09-29 固件)。
 */
#ifndef FEATHERTALK_GATT_HASH_H
#define FEATHERTALK_GATT_HASH_H

#include <stdint.h>

/* 蓝牙栈 WORKING 后调用一次: 启动 CMAC 计算 (异步, 完成后 hash 可读) */
void ft_gatt_hash_start(void);

/* 0x2B2A 特征读回调: 返回 16 字节 hash; 未就绪返回 0 (读取长度 0) */
uint16_t ft_gatt_hash_read(uint16_t offset, uint8_t *buffer, uint16_t buffer_size);

#endif /* FEATHERTALK_GATT_HASH_H */
