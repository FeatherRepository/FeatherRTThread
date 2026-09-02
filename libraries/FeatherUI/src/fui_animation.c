#include <limits.h>
#include <string.h>

#include "feather_ui.h"

#define FUI_ANIMATION_Q_ONE 65536L

typedef struct
{
    fui_animation_spec_t spec;
    uint32_t cycle_start_ms;
    int32_t cycle_from;
    int32_t cycle_to;
    int32_t last_value;
    uint16_t repeats_left;
    bool active;
    bool applied;
} fui_animation_slot_t;

static fui_animation_slot_t s_slots[FUI_ANIMATION_CAPACITY];
static fui_animation_stats_t s_stats;

static int32_t clamp_q(int64_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return (int32_t)value;
}

static int32_t mul_q(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 16);
}

static int32_t easing_q(fui_easing_t easing, int32_t progress)
{
    int32_t t = clamp_q(progress, 0, FUI_ANIMATION_Q_ONE);
    int32_t inverse;
    int32_t squared;
    int32_t cubic;

    switch (easing)
    {
    case FUI_EASING_OUT_CUBIC:
        inverse = FUI_ANIMATION_Q_ONE - t;
        squared = mul_q(inverse, inverse);
        cubic = mul_q(squared, inverse);
        return FUI_ANIMATION_Q_ONE - cubic;

    case FUI_EASING_IN_OUT_CUBIC:
        if (t < FUI_ANIMATION_Q_ONE / 2)
        {
            squared = mul_q(t, t);
            cubic = mul_q(squared, t);
            return cubic * 4;
        }
        inverse = FUI_ANIMATION_Q_ONE - t;
        squared = mul_q(inverse, inverse);
        cubic = mul_q(squared, inverse);
        return FUI_ANIMATION_Q_ONE - cubic * 4;

    case FUI_EASING_OUT_BACK:
    {
        /* Standard back-out curve, represented in Q16.16.  Overshoot is
         * intentional and is not clamped to one. */
        const int32_t c1 = 111515; /* 1.70158 */
        const int32_t c3 = 177051; /* c1 + 1 */
        int32_t u = t - FUI_ANIMATION_Q_ONE;
        int32_t u2 = mul_q(u, u);
        int32_t u3 = mul_q(u2, u);
        return FUI_ANIMATION_Q_ONE + mul_q(c3, u3) + mul_q(c1, u2);
    }

    case FUI_EASING_OUT_SPRING:
    {
        /* A small fixed-point damped-spring curve.  The table defines the
         * easing algorithm, not a screen-dependent animation value. */
        static const int32_t samples[] =
        {
            0, 22938, 49152, 69468, 76022, 72090, 65536, 62259, 62915,
            65536, 66847, 66519, 65536, 65077, 65274, 65536, 65536
        };
        uint32_t scaled = (uint32_t)t *
                          (uint32_t)(sizeof(samples) / sizeof(samples[0]) - 1U);
        uint32_t index = scaled >> 16;
        uint32_t fraction = scaled & 0xffffU;
        int32_t a;
        int32_t b;
        if (index >= sizeof(samples) / sizeof(samples[0]) - 1U)
            return samples[sizeof(samples) / sizeof(samples[0]) - 1U];
        a = samples[index];
        b = samples[index + 1U];
        return a + (int32_t)(((int64_t)(b - a) * fraction) >> 16);
    }

    case FUI_EASING_LINEAR:
    default:
        return t;
    }
}

static int32_t interpolate(int32_t from, int32_t to, int32_t progress_q)
{
    int64_t value = (int64_t)from +
        (((int64_t)to - from) * progress_q >> 16);
    if (value < INT32_MIN) return INT32_MIN;
    if (value > INT32_MAX) return INT32_MAX;
    return (int32_t)value;
}

static void refresh_active_stats(void)
{
    uint16_t active = 0U;
    size_t i;
    for (i = 0U; i < FUI_ANIMATION_CAPACITY; i++)
        if (s_slots[i].active) active++;
    s_stats.active = active;
    if (active > s_stats.peak_active) s_stats.peak_active = active;
}

