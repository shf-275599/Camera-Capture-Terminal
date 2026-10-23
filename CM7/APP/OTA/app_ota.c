/**
 * @file    app_ota.c
 * @brief   空闲槽擦写 + CRC + EEPROM pending（CM7 Bank1 / CM4 Bank2）
 *
 * CM4 路径额外依赖 cm4_ota_hold：擦写前停核，abort 时唤醒。
 */
#include "app_ota.h"
#include "boot_eeprom.h"
#include "bsp_flash_ram.h"
#include "cmsis_os2.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "cm4_ota_hold.h"
#include "boot_eeprom.h"
#include "boot_slot.h"
#include "app_ov5640.h"
#include "app_http_mjpeg.h"
#include "mylog.h"

#define APP_OTA_PROG_CHUNK (8192U)           // 编程缓冲粒度
#define APP_OTA_CM4_HOLD_MS (3000U)          // 等待 CM4 ACK 超时
#define APP_OTA_PROG_LOG_STEP (128U * 1024U) // 进度日志步长

typedef enum
{
  APP_OTA_IDLE = 0, // 空闲
  APP_OTA_PREPARING,
  APP_OTA_RECV, // 已 begin，正在收/写镜像
  APP_OTA_PENDING_REBOOT,
} app_ota_state_t;

static app_ota_state_t s_state; // 会话状态
static app_ota_core_t s_core;   // 当前会话目标核
static uint8_t s_target_slot;   // 空闲槽索引 0=A / 1=B
static uint32_t s_target_base;  // 目标槽 Flash 基址
static uint32_t s_image_size;   // begin 约定总长度
static uint32_t s_version;      // X-Fw-Version 编码值
static uint32_t s_offset;       // 已刷入 Flash 的字节（不含 buf）
static uint32_t s_crc;          // 累计 CRC32（未取反）
static uint32_t s_expected_crc;
static uint8_t s_prog_buf[APP_OTA_PROG_CHUNK];
static uint32_t s_prog_fill;     // prog_buf 已填字节
static uint32_t s_prog_log_next; // 下一次进度日志阈值
static uint8_t s_cm4_held;       // 1=已成功 hold CM4，abort 须释放

/**
 * @brief  获取会话内已接收/缓冲的总字节（offset + fill）
 * @return 会话内已接收/缓冲的总字节（offset + fill）
 * @note  内部使用，不建议外部调用。
 */
static uint32_t ota_written_bytes_internal(void)
{
  return s_offset + s_prog_fill;
}

/**
 * @brief  按步长打印写 Flash 进度
 * @param  force  非 0 强制打印一次
 */
static void ota_log_write_progress(int force)
{
  uint32_t done = ota_written_bytes_internal();

  if ((force == 0) && (done < s_prog_log_next) && (done < s_image_size))
  {
    return;
  }

  LOG_INFO("OTA", " write progress %lu / %lu (%lu%%) @ 0x%08lX",
           (unsigned long)done,
           (unsigned long)s_image_size,
           (unsigned long)((s_image_size != 0U) ? ((done * 100UL) / s_image_size) : 0UL),
           (unsigned long)(s_target_base + s_offset));

  while (s_prog_log_next <= done)
  {
    s_prog_log_next += APP_OTA_PROG_LOG_STEP;
  }
}

/**
 * @brief  更新 CRC32 值
 * @param  crc  当前 CRC32 值（未取反）
 * @param  data  数据指针
 * @param  len  数据长度
 * @return 更新后的 CRC32 值（未取反）
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t b;

  for (i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (b = 0U; b < 8U; b++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

/**
 * @brief  生成 IEEE CRC32 终值（按位取反）
 * @param  crc  当前 CRC32 值（未取反）
 * @return  终值后的 CRC32 值（未取反）
 */
static uint32_t crc32_finish(uint32_t crc)
{
  return ~crc;
}

