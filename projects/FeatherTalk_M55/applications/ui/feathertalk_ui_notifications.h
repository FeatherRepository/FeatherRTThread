#ifndef FEATHERTALK_UI_NOTIFICATIONS_H
#define FEATHERTALK_UI_NOTIFICATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FT_NOTIFICATION_CAPACITY 8U
#define FT_NOTIFICATION_SOURCE_BYTES 20U
#define FT_NOTIFICATION_TITLE_BYTES 40U
#define FT_NOTIFICATION_BODY_BYTES 96U

typedef struct
{
    uint32_t id;
    uint32_t created_ms;
    bool unread;
    char source[FT_NOTIFICATION_SOURCE_BYTES];
    char title[FT_NOTIFICATION_TITLE_BYTES];
    char body[FT_NOTIFICATION_BODY_BYTES];
} ft_notification_t;

void ft_notifications_init(void);
uint32_t ft_notifications_push(const char *source, const char *title, const char *body);
size_t ft_notifications_count(void);
size_t ft_notifications_unread_count(void);
uint32_t ft_notifications_revision(void);
bool ft_notifications_get(size_t index, ft_notification_t *notification);
bool ft_notifications_remove(uint32_t id);
void ft_notifications_mark_all_read(void);
void ft_notifications_clear(void);

#endif /* FEATHERTALK_UI_NOTIFICATIONS_H */