void fui_animation_cancel(void *target, uint16_t property)
{
    size_t i;
    for (i = 0U; i < FUI_ANIMATION_CAPACITY; i++)
        if (s_slots[i].active && s_slots[i].spec.target == target &&
            (property == FUI_ANIMATION_PROPERTY_ALL ||
             s_slots[i].spec.property == property))
        {
            s_slots[i].active = false;
            s_stats.cancelled++;
        }
    refresh_active_stats();
}

void fui_animation_cancel_all(void)
{
    size_t i;
    for (i = 0U; i < FUI_ANIMATION_CAPACITY; i++)
        if (s_slots[i].active)
        {
            s_slots[i].active = false;
            s_stats.cancelled++;
        }
    refresh_active_stats();
}

int fui_animation_start(const fui_animation_spec_t *spec,
                        uint32_t start_time_ms)
{
    fui_animation_slot_t *slot = NULL;
    size_t i;
    if (spec == NULL || spec->target == NULL || spec->apply == NULL ||
        spec->duration_ms == 0U || spec->easing > FUI_EASING_OUT_SPRING)
        return -1;

    fui_animation_cancel(spec->target, spec->property);
    for (i = 0U; i < FUI_ANIMATION_CAPACITY; i++)
        if (!s_slots[i].active)
        {
            slot = &s_slots[i];
            break;
        }
    if (slot == NULL) return -2;

    memset(slot, 0, sizeof(*slot));
    slot->spec = *spec;
    slot->cycle_start_ms = start_time_ms + spec->delay_ms;
    slot->cycle_from = spec->from;
    slot->cycle_to = spec->to;
    slot->last_value = spec->from;
    slot->repeats_left = spec->repeat_count;
    slot->active = true;
    s_stats.started++;
    refresh_active_stats();
    return 0;
}

bool fui_animation_update(uint32_t now_ms)
{
    bool changed = false;
    size_t i;
    for (i = 0U; i < FUI_ANIMATION_CAPACITY; i++)
    {
        fui_animation_slot_t *slot = &s_slots[i];
        int32_t elapsed;
        int32_t progress;
        int32_t eased;
        int32_t value;
        if (!slot->active) continue;
        elapsed = (int32_t)(now_ms - slot->cycle_start_ms);
        if (elapsed < 0) continue;
        if ((uint32_t)elapsed >= slot->spec.duration_ms)
            progress = FUI_ANIMATION_Q_ONE;
        else
            progress = (int32_t)(((uint64_t)(uint32_t)elapsed << 16) /
                                 slot->spec.duration_ms);
        eased = easing_q(slot->spec.easing, progress);
        value = interpolate(slot->cycle_from, slot->cycle_to, eased);
        if (!slot->applied || value != slot->last_value)
        {
            slot->spec.apply(slot->spec.target, slot->spec.property, value);
            slot->last_value = value;
            slot->applied = true;
            changed = true;
        }
        if (progress != FUI_ANIMATION_Q_ONE) continue;

        /* End every cycle at the exact target even for overshooting curves. */
        if (slot->last_value != slot->cycle_to)
        {
            slot->spec.apply(slot->spec.target, slot->spec.property,
                             slot->cycle_to);
            slot->last_value = slot->cycle_to;
            changed = true;
        }
        if (slot->repeats_left == FUI_ANIMATION_REPEAT_FOREVER ||
            slot->repeats_left > 0U)
        {
            int32_t old_from = slot->cycle_from;
            if (slot->repeats_left != FUI_ANIMATION_REPEAT_FOREVER)
                slot->repeats_left--;
            if (slot->spec.autoreverse)
            {
                slot->cycle_from = slot->cycle_to;
                slot->cycle_to = old_from;
            }
            slot->cycle_start_ms += slot->spec.duration_ms;
            slot->applied = false;
        }
        else
        {
            fui_animation_complete_cb_t complete = slot->spec.complete;
            void *target = slot->spec.target;
            uint16_t property = slot->spec.property;
            slot->active = false;
            s_stats.completed++;
            if (complete != NULL) complete(target, property);
        }
    }
    refresh_active_stats();
    return changed;
}

bool fui_animation_is_active(void)
{
    return s_stats.active != 0U;
}

void fui_animation_get_stats(fui_animation_stats_t *stats)
{
    if (stats != NULL) *stats = s_stats;
}