/** 擦写后使对应 Flash 区间 D-Cache 失效，避免读到旧数据 */
static void cache_invalidate_range(uint32_t addr, uint32_t len)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (len == 0U)
  {
    return;
  }
  SCB_InvalidateDCache_by_Addr((void *)addr, (int32_t)len);
#else
  (void)addr;
  (void)len;
#endif
}

/**
 * @brief  擦除 CM7 Bank1 空闲槽（A=S1~S3，B=S5~S7；不擦 Boot/Reserve）
 * @param  slot  空闲槽索引 0=A / 1=B
 * @retval 0 成功；-1 失败
 * @note   严禁擦 S0（Stub @ 0x08100000）；调用前须已 hold CM4
 */
static int erase_inactive_slot(uint8_t slot)
{
  if (slot == CM7_APP_SLOT_A_INDEX)
  {
    LOG_INFO("OTA", " erase CM7 slot A: S1~S3");
    if (bsp_flash_ram_erase_sectors(FLASH_SECTOR_1, FLASH_SECTOR_3) != 0)
    {
      LOG_ERROR("OTA", " erase S1~S3 FAIL");
      return -1;
    }
    cache_invalidate_range(CM7_APP_SLOT_A_BASE, CM7_APP_SLOT_A_SIZE);
    LOG_INFO("OTA", " erase CM7 slot A done");
    return 0;
  }

  if (slot == CM7_APP_SLOT_B_INDEX)
  {
    LOG_INFO("OTA", " erase CM7 slot B: S5~S7");
    if (bsp_flash_ram_erase_sectors(FLASH_SECTOR_5, FLASH_SECTOR_7) != 0)
    {
      LOG_ERROR("OTA", " erase S5~S7 FAIL");
      return -1;
    }
    cache_invalidate_range(CM7_APP_SLOT_B_BASE, CM7_APP_SLOT_B_SIZE);
    LOG_INFO("OTA", " erase CM7 slot B done");
    return 0;
  }

  LOG_ERROR("OTA", " erase CM7: invalid slot %u", (unsigned)slot);
  return -1;
}

/**
 * @brief  擦除 CM4 Bank2 空闲槽（A=S1~S3，B=S5~S7）
 * @param  slot  空闲槽索引 0=A / 1=B
 * @retval 0 成功；-1 失败
 * @note   严禁擦 S0（Stub @ 0x08100000）；调用前须已 hold CM4
 */
static int erase_cm4_inactive_slot(uint8_t slot)
{
  if (slot == CM4_APP_SLOT_A_INDEX)
  {
    LOG_INFO("OTA", " erase CM4 slot A: Bank2 S1~S3");
    if (bsp_flash_ram_erase_bank2_sectors(FLASH_SECTOR_1, FLASH_SECTOR_3) != 0)
    {
      LOG_ERROR("OTA", " erase Bank2 S1~S3 FAIL");
      return -1;
    }
    cache_invalidate_range(CM4_APP_SLOT_A_BASE, CM4_APP_SLOT_A_SIZE);
    LOG_INFO("OTA", " erase CM4 slot A done");
    return 0;
  }

  if (slot == CM4_APP_SLOT_B_INDEX)
  {
    LOG_INFO("OTA", " erase CM4 slot B: Bank2 S5~S7");
    if (bsp_flash_ram_erase_bank2_sectors(FLASH_SECTOR_5, FLASH_SECTOR_7) != 0)
    {
      LOG_ERROR("OTA", " erase Bank2 S5~S7 FAIL");
      return -1;
    }
    cache_invalidate_range(CM4_APP_SLOT_B_BASE, CM4_APP_SLOT_B_SIZE);
    LOG_INFO("OTA", " erase CM4 slot B done");
    return 0;
  }

  LOG_ERROR("OTA", " erase CM4: invalid slot %u", (unsigned)slot);
  return -1;
}

/**
 * @brief  将 prog_buf 刷入 s_target_base+s_offset（Bank1/2 由地址自动选择）
 * @retval 0 成功；-1 编程失败
 * @note   编程失败时，s_offset 未更新
 */
