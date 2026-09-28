/**
  ******************************************************************************
  * @file    bl24c16f.c
  * @brief   BL24C16F I2C EEPROM 驱动实现
  *
  * 寻址说明（与手册 Figure 5 对应）：
  *   8 位设备地址：| 1 | 0 | 1 | 0 | A10 | A9 | A8 | R/W |
  *   - 高 4 位固定为 1010
  *   - A10/A9/A8 为存储地址的高 3 位，决定使用哪个 I2C 设备地址
  *   - R/W：0=写，1=读
  *   - 字地址字节 = 存储地址低 8 位（A7~A0）
  *
  * 地址计算示例：
  *   存储地址 0x0156 → 设备写地址 0xA0，字地址 0x56
  *   存储地址 0x0234 → 设备写地址 0xA4，字地址 0x34
  ******************************************************************************
  */

#include "bl24c16f.h"

/* -------------------------------------------------------------------------- */
/*                              内部辅助函数                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief  根据存储地址计算 8 位 I2C 设备地址（含 R/W=0 的写方向）
 * @param  addr  存储器地址（0x0000 ~ 0x07FF）
 * @return 8 位设备地址，范围 0xA0 ~ 0xAE
 *
 * @note   公式：0xA0 | ((addr >> 8) & 0x07) << 1
 *         addr >> 8 取出 A10/A9/A8，左移 1 位填入设备地址的 B3~B1 位
 */
static uint16_t BL24C16F_GetDevAddr(uint16_t addr)
{
  return (uint16_t)(BL24C16F_DEV_ADDR_BASE | (((addr >> 8U) & 0x07U) << 1U));
}

/**
 * @brief  等待 EEPROM 内部写周期完成
 * @param  hdev  驱动句柄指针
 * @param  addr  当前写入的存储地址（用于确定轮询哪个设备地址块）
 * @retval HAL_OK      写周期完成，设备已就绪
 * @retval HAL_TIMEOUT 等待超时
 *
 * @note   页写或字节写结束后，芯片需约 5ms 内部写入时间，
 *         期间不响应 I2C，通过 IsDeviceReady 轮询检测
 */
static HAL_StatusTypeDef BL24C16F_WaitWriteCycle(BL24C16F_HandleTypeDef *hdev, uint16_t addr)
{
  uint32_t tickstart = HAL_GetTick();
  uint16_t dev_addr = BL24C16F_GetDevAddr(addr);

  while (HAL_I2C_IsDeviceReady(hdev->hi2c, dev_addr, 1U, 1U) != HAL_OK)
  {
    if ((HAL_GetTick() - tickstart) > hdev->timeout)
    {
      return HAL_TIMEOUT;
    }
  }

  return HAL_OK;
}

/**
 * @brief  校验地址和长度参数是否合法
 * @param  addr  起始存储地址
 * @param  len   读写字节数
 * @retval HAL_OK    参数合法
 * @retval HAL_ERROR len 为 0 或 addr+len 超出芯片容量
 */
