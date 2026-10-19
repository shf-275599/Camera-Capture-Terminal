/**
 * @file    app_ota.h
 * @brief   HTTP OTA：写入空闲槽 + EEPROM pending（对齐上位机 CamViewHost）
 *
 * 支持目标核：
 *   - CM7：擦写 Bank1 空闲 APP 槽，finish 置 CM7 pending
 *   - CM4：先 hold CM4，再擦写 Bank2 空闲 APP 槽，finish 置 CM4 pending
 * 槽内不写 footer；size/CRC/version 均在 EEPROM。
 */
#ifndef APP_OTA_H
#define APP_OTA_H

#include "boot_slot.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// CM7 单槽镜像上限（= CM7_APP_SLOT_SIZE）
#define APP_OTA_MAX_IMAGE_SIZE (CM7_APP_SLOT_A_SIZE)
// CM4 单槽镜像上限（= CM4_APP_SLOT_SIZE）
#define APP_OTA_MAX_IMAGE_SIZE_CM4 (CM4_APP_SLOT_A_SIZE)

  /**
   * @brief  OTA 目标核（与上位机头 X-Ota-Core / 查询 ?core= 对齐）
   */
  typedef enum
  {
    APP_OTA_CORE_CM7 = 0, // Bank1 APP A/B
    APP_OTA_CORE_CM4 = 1, // Bank2 APP A/B（Stub 不擦）
  } app_ota_core_t;

  int app_ota_is_busy(void);
  app_ota_core_t app_ota_session_core(void);
  uint32_t app_ota_image_size(void);
  uint32_t app_ota_written_bytes(void);
  uint32_t app_ota_session_version(void);
  int app_ota_format_status(char *buf, size_t buflen, app_ota_core_t core);
  int app_ota_begin(uint32_t image_size, uint32_t version, app_ota_core_t core);
  int app_ota_write(const uint8_t *data, uint32_t len);
  int app_ota_finish(void);
  void app_ota_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OTA_H */
