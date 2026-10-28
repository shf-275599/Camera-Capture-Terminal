#ifndef BOOT_LOCAL_UPDATE_H
#define BOOT_LOCAL_UPDATE_H

#include <stdint.h>

/* Flash 描述符与 SRAM4 邮箱；与 BaseProject/local_flash_update.ps1 配套。 */
#define BOOT_LOCAL_DESCRIPTOR_MAGIC 0x4C555044U
#define BOOT_LOCAL_PROTOCOL_VERSION 2U
#define BOOT_LOCAL_MAILBOX_ADDR 0x3800F100U
#define BOOT_LOCAL_MAILBOX_SIZE 32U
#define BOOT_LOCAL_CHECK 0x4C555031U
#define BOOT_LOCAL_COMMIT 0x4C555032U
#define BOOT_LOCAL_BUSY 1U
#define BOOT_LOCAL_OK 2U
#define BOOT_LOCAL_FAILED 3U

/* word[0] 命令，word[1..6] 核(7/4)、槽、长度、CRC、版本、保留(0)，word[7] 状态。 */
int boot_eeprom_local_update(const uint32_t values[6], int commit);

#endif
