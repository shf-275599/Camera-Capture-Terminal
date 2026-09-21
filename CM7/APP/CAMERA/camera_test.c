/**
 * @file    camera_test.c
 * @brief   OV5640摄像头简单初始化测试
 *          通过I2C1读取芯片ID，验证通信是否正常
 */
#include "camera_test.h"
#include "ov5640.h"
#include "i2c.h"
#include <stdio.h>

#define OV5640_ADDR  0x3C

/**
 * @brief   I2C总线扫描，打印所有响应的设备地址
 */
static void i2c_scan(void)
{
    uint8_t addr;
    uint8_t dummy;
    HAL_StatusTypeDef result;
    uint8_t found = 0;

    printf("CM7: I2C1 scan...\r\n");
    for (addr = 0x08; addr < 0x78; addr++)
    {
        result = HAL_I2C_Master_Transmit(&hi2c1, addr << 1, &dummy, 0, 50);
        if (result == HAL_OK)
        {
            printf("CM7:   Found device at 0x%02X (7bit)\r\n", addr);
            found++;
        }
    }
    if (found == 0)
    {
        printf("CM7:   No I2C device found!\r\n");
    }
    else
    {
        printf("CM7:   Total %d device(s) found\r\n", found);
    }
}

/**
 * @brief   直接读取OV5640芯片ID（不依赖驱动初始化流程）
 */
static void ov5640_read_chip_id_direct(void)
{
    uint8_t id_high = 0, id_low = 0;
    HAL_StatusTypeDef ret;

    printf("CM7: OV5640 direct chip ID read...\r\n");

    ret = HAL_I2C_Mem_Read(&hi2c1, OV5640_ADDR << 1, 0x300A, I2C_MEMADD_SIZE_16BIT, &id_high, 1, 100);
    printf("CM7:   HAL_I2C_Mem_Read(0x300A) = %d, val=0x%02X\r\n", ret, id_high);

    ret = HAL_I2C_Mem_Read(&hi2c1, OV5640_ADDR << 1, 0x300B, I2C_MEMADD_SIZE_16BIT, &id_low, 1, 100);
    printf("CM7:   HAL_I2C_Mem_Read(0x300B) = %d, val=0x%02X\r\n", ret, id_low);

    printf("CM7:   Chip ID = 0x%04X (expected 0x5640)\r\n", (id_high << 8) | id_low);
}

void camera_init_test(void)
{
    uint8_t ret;

    printf("CM7: OV5640 camera init test start\r\n");

    /* 1. I2C总线扫描 */
    i2c_scan();

    /* 2. 直接读取芯片ID */
    ov5640_read_chip_id_direct();

    /* 3. 完整初始化 */
    printf("CM7: OV5640 full init...\r\n");
    ret = ov5640_init();
    if (ret == OV5640_EOK)
    {
        printf("CM7: OV5640 init OK, chip ID verified\r\n");
    }
    else
    {
        printf("CM7: OV5640 init FAILED (err=%d)\r\n", ret);
    }
}
