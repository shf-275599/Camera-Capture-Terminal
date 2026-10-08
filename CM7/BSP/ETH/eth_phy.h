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

/**
 * @brief  注册 MDIO 总线回调、扫描 LAN8720 地址并完成 PHY 软复位初始化
 * @note   须在 CubeMX 的 ETH 外设（heth）初始化之后调用；失败时会打印诊断信息
 * @retval LAN8720_STATUS_OK            成功
 * @retval LAN8720_STATUS_ERROR         总线注册失败
 * @retval LAN8720_STATUS_READ_ERROR    扫描未找到 LAN8720
 * @retval LAN8720_STATUS_RESET_TIMEOUT PHY 复位超时（仍可能部分可用）
 * @retval 其它 LAN8720_STATUS_*        见 ST lan8720 驱动
 */
int32_t eth_phy_init(void);

/**
 * @brief  获取当前使用的 PHY MDIO 地址（0~31）
 * @note   成功 init 后为扫描到的实际地址；失败前可能仍为默认宏 LAN8720_PHY_ADDRESS
 */
uint32_t eth_phy_get_address(void);

/**
 * @brief  读取当前链路与协商状态
 * @retval LAN8720_STATUS_LINK_DOWN / *_FULLDUPLEX / *_HALFDUPLEX /
 *         LAN8720_STATUS_AUTONEGO_NOTDONE / 读写错误码等
 */
int32_t eth_phy_get_link_state(void);

/**
 * @brief  将链路状态码转为可读字符串（用于串口日志）
 * @param  state  eth_phy_get_link_state() 的返回值
 */
const char *eth_phy_link_state_str(int32_t state);

/**
 * @brief  打印 PHY 诊断：当前地址、最近 BCR，以及地址 0~3 的 ID/BCR/BSR
 * @note   仅用于排障（链路起不来、复位超时、MDIO 不通等）
 */
void eth_phy_print_diag(void);

#endif /* ETH_PHY_H */
