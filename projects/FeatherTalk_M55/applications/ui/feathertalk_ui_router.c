#include <rtthread.h>
#include "feathertalk_ui_internal.h"

#define FT_ROUTER_MAX_DEPTH 8U

typedef struct
{
    const ft_page_definition_t *definition;
    lv_obj_t *view;
} ft_route_entry_t;

static lv_obj_t *s_route_host;
static ft_route_entry_t s_route_stack[FT_ROUTER_MAX_DEPTH];
static size_t s_route_depth;

int ft_router_init(lv_obj_t *host)
{
    if (host == RT_NULL)
    {
        return -RT_EINVAL;
    }

    s_route_host = host;
    s_route_depth = 0U;
    return ft_router_push(FT_PAGE_HOME);
}

int ft_router_push(ft_page_id_t page_id)
{
    const ft_page_definition_t *definition;
    lv_obj_t *view;

    if ((s_route_host == RT_NULL) || (s_route_depth >= FT_ROUTER_MAX_DEPTH))
    {
        return -RT_EFULL;
    }

    definition = ft_pages_find(page_id);
    if ((definition == RT_NULL) || (definition->create == RT_NULL))
    {
        return -RT_ENOSYS;
    }

    if (s_route_depth > 0U)
    {
        lv_obj_add_flag(s_route_stack[s_route_depth - 1U].view, LV_OBJ_FLAG_HIDDEN);
    }

    view = definition->create(s_route_host);
    if (view == RT_NULL)
    {
        if (s_route_depth > 0U)
        {
            lv_obj_remove_flag(s_route_stack[s_route_depth - 1U].view, LV_OBJ_FLAG_HIDDEN);
        }
        return -RT_ENOMEM;
    }

    lv_obj_set_size(view, lv_pct(100), lv_pct(100));
    lv_obj_align(view, LV_ALIGN_CENTER, 0, 0);
    ft_ui_register_page_background(view);
    s_route_stack[s_route_depth].definition = definition;
    s_route_stack[s_route_depth].view = view;
    s_route_depth++;

    if (definition->on_enter != RT_NULL)
    {
        definition->on_enter();
    }

    return RT_EOK;
}

bool ft_router_back(void)
{
    ft_route_entry_t *top;

    if (s_route_depth <= 1U)
    {
        ft_pages_show_start();
        return false;
    }

    top = &s_route_stack[s_route_depth - 1U];
    if ((top->definition->on_back != RT_NULL) && top->definition->on_back())
    {
        return true;
    }

    if (top->definition->on_leave != RT_NULL)
    {
        top->definition->on_leave();
    }

    lv_obj_delete(top->view);
    top->definition = RT_NULL;
    top->view = RT_NULL;
    s_route_depth--;
    lv_obj_remove_flag(s_route_stack[s_route_depth - 1U].view, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void ft_router_home(void)
{
    while (s_route_depth > 1U)
    {
        (void)ft_router_back();
    }
    ft_pages_show_start();
}

int ft_router_refresh_all(void)
{
    ft_page_id_t page_ids[FT_ROUTER_MAX_DEPTH];
    size_t depth = s_route_depth;
    size_t i;
    int result = RT_EOK;

    if (s_route_host == RT_NULL || depth == 0U) return -RT_EINVAL;
    for (i = 0U; i < depth; i++)
        page_ids[i] = s_route_stack[i].definition->id;
    while (s_route_depth > 0U)
    {
        ft_route_entry_t *top = &s_route_stack[s_route_depth - 1U];
        if (top->definition != RT_NULL && top->definition->on_leave != RT_NULL)
            top->definition->on_leave();
        if (top->view != RT_NULL) lv_obj_delete(top->view);
        top->definition = RT_NULL;
        top->view = RT_NULL;
        s_route_depth--;
    }
    for (i = 0U; i < depth; i++)
    {
        result = ft_router_push(page_ids[i]);
        if (result != RT_EOK) break;
    }
    return result;
}

size_t ft_router_depth(void)
{
    return s_route_depth;
}

ft_page_id_t ft_router_current_page(void)
{
    if (s_route_depth == 0U)
    {
        return FT_PAGE_COUNT;
    }
    return s_route_stack[s_route_depth - 1U].definition->id;
}