static int flush_prog_buf(void)
{
  uint32_t addr;

  if (s_prog_fill == 0U)
  {
    return 0;
  }

  addr = s_target_base + s_offset;
  if (bsp_flash_ram_program_buffer(addr, s_prog_buf, s_prog_fill) != 0)
  {
    LOG_ERROR("OTA", " FLASH program FAIL @ 0x%08lX len=%lu written=%lu/%lu",
              (unsigned long)addr,
              (unsigned long)s_prog_fill,
              (unsigned long)s_offset,
              (unsigned long)s_image_size);
    return -1;
  }
  cache_invalidate_range(addr, s_prog_fill);
  s_offset += s_prog_fill;
  s_prog_fill = 0U;
  ota_log_write_progress(0);
  return 0;
}

/**
 * @brief  停外部设备 DMA/预览并挂起其它业务线程，再允许擦写 Flash
 * @note   保持中断开启：Eth/tcpip/SysTick 需继续工作；勿长关 __disable_irq
 */
static int ota_quiesce_system(void)
{
  if (app_http_mjpeg_stop(5000U) != 0)
  {
    app_http_mjpeg_resume();
    return -1;
  }
  if (app_ov5640_pause(3000U) != 0)
  {
    app_http_mjpeg_resume();
    return -1;
  }
  return 0;
}

/**
 * @brief  OTA 结束后恢复协作任务与 LCD 预览
 */
static void ota_restore_system(void)
{
  app_ov5640_resume();
  app_http_mjpeg_resume();
}

/**
 * @brief  是否正在接收/编程（非 IDLE）
 * @retval 1 繁忙；0 未繁忙
 */
int app_ota_is_busy(void)
{
  return (s_state != APP_OTA_IDLE) ? 1 : 0;
}

/**
 * @brief  获取begin 时约定的镜像长度；非接收态返回 0
 * @retval  begin 时约定的镜像长度；非接收态返回 0
 */
uint32_t app_ota_image_size(void)
{
  return (s_state != APP_OTA_IDLE) ? s_image_size : 0U;
}

/**
 * @brief  获取已写入空闲槽的字节数；非接收态返回 0
 * @retval  已写入空闲槽的字节数；非接收态返回 0
 */
uint32_t app_ota_written_bytes(void)
{
  return (s_state != APP_OTA_IDLE) ? ota_written_bytes_internal() : 0U;
}

/**
 * @brief  获取当前会话版本（begin 时写入）
 * @retval  当前会话版本（begin 时写入）；非接收态返回 0
 */
uint32_t app_ota_session_version(void)
{
  return (s_state != APP_OTA_IDLE) ? s_version : 0U;
}

int app_ota_session_matches(uint32_t image_size, uint32_t version, app_ota_core_t core,
                            uint32_t expected_crc32, uint32_t linked_base)
{
  return (s_state == APP_OTA_RECV) && (s_core == core) &&
         (s_image_size == image_size) && (s_version == version) &&
         (s_expected_crc == expected_crc32) && (s_target_base == linked_base);
}

/**
 * @brief  中止本次 OTA：若持有 CM4 hold 则释放，并恢复外部设备与协作任务
 * @note   已擦写的 Flash 不回滚；pending 不会被置位
 */
void app_ota_abort(void)
{
  /* pending 已持久化，保持资源直到调用方复位，不能伪装成已取消。 */
  if (s_state == APP_OTA_PENDING_REBOOT)
  {
    return;
  }
  if (s_state != APP_OTA_IDLE)
  {
    LOG_ERROR("OTA", " abort: core=%s written=%lu/%lu target=%u",
              (s_core == APP_OTA_CORE_CM4) ? "CM4" : "CM7",
              (unsigned long)ota_written_bytes_internal(),
              (unsigned long)s_image_size,
              (unsigned)s_target_slot);
  }
  if (s_cm4_held != 0U)
  {
    cm4_ota_hold_release();
    s_cm4_held = 0U;
    LOG_INFO("OTA", " CM4 hold released");
  }
  s_state = APP_OTA_IDLE;
  s_core = APP_OTA_CORE_CM7;
  s_prog_fill = 0U;
  s_offset = 0U;
  s_image_size = 0U;
  s_version = 0U;
  s_expected_crc = 0U;
  s_prog_log_next = 0U;
  ota_restore_system();
}

