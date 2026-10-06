/**
 ****************************************************************************************************
 * @file        ov5640_dcmi.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       OV5640模块DCMI接口驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 阿波罗 H743开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "ov5640_dcmi.h"
#include "dcmi.h"
#include "mylog.h"

/* OV5640模块DCMI接口数据结构体 */
static struct
{
    volatile uint8_t frame_sem;
    volatile uint16_t lines;
    volatile uint16_t frame_lines;
} g_ov5640_dcmi_sta = {0};

void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    (void)hdcmi;
    g_ov5640_dcmi_sta.lines++;
}

/**
 * @brief       OV5640模块DCMI接口帧中断回调函数
 * @param       hdcmi: DCMI句柄
 * @retval      无
 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    __HAL_DCMI_CLEAR_FLAG(hdcmi, DCMI_IT_FRAME); // 清除帧中断标志位
    g_ov5640_dcmi_sta.frame_lines = g_ov5640_dcmi_sta.lines;
    g_ov5640_dcmi_sta.frame_sem = 1;
}

/**
 * @brief       开始OV5640模块DCMI接口DMA传输
 * @param       dts_addr        : 帧数据的接收目的地址
 *              meminc          : DMA_MINC_DISABLE: 帧数据接收的目的地址不自动增加
 *                                DMA_MINC_ENABLE : 帧数据接收的目的地址自动增加
 *              memdataalignment: DMA_MDATAALIGN_BYTE    : 帧数据接收缓冲的位宽为8比特
 *                                DMA_MDATAALIGN_HALFWORD: 帧数据接收缓冲的位宽为16比特
 *                                DMA_MDATAALIGN_WORD    : 帧数据接收缓冲的位宽为32比特
 *              len             : DMA传输的数据单元数量
 * @retval      OV5640_EOK: 成功
 *              OV5640_DCMI_ERROR: 失败
 *              OV5640_DCMI_ETIMEOUT: 超时
 */
uint8_t ov5640_dcmi_start(uint32_t dts_addr, uint32_t meminc, uint32_t memdataalignment, uint32_t len)
{
    uint32_t start_tick = HAL_GetTick();
    /* 清空帧接收完成标记，并使能DCMI帧接收中断。 */
    g_ov5640_dcmi_sta.frame_sem = 0;
    g_ov5640_dcmi_sta.lines = 0;
    g_ov5640_dcmi_sta.frame_lines = 0;
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_FRAME);
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT, dts_addr, len) != HAL_OK)
    {
        LOG_ERROR("CM7","OV5640 start DCMI DMA failed");
        HAL_DCMI_Stop(&hdcmi);
        return OV5640_DCMI_ERROR;
    }
    
    /* 等待传输完成 */
    while (g_ov5640_dcmi_sta.frame_sem == 0)
    {
        if ((HAL_GetTick() - start_tick) > 1000U)
        {
            LOG_ERROR("CM7","OV5640 DCMI DMA transfer timeout");
            HAL_DCMI_Stop(&hdcmi);
            return OV5640_DCMI_ETIMEOUT;
        }
    }

    if (hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE ||
        (hdcmi.DMA_Handle->ErrorCode & ~HAL_DMA_ERROR_FE) != HAL_DMA_ERROR_NONE ||
        hdcmi.XferCount != hdcmi.XferTransferNumber)
    {
        LOG_ERROR("CM7", "OV5640 DCMI error=0x%lX DMA error=0x%lX", (unsigned long)hdcmi.ErrorCode, (unsigned long)hdcmi.DMA_Handle->ErrorCode);
        LOG_ERROR("CM7", "DMA addr0=0x%08lX addr1=0x%08lX pixels first=%04X last=%04X",
                  (unsigned long)((DMA_Stream_TypeDef *)hdcmi.DMA_Handle->Instance)->M0AR,
                  (unsigned long)((DMA_Stream_TypeDef *)hdcmi.DMA_Handle->Instance)->M1AR,
                  ((volatile uint16_t *)dts_addr)[0], ((volatile uint16_t *)dts_addr)[800U * 480U - 1U]);
        HAL_DCMI_Stop(&hdcmi);
        return OV5640_DCMI_ERROR;
    }
    HAL_DCMI_Stop(&hdcmi);

    return OV5640_EOK;
}
