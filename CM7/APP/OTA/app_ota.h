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

  /**
   * @brief  检查 OTA 会话是否繁忙（写入中）
   * @return int 1：繁忙（写入中）；0：空闲
   */
  int app_ota_is_busy(void);

  /**
   * @brief  尝试获取 OTA 会话控制锁
   * @return int 1：成功获取；0：已占用
   */
  int app_ota_control_try_lock(void);

  /**
   * @brief  释放 OTA 会话控制锁
   */
  void app_ota_control_unlock(void);

  /**
   * @brief  获取当前 OTA 会话的目标核
   * @return app_ota_core_t 目标核
   */
  app_ota_core_t app_ota_session_core(void);

  /**
   * @brief  获取当前 OTA 会话的镜像大小
   * @return uint32_t 镜像大小
   */
  uint32_t app_ota_image_size(void);

  /**
   * @brief  获取当前 OTA 会话已写入的字节数
   * @return uint32_t 已写入字节数
   */
  uint32_t app_ota_written_bytes(void);

  /**
   * @brief  获取当前 OTA 会话的版本号
   * @return uint32_t 版本号
   */
  uint32_t app_ota_session_version(void);

  /**
   * @brief  格式化 OTA 会话状态为字符串
   * @param buf 输出缓冲区
   * @param buflen 缓冲区大小
   * @param core 目标核
   * @return int 0：成功；-1：失败（缓冲区太小）
   */
  int app_ota_format_status(char *buf, size_t buflen, app_ota_core_t core);

  /**
   * @brief  开始 OTA 会话
   * @param image_size 镜像大小
   * @param version 版本号
   * @param core 目标核
   * @param expected_crc32 预期 CRC32
   * @param linked_base 链接基地址（CM7 无）
   * @return int 0：成功；-1：失败（镜像大小超出上限）
   */
  int app_ota_begin(uint32_t image_size, uint32_t version, app_ota_core_t core,
                    uint32_t expected_crc32, uint32_t linked_base);

  /**
   * @brief  为当前 OTA 会话写入数据
   * @param data 数据指针
   * @param len 数据长度
   * @return int 0：成功；-1：失败（数据长度超出上限）
   */
  int app_ota_write(const uint8_t *data, uint32_t len);

  /**
   * @brief  完成 OTA 会话
   * @return int 0：成功；-1：失败（校验失败）
   */
  int app_ota_finish(void);

  /**
   * @brief  取消 OTA 会话
   */
  void app_ota_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OTA_H */