/**
 * @brief  获取当前 OTA 会话核心
 * @retval  当前会话目标核；非接收态返回 APP_OTA_CORE_CM7（无意义，仅占位）
 */
app_ota_core_t app_ota_session_core(void)
{
  return (s_state != APP_OTA_IDLE) ? s_core : APP_OTA_CORE_CM7;
}

/**
 * @brief  填充指定核的纯文本状态（供 GET /ota/status）
 * @param  core  APP_OTA_CORE_CM7 / APP_OTA_CORE_CM4
 * @retval 写入字节数（不含 '\\0'），失败返回 -1
 */
int app_ota_format_status(char *buf, size_t buflen, app_ota_core_t core)
{
  boot_eeprom_meta_t ee;
  uint8_t running;
  uint8_t inactive;
  int session_match;
  unsigned busy_for_core;
  uint32_t written;
  uint32_t image_size;
  uint32_t session_ver;
  int n;

  if ((buf == NULL) || (buflen < 64U))
  {
    return -1;
  }

  memset(&ee, 0, sizeof(ee));
  (void)boot_eeprom_load(&ee);

  session_match = ((s_state != APP_OTA_IDLE) && (s_core == core)) ? 1 : 0;
  busy_for_core = (session_match != 0) ? 1U : 0U;
  written = (session_match != 0) ? ota_written_bytes_internal() : 0U;
  image_size = (session_match != 0) ? s_image_size : 0U;
  session_ver = (session_match != 0) ? s_version : 0U;

  if (core == APP_OTA_CORE_CM4)
  {
    /* 运行槽由 CM4 启动后的任务报告，不能用 confirmed 代替。 */
    running = cm4_ota_running_slot();
    if (running >= CM4_APP_SLOT_COUNT)
    {
      return -1;
    }
    inactive = cm4_boot_slot_inactive_index(running);

    n = snprintf(buf, buflen,
                 "core=cm4\r\n"
                 "busy=%u\r\n"
                 "written=%lu\r\n"
                 "image_size=%lu\r\n"
                 "session_ver=0x%08lX\r\n"
                 "session_core=%s\r\n"
                 "other_core_busy=%u\r\n"
                 "running=%u\r\n"
                 "inactive=%u\r\n"
                 "active=%u\r\n"
                 "pending=%u\r\n"
                 "flags=0x%02X\r\n"
                 "trial=%u\r\n"
                 "target_slot=%u\r\n"
                 "target_base=0x%08lX\r\n"
                 "stub_base=0x%08lX\r\n"
                 "max_image=%lu\r\n"
                 "version_a=0x%08lX\r\n"
                 "version_b=0x%08lX\r\n"
                 "size_a=%lu\r\n"
                 "size_b=%lu\r\n"
                 "hint=CM4: Stub@0x%08lX + upload idle slot %c @ 0x%08lX (Bank2); "
                  "CM7 holds CM4 then programs; repeat matching begin to resume\r\n",
                 busy_for_core,
                 (unsigned long)written,
                 (unsigned long)image_size,
                 (unsigned long)session_ver,
                 (s_state != APP_OTA_IDLE)
                     ? ((s_core == APP_OTA_CORE_CM4) ? "cm4" : "cm7")
                     : "none",
                 (unsigned)((app_ota_is_busy() != 0) && (session_match == 0)),
                 (unsigned)running,
                 (unsigned)inactive,
                 (unsigned)ee.cm4_confirmed_slot,
                 (unsigned)ee.cm4_pending_slot,
                 (unsigned)ee.cm4_flags,
                 (unsigned)ee.cm4_boot_trial,
                 (unsigned)inactive,
                 (unsigned long)cm4_boot_slot_base_get(inactive),
                 (unsigned long)CM4_BOOT_STUB_BASE,
                 (unsigned long)APP_OTA_MAX_IMAGE_SIZE_CM4,
                 (unsigned long)ee.cm4_app_version[0],
                 (unsigned long)ee.cm4_app_version[1],
                 (unsigned long)ee.cm4_app_size[0],
                 (unsigned long)ee.cm4_app_size[1],
                 (unsigned long)CM4_BOOT_STUB_BASE,
                 (inactive == CM4_APP_SLOT_A_INDEX) ? 'A' : 'B',
                 (unsigned long)cm4_boot_slot_base_get(inactive));
  }
  else
  {
    running = boot_get_running_slot();
    inactive = boot_slot_inactive_index(running);

    n = snprintf(buf, buflen,
                 "core=cm7\r\n"
                 "busy=%u\r\n"
                 "written=%lu\r\n"
                 "image_size=%lu\r\n"
                 "session_ver=0x%08lX\r\n"
                 "session_core=%s\r\n"
                 "other_core_busy=%u\r\n"
                 "running=%u\r\n"
                 "inactive=%u\r\n"
                 "active=%u\r\n"
                 "pending=%u\r\n"
                 "flags=0x%02X\r\n"
                 "trial=%u\r\n"
                 "target_slot=%u\r\n"
                 "target_base=0x%08lX\r\n"
                 "max_image=%lu\r\n"
                 "version_a=0x%08lX\r\n"
                 "version_b=0x%08lX\r\n"
                 "size_a=%lu\r\n"
                 "size_b=%lu\r\n"
                 "hint=CM7: upload bin linked for slot %c (base 0x%08lX); "
                  "repeat begin with matching core/size/version/crc32/base to resume\r\n",
                 busy_for_core,
                 (unsigned long)written,
                 (unsigned long)image_size,
                 (unsigned long)session_ver,
                 (s_state != APP_OTA_IDLE)
                     ? ((s_core == APP_OTA_CORE_CM4) ? "cm4" : "cm7")
                     : "none",
                 (unsigned)((app_ota_is_busy() != 0) && (session_match == 0)),
                 (unsigned)running,
                 (unsigned)inactive,
                 (unsigned)ee.confirmed_slot,
                 (unsigned)ee.pending_slot,
                 (unsigned)ee.flags,
                 (unsigned)ee.boot_trial,
                 (unsigned)inactive,
                 (unsigned long)boot_slot_base_get(inactive),
                 (unsigned long)APP_OTA_MAX_IMAGE_SIZE,
                 (unsigned long)ee.app_version[0],
                 (unsigned long)ee.app_version[1],
                 (unsigned long)ee.app_size[0],
                 (unsigned long)ee.app_size[1],
                 (inactive == CM7_APP_SLOT_A_INDEX) ? 'A' : 'B',
                 (unsigned long)boot_slot_base_get(inactive));
  }

  if (n < 0)
  {
    return -1;
  }
  if ((size_t)n >= buflen)
  {
    buf[buflen - 1U] = '\0';
    return (int)(buflen - 1U);
  }
  return n;
}

