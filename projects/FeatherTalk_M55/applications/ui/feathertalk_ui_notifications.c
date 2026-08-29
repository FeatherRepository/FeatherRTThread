#include <rtthread.h>
#include <string.h>
#include "feathertalk_ui_notifications.h"

static ft_notification_t s_items[FT_NOTIFICATION_CAPACITY];
static size_t s_count;
static uint32_t s_next_id = 1U;
static uint32_t s_revision;

static void revision_advance(void)
{
    s_revision++;
    if (s_revision == 0U) s_revision = 1U;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) return;
    if (source == RT_NULL) source = "";
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

void ft_notifications_init(void)
{
    rt_memset(s_items, 0, sizeof(s_items));
    s_count = 0U;
    s_next_id = 1U;
    s_revision = 1U;
}

uint32_t ft_notifications_push(const char *source, const char *title, const char *body)
{
    ft_notification_t *item;
    size_t move_count = s_count < FT_NOTIFICATION_CAPACITY ? s_count : FT_NOTIFICATION_CAPACITY - 1U;
    if (move_count > 0U)
        rt_memmove(&s_items[1], &s_items[0], move_count * sizeof(s_items[0]));
    if (s_count < FT_NOTIFICATION_CAPACITY) s_count++;
    item = &s_items[0];
    rt_memset(item, 0, sizeof(*item));
    item->id = s_next_id++;
    if (s_next_id == 0U) s_next_id = 1U;
    item->created_ms = rt_tick_get_millisecond();
    item->unread = true;
    copy_text(item->source, sizeof(item->source), source);
    copy_text(item->title, sizeof(item->title), title);
    copy_text(item->body, sizeof(item->body), body);
    revision_advance();
    return item->id;
}

size_t ft_notifications_count(void) { return s_count; }

uint32_t ft_notifications_revision(void) { return s_revision; }

size_t ft_notifications_unread_count(void)
{
    size_t i;
    size_t unread = 0U;
    for (i = 0U; i < s_count; i++) if (s_items[i].unread) unread++;
    return unread;
}

bool ft_notifications_get(size_t index, ft_notification_t *notification)
{
    if (index >= s_count || notification == RT_NULL) return false;
    *notification = s_items[index];
    return true;
}

bool ft_notifications_remove(uint32_t id)
{
    size_t i;
    for (i = 0U; i < s_count; i++)
    {
        if (s_items[i].id == id)
        {
            if (i + 1U < s_count)
                rt_memmove(&s_items[i], &s_items[i + 1U],
                           (s_count - i - 1U) * sizeof(s_items[0]));
            s_count--;
            rt_memset(&s_items[s_count], 0, sizeof(s_items[0]));
            revision_advance();
            return true;
        }
    }
    return false;
}

void ft_notifications_mark_all_read(void)
{
    size_t i;
    bool changed = false;
    for (i = 0U; i < s_count; i++)
    {
        if (s_items[i].unread)
        {
            s_items[i].unread = false;
            changed = true;
        }
    }
    if (changed) revision_advance();
}

void ft_notifications_clear(void)
{
    if (s_count == 0U) return;
    rt_memset(s_items, 0, sizeof(s_items));
    s_count = 0U;
    revision_advance();
}
