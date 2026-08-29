#ifndef FEATHERTALK_UI_H
#define FEATHERTALK_UI_H

#ifdef __cplusplus
extern "C" {
#endif

int feathertalk_ui_init(void);
void feathertalk_ui_alert(const char *title, const char *message);
void feathertalk_ui_notify(const char *source, const char *title, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* FEATHERTALK_UI_H */
