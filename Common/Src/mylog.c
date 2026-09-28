#include "mylog.h"
#include <stdarg.h>
#include <stdio.h>

/* 同步输出到各核 printf 已重定向的通道；仅在非中断上下文调用。 */
void log_write(int level, const char *module, const char *format, ...)
{
    const char *color;
    const char *name;
    va_list args;

    if (level > LOG_LEVEL || level < LOG_LEVEL_ERROR)
        return;

    switch (level)
    {
    case LOG_LEVEL_ERROR:
        name = "E";
        color = "\033[1;31m";
        break;
    case LOG_LEVEL_WARN:
        name = "W";
        color = "\033[1;33m";
        break;
    case LOG_LEVEL_DEBUG:
        name = "D";
        color = "\033[0;36m";
        break;
    default:
        name = "I";
        color = "\033[0;32m";
        break;
    }

    printf("\033[0m%s[%s][%s] ", color, name, module);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\033[0m\r\n");
}