/**
 * @brief  擦除空闲槽并准备接收镜像
 * @param  image_size  Content-Length（raw .bin）
 * @param  version     来自 X-Fw-Version（解析为整数；失败则为 0）
 * @param  core        目标核（CM7 Bank1 / CM4 Bank2）
 * @param  expected_crc32 发布文件的完整 CRC32
 * @param  linked_base 发布文件链接地址，必须等于空闲槽基址
 * @retval 0 成功；非 0 失败（忙/尺寸非法/擦除失败等）；-11=CM4 hold 超时
 * @note   会话存在时拒绝 begin；调用方必须串行管理会话，勿仅凭 size/version 续传
 */
int app_ota_begin(uint32_t image_size, uint32_t version, app_ota_core_t core,
                  uint32_t expected_crc32, uint32_t linked_base)
{
  boot_eeprom_meta_t ee;
  uint8_t running;
  uint32_t max_size;

  /* 检查是否正在忙 */
  if (s_state != APP_OTA_IDLE)
  {
    LOG_ERROR("OTA", " begin rejected: busy (state=%d)", (int)s_state);
    return -1;
  }
  /* 检查核心是否有效 */
  if ((core != APP_OTA_CORE_CM7) && (core != APP_OTA_CORE_CM4))
  {
    return -2;
  }
  /* 检查 EEPROM 状态 */
  if ((boot_eeprom_load(&ee) != 0) ||
      ((ee.flags & BOOT_EE_FLAG_PENDING) != 0U) ||
      ((ee.cm4_flags & BOOT_EE_FLAG_CM4_PENDING) != 0U))
  {
    return -6;
  }
  /* 检查运行槽是否有效 */
  running = (core == APP_OTA_CORE_CM4) ? cm4_ota_running_slot() : boot_get_running_slot();
  /* 检查运行槽是否与确认槽匹配 */
  if ((running >= 2U) ||
      (running != ((core == APP_OTA_CORE_CM4) ? ee.cm4_confirmed_slot : ee.confirmed_slot)))
  {
    return -8;
  }
  /* 检查目标槽是否与链接基址匹配 */
  s_target_slot = (core == APP_OTA_CORE_CM4) ? cm4_boot_slot_inactive_index(running)
                                             : boot_slot_inactive_index(running);
  s_target_base = (core == APP_OTA_CORE_CM4) ? cm4_boot_slot_base_get(s_target_slot)
                                             : boot_slot_base_get(s_target_slot);
  /* 检查链接基址是否与目标槽基址匹配 */
  if (linked_base != s_target_base)
  {
    return -8;
  }
  /* 检查镜像尺寸是否有效 */
  max_size = (core == APP_OTA_CORE_CM4) ? APP_OTA_MAX_IMAGE_SIZE_CM4
                                        : APP_OTA_MAX_IMAGE_SIZE;
  /* 检查镜像尺寸是否在有效范围内 */
  if ((image_size < 8U) || (image_size > max_size))
  {
    LOG_ERROR("OTA", " begin rejected: core=%s size=%lu max=%lu",
              (core == APP_OTA_CORE_CM4) ? "CM4" : "CM7",
              (unsigned long)image_size,
              (unsigned long)max_size);
    return -2;
  }

  s_image_size = image_size;               // 记录镜像尺寸
  s_version = version;                     // 记录版本号
  s_offset = 0U;                           // 记录偏移量
  s_prog_fill = 0U;                        // 记录进度填充
  s_prog_log_next = APP_OTA_PROG_LOG_STEP; // 记录进度日志下一个位置
  s_crc = 0xFFFFFFFFUL;                    // 记录 CRC32 值
  s_cm4_held = 0U;                         // 记录 CM4 是否被 hold
  s_expected_crc = expected_crc32;         // 记录预期 CRC32 值
  s_core = core;                           // 记录核心
  s_state = APP_OTA_PREPARING;             // 记录状态为准备中
  /* 尝试静默系统 */
  if (ota_quiesce_system() != 0)
  {
    app_ota_abort();
    return -9;
  }
  /* 检查核心是否为 CM4 */
  if (core == APP_OTA_CORE_CM4)
  {
    /* —— CM4：hold → 擦 Bank2 空闲槽 —— */
    LOG_INFO("OTA", " begin CM4 size=%lu ver=0x%08lX active=%u(%c) -> target=%u(%c) base=0x%08lX",
             (unsigned long)image_size,
             (unsigned long)version,
             (unsigned)running,
             (running == CM4_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned)s_target_slot,
             (s_target_slot == CM4_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned long)s_target_base);

    LOG_INFO("OTA", " quiesce system (stop cam / suspend peers)");

    LOG_INFO("OTA", " hold CM4 before Bank2 erase");
    s_cm4_held = 1U; // 请求发出后，无论 ACK 是否到达都必须清理
    /* 尝试 hold CM4 */
    if (cm4_ota_hold_request(APP_OTA_CM4_HOLD_MS) != 0)
    {
      LOG_ERROR("OTA", " CM4 hold timeout");
      app_ota_abort();
      return -11;
    }
    LOG_INFO("OTA", " CM4 held (ACK ok)");
    /* 尝试擦除 CM4 空闲槽 */
    if (erase_cm4_inactive_slot(s_target_slot) != 0)
    {
      app_ota_abort();
      LOG_ERROR("OTA", " CM4 erase fail, abort");
      return -3;
    }
  }
  else
  {
    /* —— CM7：擦 Bank1 空闲槽 —— */
    LOG_INFO("OTA", " begin CM7 size=%lu ver=0x%08lX running=%u(%c) -> target=%u(%c) base=0x%08lX",
             (unsigned long)image_size,
             (unsigned long)version,
             (unsigned)running,
             (running == CM7_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned)s_target_slot,
             (s_target_slot == CM7_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned long)s_target_base);

    LOG_INFO("OTA", " quiesce system (stop cam / suspend peers)");
    /* 尝试擦除 CM7 空闲槽 */
    if (erase_inactive_slot(s_target_slot) != 0)
    {
      app_ota_abort();
      LOG_ERROR("OTA", " erase fail, abort");
      return -3;
    }
  }

  s_state = APP_OTA_RECV; // 记录状态为接收
  LOG_INFO("OTA", " erase OK, waiting for body");
  return 0;
}

