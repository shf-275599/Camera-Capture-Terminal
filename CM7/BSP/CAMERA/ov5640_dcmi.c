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
#include "ltdc.h"
#include "mylog.h"

/* OV5640模块DCMI接口数据结构体 */
static struct
{
    volatile uint8_t frame_sem;
    volatile uint16_t lines;
    volatile uint16_t frame_lines;
} g_ov5640_dcmi_sta = {0};

#define CAMERA_DMA_WORDS (48000U) /* 每段 192000 字节，四段组成一帧 */
#define CAMERA_FRAME_BYTES (CAMERA_DMA_WORDS * 4U * 4U)

static struct
{
    uint32_t addr[3];
    volatile uint8_t display;
    volatile uint8_t capture;
    volatile uint8_t next;
    volatile uint8_t pending; /* 3 表示无待显示帧 */
    volatile uint8_t segment;
    volatile uint8_t active;
    volatile uint8_t error;
    volatile uint8_t reload;
    uint32_t reload_tick;
    volatile uint32_t frame_tick;
    volatile uint32_t frames;
    uint8_t reported;
    volatile uint32_t dma_cr;
    volatile uint32_t dma_ndtr;
    volatile uint32_t dma_m0ar;
    volatile uint32_t dma_m1ar;
    volatile uint32_t dma_lisr;
} g_stream;

static void camera_dma_complete(DMA_HandleTypeDef *hdma)
{
    DMA_Stream_TypeDef *dma = (DMA_Stream_TypeDef *)hdma->Instance;
    uint8_t finished = (dma->CR & DMA_SxCR_CT) ? 0U : 1U;
    uint8_t segment = g_stream.segment;
    uint8_t index;

    if (!g_stream.active)
    {
        return;
    }
    if (finished != (segment & 1U))
    {
        g_stream.error = 1U;
        return;
    }
    if (segment == 2U)
    {
        /* 最后一段正由另一目标接收，提前选定下一帧的空缓冲。 */
        for (index = 0U; index < 3U; index++)
        {
            if (index != g_stream.display && index != g_stream.pending && index != g_stream.capture)
            {
                break;
            }
        }
        g_stream.next = (index < 3U) ? index : g_stream.capture;
    }
    else if (segment == 3U)
    {
        if (g_stream.pending == 3U && g_stream.next != g_stream.capture)
        {
            g_stream.pending = g_stream.capture;
        }
        g_stream.capture = g_stream.next;
        g_stream.frame_tick = HAL_GetTick();
        g_stream.frames++;
    }

    /* DMA 已切到另一目标，只有刚完成的目标可以改址。 */
    index = (uint8_t)((segment + 2U) & 3U);
    /* segment 2 完成时，空闲目标要接收新帧第 0 段。 */
    if (finished == 0U)
    {
        dma->M0AR = g_stream.addr[segment == 2U ? g_stream.next : g_stream.capture] +
                    (uint32_t)index * CAMERA_DMA_WORDS * 4U;
    }
    else
    {
        dma->M1AR = g_stream.addr[g_stream.capture] + (uint32_t)index * CAMERA_DMA_WORDS * 4U;
    }
    g_stream.segment = (uint8_t)((segment + 1U) & 3U);
}

static void camera_dma_error(DMA_HandleTypeDef *hdma)
{
    if ((hdma->ErrorCode & ~HAL_DMA_ERROR_FE) == HAL_DMA_ERROR_NONE)
    {
        return;
    }
    DMA_Stream_TypeDef *dma = (DMA_Stream_TypeDef *)hdma->Instance;
    g_stream.dma_cr = dma->CR;
    g_stream.dma_ndtr = dma->NDTR;
    g_stream.dma_m0ar = dma->M0AR;
    g_stream.dma_m1ar = dma->M1AR;
    g_stream.dma_lisr = DMA1->LISR;
    g_stream.error = 1U;
}

void ov5640_dcmi_stream_log_error(void)
{
    LOG_ERROR("CM7", "CAMERA_DMA segment=%u frames=%lu CR=%08lX NDTR=%lu M0=%08lX M1=%08lX ISR=%08lX",
              g_stream.segment, (unsigned long)g_stream.frames,
              (unsigned long)g_stream.dma_cr, (unsigned long)g_stream.dma_ndtr,
              (unsigned long)g_stream.dma_m0ar, (unsigned long)g_stream.dma_m1ar,
              (unsigned long)g_stream.dma_lisr);
}

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
    if (g_stream.active)
    {
        return;
    }
    g_ov5640_dcmi_sta.frame_lines = g_ov5640_dcmi_sta.lines;
    g_ov5640_dcmi_sta.frame_sem = 1;
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
    (void)hdcmi;
    g_stream.error = 1U;
}

