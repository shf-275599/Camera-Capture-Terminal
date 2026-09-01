#include "bsp_sys.h"
#include "mpu.h"
#include "cachel1_armv7.h"

uint8_t bsp_sys_init(void)
{
    memory_protection_unit_init();
    
    SCB_EnableDCache();
    SCB_EnableICache();

    return 0;
}