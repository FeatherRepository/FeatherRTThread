#ifndef FEATHERTALK_UI_H
#define FEATHERTALK_UI_H

#ifdef __cplusplus
extern "C" {
#endif

int feathertalk_ui_init(void);
void feathertalk_ui_alert(const char *title, const char *message);
void feathertalk_ui_notify(const char *source, const char *title, const char *message);

/* Synchronous media ownership barrier used around filesystem export.  Calls
 * from non-LVGL threads are marshalled onto the LVGL thread and wait until all
 * filesystem-backed image sources have been detached/restored. */
int feathertalk_ui_media_freeze(void);
int feathertalk_ui_media_thaw(void);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_UI_H */