/**
 * @brief  流式写入镜像数据
 * @note   累计长度不得超过 begin 时的 image_size
 * @retval 0 成功；非 0 失败并会 abort
 */
int app_ota_write(const uint8_t *data, uint32_t len)
{
  const uint8_t *p = data;

  if (s_state != APP_OTA_RECV)
  {
    LOG_ERROR("OTA", " write rejected: not in RECV (state=%d)", (int)s_state);
    return -1;
  }
  if ((data == NULL) && (len > 0U))
  {
    LOG_ERROR("OTA", " write rejected: null data len=%lu", (unsigned long)len);
    return -1;
  }
  uint32_t received = ota_written_bytes_internal();
  if ((received > s_image_size) || (len > s_image_size - received))
  {
    LOG_ERROR("OTA", " write overflow: got+=%lu would=%lu limit=%lu",
              (unsigned long)len,
              (unsigned long)(s_offset + s_prog_fill + len),
              (unsigned long)s_image_size);
    return -2;
  }

  while (len > 0U)
  {
    uint32_t space = APP_OTA_PROG_CHUNK - s_prog_fill;
    uint32_t take = (len < space) ? len : space;

    memcpy(&s_prog_buf[s_prog_fill], p, take);
    s_prog_fill += take;
    p += take;
    len -= take;

    if (s_prog_fill >= APP_OTA_PROG_CHUNK)
    {
      s_crc = crc32_update(s_crc, s_prog_buf, s_prog_fill);
      if (flush_prog_buf() != 0)
      {
        app_ota_abort();
        return -3;
      }
    }
  }

  return 0;
}

