#include "boot_jump.h"
#include <stdint.h>
#include <stdbool.h>
#include "boot_slot.h"

/**
 * @brief  校验指定基地址处应用程序是否有效
 * @param  app_base 应用程序向量表基地址
 * @retval true 有效，false 无效
 */
bool boot_app_is_valid(uint32_t app_base)
{
    uint8_t slot = boot_get_slot_index_from_address(app_base);

    if (boot_slot_base_get(slot) != app_base)
    {
        return false;
    }

    return true;
}
