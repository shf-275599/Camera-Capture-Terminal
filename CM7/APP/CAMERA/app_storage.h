#ifndef APP_STORAGE_H
#define APP_STORAGE_H
typedef enum { STORAGE_LOCAL, STORAGE_SWITCHING, STORAGE_USB, STORAGE_FAULT } storage_mode_t;
storage_mode_t app_storage_mode(void);
void app_storage_start(void);
int app_storage_snapshot_begin(void);
void app_storage_snapshot_end(int clean);
int app_storage_toggle(void);
int app_storage_service(void);
int app_storage_ota_lock(void);
void app_storage_ota_unlock(void);
#endif
