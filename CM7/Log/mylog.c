#include "mylog.h"

#include <stdarg.h>
#include <stdio.h>

/* 同步输出到 printf 已重定向的 USART1；仅在主循环上下文调用。 */
void log_write(int level, const char *module, const char *format, ...)
{
    const char *color;
    va_list args;

    if (level > LOG_LEVEL || level < LOG_LEVEL_ERROR)
        return;

    switch (level)
    {
    case LOG_LEVEL_ERROR:
        color = "\033[1;31m";
        break;
    case LOG_LEVEL_WARN:
        color = "\033[1;33m";
        break;
    case LOG_LEVEL_DEBUG:
        color = "\033[0;36m";
        break;
    default:
        color = "\033[0;32m";
        break;
    }

    printf("\033[0m%s[%s] ", color, module);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\033[0m\r\n");
}
