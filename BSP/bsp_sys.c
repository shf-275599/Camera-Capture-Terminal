#include "bsp_sys.h"
#include "cachel1_armv7.h"

uint8_t scb_cache_init(void)
{
    SCB_EnableDCache();
    SCB_EnableICache();
    return 0;
}