#ifndef APP_USB_EXPORT_H
#define APP_USB_EXPORT_H
#include <stdint.h>
void app_usb_export_irq(void);
void app_usb_export_start_task(void);
int app_usb_export_set(int enabled);
int app_usb_export_ready(void);
int app_usb_export_read(uint8_t *buffer, uint32_t sector, uint16_t count);
#endif