/**
 * @brief  校验槽向量表并写入 EEPROM pending
 * @retval 0 成功；非 0 失败（会 abort）
 * @note   CM7 → boot_eeprom_set_pending；CM4 → boot_eeprom_set_cm4_pending
 */
int app_ota_finish(void)
{
  boot_eeprom_meta_t ee;
  uint32_t image_crc;
  /* 校验状态 */
  if (s_state != APP_OTA_RECV)
  {
    LOG_ERROR("OTA", " finish rejected: not in RECV (state=%d)", (int)s_state);
    return -1;
  }
  /* 校验写入大小 */
  if ((s_offset + s_prog_fill) != s_image_size)
  {
    LOG_ERROR("OTA", " finish size mismatch: written=%lu expect=%lu",
              (unsigned long)(s_offset + s_prog_fill),
              (unsigned long)s_image_size);
    app_ota_abort();
    return -2;
  }
  /* 校验 CRC32 */
  if (s_prog_fill > 0U)
  {
    s_crc = crc32_update(s_crc, s_prog_buf, s_prog_fill); // 更新 CRC32 校验和
    if (flush_prog_buf() != 0)
    {
      app_ota_abort();
      return -3;
    }
  }
  ota_log_write_progress(1);       // 按步长打印写 Flash 进度
  image_crc = crc32_finish(s_crc); // 计算 CRC32 校验和
  /* 校验 CRC32 */
  if (image_crc != s_expected_crc)
  {
    LOG_ERROR("OTA", "CRC mismatch received=0x%08lX expected=0x%08lX",
              (unsigned long)image_crc, (unsigned long)s_expected_crc);
    app_ota_abort();
    return -7;
  }
  /* 校验 CM4 向量表 */
  if (s_core == APP_OTA_CORE_CM4)
  {
    /* 校验 CM4 向量表后写 cm4_pending；复位后 prepare_cm4_boot 试跑新槽 */
    if (cm4_boot_slot_is_valid(s_target_slot) == 0)
    {
      uint32_t sp = *(__IO uint32_t *)s_target_base;
      uint32_t pc = *(__IO uint32_t *)(s_target_base + 4U);
      app_ota_abort();
      LOG_ERROR("OTA", " CM4 slot invalid after write: SP=0x%08lX Reset=0x%08lX",
                (unsigned long)sp, (unsigned long)pc);
      return -4;
    }

    memset(&ee, 0, sizeof(ee));
    if (boot_eeprom_load(&ee) != 0)
    {
      app_ota_abort();
      return -6;
    }
    if (boot_eeprom_set_cm4_pending(&ee, s_target_slot, s_image_size, s_expected_crc, s_version) != 0)
    {
      app_ota_abort();
      return -5;
    }
    LOG_INFO("OTA", " EEPROM CM4 pending set: slot=%u trial pending",
             (unsigned)s_target_slot);

    LOG_INFO("OTA", " finish ok CM4 slot=%u(%c) size=%lu crc=0x%08lX ver=0x%08lX -> reboot",
             (unsigned)s_target_slot,
             (s_target_slot == CM4_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned long)s_image_size,
             (unsigned long)image_crc,
             (unsigned long)s_version);
  }
  /* 校验 CM7 向量表 */
  else
  {
    /* 校验 CM7 向量表后写 CM7 pending */
    if (boot_slot_is_valid(s_target_slot) == 0)
    {
      uint32_t sp = *(__IO uint32_t *)s_target_base;
      uint32_t pc = *(__IO uint32_t *)(s_target_base + 4U);
      app_ota_abort();
      LOG_ERROR("OTA", " CM7 slot invalid after write: SP=0x%08lX Reset=0x%08lX",
                (unsigned long)sp, (unsigned long)pc);
      return -4;
    }

    memset(&ee, 0, sizeof(ee));
    if (boot_eeprom_load(&ee) != 0)
    {
      app_ota_abort();
      return -6;
    }
    if (boot_eeprom_set_pending(&ee, s_target_slot, s_image_size, s_expected_crc, s_version) != 0)
    {
      app_ota_abort();
      return -5;
    }
    LOG_INFO("OTA", " EEPROM pending set: slot=%u trial pending",
             (unsigned)s_target_slot);

    LOG_INFO("OTA", " finish ok CM7 slot=%u(%c) size=%lu crc=0x%08lX ver=0x%08lX -> reboot pending",
             (unsigned)s_target_slot,
             (s_target_slot == CM7_APP_SLOT_A_INDEX) ? 'A' : 'B',
             (unsigned long)s_image_size,
             (unsigned long)image_crc,
             (unsigned long)s_version);
  }

  s_state = APP_OTA_PENDING_REBOOT; // 切换到待复位状态
  /* 即将复位：CM4 hold / peer 挂起可保持 */
  return 0;
}
