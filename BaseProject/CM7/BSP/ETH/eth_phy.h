/**
 * @file    eth_phy.h
 * @brief   LAN8720 以太网 PHY 板级封装（MDIO 扫描、链路状态、诊断打印）
 * @note    上层 LwIP / ETH 驱动通过本接口初始化 PHY，并查询当前协商结果。
 *          PHY 地址可能因硬件跳线不同，初始化时会在 MDIO 总线上自动扫描。
 */

#ifndef ETH_PHY_H
#define ETH_PHY_H

#include <stdint.h>
#include "lan8720.h"

int32_t eth_phy_init(void);                        // 初始化 PHY，返回初始化结果
uint32_t eth_phy_get_address(void);                // 获取当前 PHY 地址
int32_t eth_phy_get_link_state(void);              // 获取当前链路状态
const char *eth_phy_link_state_str(int32_t state); // 将链路状态码转为可读字符串（用于串口日志）
void eth_phy_print_diag(void);                     // 打印 PHY 诊断：当前地址、最近 BCR，以及地址 0~3 的 ID/BCR/BSR

#endif /* ETH_PHY_H */
