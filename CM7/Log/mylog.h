#ifndef __MY_LOG_H__
#define __MY_LOG_H__

/* 编译期日志级别：设为 LOG_LEVEL_NONE 可关闭全部日志。 */
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

void log_write(int level, const char *module, const char *format, ...);

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(module, ...) log_write(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#else
#define LOG_ERROR(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(module, ...) log_write(LOG_LEVEL_WARN, module, __VA_ARGS__)
#else
#define LOG_WARN(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(module, ...) log_write(LOG_LEVEL_INFO, module, __VA_ARGS__)
#else
#define LOG_INFO(module, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(module, ...) log_write(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#else
#define LOG_DEBUG(module, ...) ((void)0)
#endif

#endif /* __MY_LOG_H__ */