static HAL_StatusTypeDef BL24C16F_CheckParams(uint16_t addr, uint16_t len)
{
  if ((len == 0U) || ((uint32_t)addr + len > BL24C16F_TOTAL_SIZE))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

/* -------------------------------------------------------------------------- */
/*                              对外接口函数                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief  初始化 EEPROM 驱动句柄，绑定 I2C 外设并设置默认超时
 */
void BL24C16F_Init(BL24C16F_HandleTypeDef *hdev, I2C_HandleTypeDef *hi2c)
{
  hdev->hi2c = hi2c;
  hdev->timeout = BL24C16F_DEFAULT_TIMEOUT;
}

/**
 * @brief  检测设备是否在线（使用第一地址块 0xA0 进行探测）
 */
HAL_StatusTypeDef BL24C16F_IsReady(BL24C16F_HandleTypeDef *hdev)
{
  if ((hdev == NULL) || (hdev->hi2c == NULL))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_IsDeviceReady(hdev->hi2c, BL24C16F_DEV_ADDR_BASE, 3U, hdev->timeout);
}

/**
 * @brief  读取单字节（封装 BL24C16F_Read）
 */
HAL_StatusTypeDef BL24C16F_ReadByte(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t *data)
{
  return BL24C16F_Read(hdev, addr, data, 1U);
}

/**
 * @brief  写入单字节（封装 BL24C16F_Write）
 */
HAL_StatusTypeDef BL24C16F_WriteByte(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t data)
{
  return BL24C16F_Write(hdev, addr, &data, 1U);
}

/**
 * @brief  从指定地址连续读取多个字节
 *
 * @note   跨 256 字节地址块时自动分段：
 *         每段内设备地址相同，字地址从 A7~A0 递增；
 *         进入下一块时设备地址需切换（如 0xA0 → 0xA2）
 */
HAL_StatusTypeDef BL24C16F_Read(BL24C16F_HandleTypeDef *hdev, uint16_t addr, uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;

  if ((hdev == NULL) || (hdev->hi2c == NULL) || (data == NULL))
  {
    return HAL_ERROR;
  }

  status = BL24C16F_CheckParams(addr, len);
  if (status != HAL_OK)
  {
    return status;
  }

  while (len > 0U)
  {
    /* 当前地址块内剩余可读字节数（防止跨 256 字节块） */
    uint16_t block_remain = BL24C16F_BLOCK_SIZE - (addr & (BL24C16F_BLOCK_SIZE - 1U));
    uint16_t chunk = (len < block_remain) ? len : block_remain;
    uint16_t dev_addr = BL24C16F_GetDevAddr(addr);

    status = HAL_I2C_Mem_Read(hdev->hi2c,
                              dev_addr,                    /* 8 位设备地址 */
                              (uint16_t)(addr & 0xFFU),    /* 字地址 A7~A0 */
                              I2C_MEMADD_SIZE_8BIT,
                              data,
                              chunk,
                              hdev->timeout);
    if (status != HAL_OK)
    {
      return status;
    }

    addr += chunk;
    data += chunk;
    len -= chunk;
  }

  return HAL_OK;
}

/**
 * @brief  向指定地址连续写入多个字节
 *
 * @note   自动处理两种边界：
 *         1. 页边界（16 字节）：单次页写不可跨页，超出部分拆分到下一页
 *         2. 每次写入后等待内部写周期完成
 */
HAL_StatusTypeDef BL24C16F_Write(BL24C16F_HandleTypeDef *hdev, uint16_t addr, const uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;

  if ((hdev == NULL) || (hdev->hi2c == NULL) || (data == NULL))
  {
    return HAL_ERROR;
  }

  status = BL24C16F_CheckParams(addr, len);
  if (status != HAL_OK)
  {
    return status;
  }

  while (len > 0U)
  {
    /* 当前页内剩余可写字节数（防止跨 16 字节页边界） */
    uint16_t page_offset = addr & (BL24C16F_PAGE_SIZE - 1U);
    uint16_t page_remain = BL24C16F_PAGE_SIZE - page_offset;
    uint16_t chunk = (len < page_remain) ? len : page_remain;
    uint16_t dev_addr = BL24C16F_GetDevAddr(addr);

    status = HAL_I2C_Mem_Write(hdev->hi2c,
                               dev_addr,                    /* 8 位设备地址 */
                               (uint16_t)(addr & 0xFFU),    /* 字地址 A7~A0 */
                               I2C_MEMADD_SIZE_8BIT,
                               (uint8_t *)data,
                               chunk,
                               hdev->timeout);
    if (status != HAL_OK)
    {
      return status;
    }

    /* 等待本次写入的内部写周期结束 */
    status = BL24C16F_WaitWriteCycle(hdev, addr);
    if (status != HAL_OK)
    {
      return status;
    }

    addr += chunk;
    data += chunk;
    len -= chunk;
  }

  return HAL_OK;
}