uint8_t ov5640_dcmi_stream_start(uint32_t display_addr, uint32_t capture_addr, uint32_t frame_size)
{
    DMA_HandleTypeDef *dma = hdcmi.DMA_Handle;
    if (frame_size != CAMERA_FRAME_BYTES)
    {
        return OV5640_DCMI_EINVAL;
    }
    g_stream.addr[0] = display_addr;
    g_stream.addr[1] = capture_addr;
    g_stream.addr[2] = capture_addr + frame_size;
    g_stream.display = 0U;
    g_stream.capture = 1U;
    g_stream.next = 1U;
    g_stream.pending = 3U;
    g_stream.segment = 0U;
    g_stream.reload = 0U;
    g_stream.error = 0U;
    g_stream.frames = 0U;
    g_stream.reported = 0U;
    g_stream.frame_tick = HAL_GetTick();

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)display_addr, frame_size);
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)capture_addr, 2U * frame_size);
    dma->XferCpltCallback = camera_dma_complete;
    dma->XferM1CpltCallback = camera_dma_complete;
    dma->XferErrorCallback = camera_dma_error;
    dma->XferHalfCpltCallback = NULL;
    dma->XferM1HalfCpltCallback = NULL;
    if (HAL_DMAEx_MultiBufferStart_IT(dma, (uint32_t)&DCMI->DR,
                                      g_stream.addr[1], g_stream.addr[1] + CAMERA_DMA_WORDS * 4U,
                                      CAMERA_DMA_WORDS) != HAL_OK)
    {
        return OV5640_DCMI_ERROR;
    }
    g_stream.active = 1U;
    hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_ERRRI | DCMI_FLAG_OVRRI);
    __HAL_DCMI_ENABLE(&hdcmi);
    MODIFY_REG(hdcmi.Instance->CR, DCMI_CR_CM, DCMI_MODE_CONTINUOUS);
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_ERR | DCMI_IT_OVR);
    SET_BIT(hdcmi.Instance->CR, DCMI_CR_CAPTURE);
    return OV5640_DCMI_EOK;
}

uint8_t ov5640_dcmi_stream_poll(void)
{
    uint8_t pending;
    uint32_t primask;
    if (g_stream.error || HAL_GetTick() - g_stream.frame_tick > 1000U ||
        hdcmi.ErrorCode != HAL_DCMI_ERROR_NONE ||
        (hdcmi.DMA_Handle->ErrorCode & ~HAL_DMA_ERROR_FE) != HAL_DMA_ERROR_NONE)
    {
        return OV5640_DCMI_ERROR;
    }
    if (g_stream.reload)
    {
        if ((hltdc.Instance->SRCR & LTDC_SRCR_VBR) == 0U)
        {
            primask = __get_PRIMASK();
            __disable_irq();
            g_stream.display = g_stream.pending;
            g_stream.pending = 3U;
            g_stream.reload = 0U;
            __set_PRIMASK(primask);
            if (!g_stream.reported)
            {
                LOG_INFO("CM7", "CAMERA_STREAM_FRAME_PASS frames=%lu display=0x%08lX",
                         (unsigned long)g_stream.frames, (unsigned long)g_stream.addr[g_stream.display]);
                g_stream.reported = 1U;
            }
        }
        else if (HAL_GetTick() - g_stream.reload_tick > 100U)
        {
            return OV5640_DCMI_ETIMEOUT;
        }
    }
    if (!g_stream.reload && g_stream.pending != 3U)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        pending = g_stream.pending;
        if (pending != 3U)
        {
            SCB_InvalidateDCache_by_Addr((uint32_t *)g_stream.addr[pending], CAMERA_FRAME_BYTES);
        }
        if (pending != 3U &&
            HAL_LTDC_SetAddress_NoReload(&hltdc, g_stream.addr[pending], 0U) == HAL_OK &&
            HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) == HAL_OK)
        {
            g_stream.reload = 1U;
            g_stream.reload_tick = HAL_GetTick();
        }
        else if (pending != 3U)
        {
            g_stream.error = 1U;
        }
        __set_PRIMASK(primask);
    }
    return g_stream.error ? OV5640_DCMI_ERROR : OV5640_DCMI_EOK;
}

void ov5640_dcmi_stream_stop(void)
{
    g_stream.active = 0U;
    HAL_DCMI_Stop(&hdcmi);
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
