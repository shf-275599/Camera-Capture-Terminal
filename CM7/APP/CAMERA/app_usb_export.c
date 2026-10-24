/* IRQ 仅唤醒；PCD/BOT、同步读盘与断开在同一个任务串行执行。 */
#include "app_usb_export.h"
#include "app_sd.h"
#include "usb_device.h"
#include "usbd_desc.h"
#include "usbd_msc.h"
#include "usbd_storage_if.h"
#include "sdmmc.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mylog.h"
#include <string.h>
extern USBD_HandleTypeDef hUsbDeviceFS;
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
static StaticTask_t s_control;
static StackType_t s_stack[1536];
static osThreadId_t s_thread;
static volatile int s_wanted, s_active, s_result;
static volatile uint32_t s_request, s_done;

void app_usb_export_irq(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    if (s_thread != NULL) (void)osThreadFlagsSet(s_thread, 1U);
    __DSB(); __ISB();
}

int app_usb_export_ready(void)
{
    return s_active && s_wanted && app_sd_status(0U) == 0U;
}

int app_usb_export_read(uint8_t *buffer, uint32_t sector, uint16_t count)
{
    uint32_t capacity = hsd2.SdCard.LogBlockNbr;
    if (__get_IPSR() != 0U || !app_usb_export_ready() || count == 0U ||
        count > MSC_MEDIA_PACKET / 512U || sector >= capacity || count > capacity - sector)
        return -1;
    if (app_sd_read(0U, buffer, sector, count) != RES_OK) return -1;
    if (!s_wanted || (USB_OTG_FS->GINTSTS & USB_OTG_GINTSTS_USBRST) != 0U) return -1;
    return 0;
}

static int start_device(void)
{
    memset(&hUsbDeviceFS, 0, sizeof(hUsbDeviceFS));
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK) return -1;
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_MSC) != USBD_OK ||
        USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_Storage_Interface_fops_FS) != USBD_OK ||
        USBD_Start(&hUsbDeviceFS) != USBD_OK)
    {
        (void)USBD_DeInit(&hUsbDeviceFS);
        return -1;
    }
    return 0;
}

static void usb_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        (void)osThreadFlagsWait(1U, osFlagsWaitAny, 10U);
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
        if (s_done != s_request)
        {
            uint32_t request = s_request;
            if (s_wanted)
            {
                s_result = start_device();
                s_active = s_result == 0;
            }
            else
            {
                s_result = 0;
                if (hpcd_USB_OTG_FS.Instance != NULL)
                {
                    if (HAL_PCD_Stop(&hpcd_USB_OTG_FS) != HAL_OK ||
                        USBD_DeInit(&hUsbDeviceFS) != USBD_OK) s_result = -1;
                }
                if (s_result == 0)
                {
                    s_active = 0;
                    hpcd_USB_OTG_FS.Instance = NULL;
                    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
                }
            }
            __DMB();
            s_done = request;
        }
        if (s_active && s_wanted)
        {
            HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
            HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
        }
    }
}

void app_usb_export_start_task(void)
{
    const osThreadAttr_t attr = { .name = "usb_msc", .priority = osPriorityAboveNormal,
        .stack_mem = s_stack, .stack_size = sizeof(s_stack),
        .cb_mem = &s_control, .cb_size = sizeof(s_control) };
    s_thread = osThreadNew(usb_task, NULL, &attr);
    if (s_thread == NULL) LOG_ERROR("USB", "USB_TASK_FAIL");
}

int app_usb_export_set(int enabled)
{
    if (s_thread == NULL || s_request != s_done) return -1;
    s_wanted = enabled;
    __DMB();
    uint32_t request = ++s_request;
    (void)osThreadFlagsSet(s_thread, 1U);
    uint32_t start = HAL_GetTick();
    while (s_done != request)
    {
        if (HAL_GetTick() - start >= 3000U)
        {
            s_wanted = 0;
            LOG_ERROR("USB", "USB_TRANSITION_TIMEOUT ownership_retained");
            return -1;
        }
        osDelay(1U);
    }
    return s_result;
}
