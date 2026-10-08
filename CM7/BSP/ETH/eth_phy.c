/**
 * @file    eth_phy.c
 * @brief   LAN8720 PHY 驱动胶水层：HAL ETH MDIO ↔ ST lan8720 组件
 * @note    - 通过 lan8720_IOCtx_t 回调把寄存器读写落到 HAL_ETH_Read/WritePHYRegister
 *          - 初始化前扫描 MDIO 0~31，用 PHY ID1/ID2 匹配 LAN8720，避免地址写死
 *          - 本板常见 PHY 地址为 0 或 1，具体以扫描结果为准
 */

#include "eth_phy.h"
#include "lan8720.h"
#include "main.h"
#include <stdio.h>

/** LAN8720 OUI/型号：ID1=0x0007，ID2 高 12 位为 0xC0F（低 4 位为版本号，掩码匹配） */
#define LAN8720_PHY_ID1  0x0007U
#define LAN8720_PHY_ID2  0xC0F0U

static lan8720_Object_t lan8720;
static uint32_t phy_addr = LAN8720_PHY_ADDRESS;
static uint32_t last_bcr = 0U;

/**
 * @brief  MDIO 时钟分频初始化（按当前 HCLK 配置 CSR）
 */
static int32_t phy_io_init(void)
{
  HAL_ETH_SetMDIOClockRange(&heth);
  return 0;
}

/**
 * @brief  MDIO 反初始化（本工程无额外资源，直接返回成功）
 */
static int32_t phy_io_deinit(void)
{
  return 0;
}

/**
 * @brief  读 PHY 寄存器
 * @param  dev_addr  PHY 地址
 * @param  reg_addr  寄存器号（如 BCR=0、BSR=1、ID1=2、ID2=3）
 * @param  reg_val   读出值
 * @retval 0 成功；-1 HAL 失败
 */
static int32_t phy_io_read_reg(uint32_t dev_addr, uint32_t reg_addr, uint32_t *reg_val)
{
  if (HAL_ETH_ReadPHYRegister(&heth, dev_addr, reg_addr, reg_val) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
 * @brief  写 PHY 寄存器
 * @retval 0 成功；-1 HAL 失败
 */
static int32_t phy_io_write_reg(uint32_t dev_addr, uint32_t reg_addr, uint32_t reg_val)
{
  if (HAL_ETH_WritePHYRegister(&heth, dev_addr, reg_addr, reg_val) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
 * @brief  提供毫秒时基给 lan8720 组件做超时等待
 */
static int32_t phy_io_get_tick(void)
{
  return (int32_t)HAL_GetTick();
}

/** 绑定到 ST lan8720 的总线操作表 */
static const lan8720_IOCtx_t lan8720_io =
{
  phy_io_init,
  phy_io_deinit,
  phy_io_write_reg,
  phy_io_read_reg,
  phy_io_get_tick
};

/**
 * @brief  在 MDIO 总线上扫描 LAN8720
 * @param  found_addr  输出匹配到的 PHY 地址
 * @retval LAN8720_STATUS_OK / LAN8720_STATUS_ERROR
 */
static int32_t phy_scan_address(uint32_t *found_addr)
{
  uint32_t id1 = 0U;
  uint32_t id2 = 0U;

  for (uint32_t addr = 0U; addr < 32U; addr++)
  {
    if (phy_io_read_reg(addr, 2U, &id1) != 0)
    {
      continue;
    }
    if (phy_io_read_reg(addr, 3U, &id2) != 0)
    {
      continue;
    }

    /* ID2 低 4 位为芯片版本，比较时屏蔽 */
    if ((id1 == LAN8720_PHY_ID1) && ((id2 & 0xFFF0U) == LAN8720_PHY_ID2))
    {
      *found_addr = addr;
      printf("PHY found at addr %lu, ID=0x%04lX/0x%04lX\r\n",
             (unsigned long)addr, (unsigned long)id1, (unsigned long)id2);
      return LAN8720_STATUS_OK;
    }
  }

  printf("PHY scan: no LAN8720 found on MDIO bus\r\n");
  return LAN8720_STATUS_ERROR;
}

void eth_phy_print_diag(void)
{
  uint32_t id1 = 0U;
  uint32_t id2 = 0U;
  uint32_t bsr = 0U;

  printf("Diag: using PHY addr %lu, last BCR=0x%04lX\r\n",
         (unsigned long)phy_addr, (unsigned long)last_bcr);

  /* 只扫低地址，覆盖本板常见布线，避免刷屏 */
  for (uint32_t addr = 0U; addr < 4U; addr++)
  {
    if (phy_io_read_reg(addr, 2U, &id1) == 0 && phy_io_read_reg(addr, 3U, &id2) == 0)
    {
      phy_io_read_reg(addr, 0U, &last_bcr);
      phy_io_read_reg(addr, 1U, &bsr);
      printf("  addr %lu: ID=0x%04lX/0x%04lX BCR=0x%04lX BSR=0x%04lX\r\n",
             (unsigned long)addr, (unsigned long)id1, (unsigned long)id2,
             (unsigned long)last_bcr, (unsigned long)bsr);
    }
  }
}

int32_t eth_phy_init(void)
{
  uint32_t found_addr = 0U;
  int32_t status;

  if (LAN8720_RegisterBusIO(&lan8720, (lan8720_IOCtx_t *)&lan8720_io) != LAN8720_STATUS_OK)
  {
    return LAN8720_STATUS_ERROR;
  }

  phy_io_init();
  /* 上电后给 PHY 一点稳定时间再扫 ID */
  HAL_Delay(100);

  if (phy_scan_address(&found_addr) != LAN8720_STATUS_OK)
  {
    eth_phy_print_diag();
    return LAN8720_STATUS_READ_ERROR;
  }

  phy_addr = found_addr;
  lan8720.DevAddr = phy_addr;

  status = LAN8720_Init(&lan8720);
  if (status == LAN8720_STATUS_RESET_TIMEOUT)
  {
    phy_io_read_reg(phy_addr, LAN8720_BCR, &last_bcr);
    eth_phy_print_diag();
  }

  return status;
}

uint32_t eth_phy_get_address(void)
{
  return phy_addr;
}

int32_t eth_phy_get_link_state(void)
{
  lan8720.DevAddr = phy_addr;
  return LAN8720_GetLinkState(&lan8720);
}

const char *eth_phy_link_state_str(int32_t state)
{
  switch (state)
  {
  case LAN8720_STATUS_LINK_DOWN:
    return "DOWN";
  case LAN8720_STATUS_100MBITS_FULLDUPLEX:
    return "100M Full";
  case LAN8720_STATUS_100MBITS_HALFDUPLEX:
    return "100M Half";
  case LAN8720_STATUS_10MBITS_FULLDUPLEX:
    return "10M Full";
  case LAN8720_STATUS_10MBITS_HALFDUPLEX:
    return "10M Half";
  case LAN8720_STATUS_AUTONEGO_NOTDONE:
    return "Autoneg...";
  case LAN8720_STATUS_READ_ERROR:
    return "MDIO Read Error";
  case LAN8720_STATUS_WRITE_ERROR:
    return "MDIO Write Error";
  case LAN8720_STATUS_RESET_TIMEOUT:
    return "PHY Reset Timeout";
  default:
    return "Unknown";
  }
}
