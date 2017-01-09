#include <pebble.h>
#include <pebble-events/pebble-events.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/ffont.h>
#include <pebble-connection-vibes/connection-vibes.h>
#include <pebble-hourly-vibes/hourly-vibes.h>
#include "enamel.h"

static FFont *s_font;
static Window *s_window;

static Layer *s_ticks_layer;
static Layer *s_hands_layer;

static EventHandle s_tick_timer_event_handle;
static struct tm s_tick_time;

static EventHandle s_settings_event_handle;

static EventHandle s_connection_event_handle;
static bool s_connected;

#ifdef PBL_RECT
static int32_t abs32(const int32_t a) {
    return (a^(a>>31)) - (a>>31);
}

static FPoint fpoint_on_rect(const GRect r, const int angle) {
    int32_t sin = sin_lookup(angle);
    int32_t cos = cos_lookup(angle);
    int32_t dy = sin > 0 ? (r.size.h / 2) : ((0 - r.size.h) / 2);
    int32_t dx = cos > 0 ? (r.size.w / 2) : ((0 - r.size.w) / 2);
    if (abs32(dx * sin) < abs32(dy * cos)) {
        dy = (dx * sin) / cos;
    } else {
        dx = (dy * cos) / sin;
    }
    return FPointI(dx + r.origin.x + (r.size.w / 2), dy + r.origin.y + (r.size.h / 2));
}

#define fpoint_on_edge(bounds, angle) fpoint_on_rect((bounds), (angle))
#else
#define fpoint_on_edge(bounds, angle) g2fpoint(gpoint_from_polar((bounds), GOvalScaleModeFitCircle, (angle)))
#endif //PBL_RECT

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    memcpy(&s_tick_time, tick_time, sizeof(struct tm));
    if (s_tick_time.tm_hour != 12) s_tick_time.tm_hour %= 12;
#ifdef DEMO
    s_tick_time.tm_hour = 3;
    s_tick_time.tm_min = 0;
    s_tick_time.tm_sec = 35;
    s_tick_time.tm_mday = 22;
#endif
    layer_mark_dirty(s_hands_layer);
}

static void connection_handler(bool connected) {
    s_connected = connected;
    layer_mark_dirty(s_hands_layer);
}

static void ticks_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_unobstructed_bounds(layer);
#ifdef PBL_ROUND
    bounds = grect_crop(bounds, 4);
#endif

    FContext fctx;
#if defined(PBL_COLOR) && defined(PBL_RECT)
    fctx_enable_aa(false);
#endif
    fctx_init_context(&fctx, ctx);

    int16_t font_size = bounds.size.w / 10;
    fctx_set_text_em_height(&fctx, s_font, font_size);
#ifndef PBL_COLOR
    fctx_set_fill_color(&fctx, GColorWhite);
#endif

    for (int i = 1; i <= 12; i++) {
        bool date_spot = enamel_get_ENABLE_DATE() && i == 3;

#ifdef PBL_COLOR
        fctx_set_fill_color(&fctx, s_tick_time.tm_hour == i ? GColorWhite : GColorLightGray);
#endif
        fctx_begin_fill(&fctx);

        int32_t angle = PBL_IF_ROUND_ELSE(i, i - 3) * TRIG_MAX_ANGLE / 12;
#ifdef PBL_RECT
        switch (i) {
            case 2: case 3: case 4:  fctx_set_rotation(&fctx, DEG_TO_TRIGANGLE(90)); break;
            case 5: case 6: case 7:  fctx_set_rotation(&fctx, DEG_TO_TRIGANGLE(180)); break;
            case 8: case 9: case 10: fctx_set_rotation(&fctx, DEG_TO_TRIGANGLE(270)); break;
            default: fctx_set_rotation(&fctx, 0); break;
        }
        if (date_spot) fctx_set_rotation(&fctx, 0);
#else
        fctx_set_rotation(&fctx, date_spot ? 0 : angle);
#endif

        FPoint offset = fpoint_on_edge(bounds, angle);
        fctx_set_offset(&fctx, offset);

        char s[3];
        snprintf(s, sizeof(s), "%02d", date_spot ? s_tick_time.tm_mday : i);
        fctx_draw_string(&fctx, s, s_font,
                         date_spot ? GTextAlignmentRight : GTextAlignmentCenter,
                         date_spot ? FTextAnchorMiddle : FTextAnchorTop);

        fctx_end_fill(&fctx);
    }

    fctx_deinit_context(&fctx);
}

static void fctx_draw_line(FContext *fctx) {
    fctx_begin_fill(fctx);
    fctx_move_to(fctx, FPoint(-1, 0));
    fctx_line_to(fctx, FPoint(-1, -1));
    fctx_line_to(fctx, FPoint(1, -1));
    fctx_line_to(fctx, FPoint(1, 0));
    fctx_close_path(fctx);
    fctx_end_fill(fctx);
}

static void hands_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_unobstructed_bounds(layer);
    FPoint center = FPointI(bounds.size.w / 2, bounds.size.h / 2);

    FContext fctx;
#ifdef PBL_COLOR
    fctx_enable_aa(true);
#endif
    fctx_init_context(&fctx, ctx);

    fctx_set_offset(&fctx, center);

    int32_t angle = s_tick_time.tm_min * TRIG_MAX_ANGLE / 60;
    fctx_set_rotation(&fctx, angle);
    fctx_set_fill_color(&fctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorDarkGray));
    fctx_set_scale(&fctx, FPointOne, FPointI(2, (bounds.size.h - PBL_IF_ROUND_ELSE(60, 65)) / 2));
    fctx_draw_line(&fctx);

    angle = (TRIG_MAX_ANGLE * ((s_tick_time.tm_hour * 6) + (s_tick_time.tm_min / 10))) / (12 * 6);
    fctx_set_rotation(&fctx, angle);
    fctx_set_fill_color(&fctx, PBL_IF_COLOR_ELSE(enamel_get_HOUR_HAND_COLOR(), GColorWhite));
    fctx_set_scale(&fctx, FPointOne, FPointI(2, (bounds.size.h - 80) / 2));
    fctx_draw_line(&fctx);

    if (enamel_get_ENABLE_SECONDS()) {
        angle = s_tick_time.tm_sec * TRIG_MAX_ANGLE / 60;
        fctx_set_rotation(&fctx, angle);
        fctx_set_fill_color(&fctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
        fctx_set_scale(&fctx, FPointOne, FPointI(1, (bounds.size.h - PBL_IF_ROUND_ELSE(60, 65)) / 2));
        fctx_draw_line(&fctx);
    }

    fctx_set_fill_color(&fctx, PBL_IF_COLOR_ELSE(enamel_get_HOUR_HAND_COLOR(), GColorWhite));
    fctx_begin_fill(&fctx);
    fctx_plot_circle(&fctx, &center, INT_TO_FIXED(4));
    fctx_end_fill(&fctx);

    fctx_set_fill_color(&fctx, GColorBlack);
    fctx_begin_fill(&fctx);
    fctx_plot_circle(&fctx, &center, INT_TO_FIXED(s_connected ? 1 : 3));
    fctx_end_fill(&fctx);

    fctx_deinit_context(&fctx);
}

static void settings_received_handler(void *context) {
    connection_vibes_set_state(atoi(enamel_get_CONNECTION_VIBE()));
    hourly_vibes_set_enabled(enamel_get_HOURLY_VIBE());
#ifdef PBL_HEALTH
    connection_vibes_enable_health(enamel_get_ENABLE_HEALTH());
    hourly_vibes_enable_health(enamel_get_ENABLE_HEALTH());
#endif
    if (s_tick_timer_event_handle) events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);
    time_t now = time(NULL);
    tick_handler(localtime(&now), enamel_get_ENABLE_SECONDS() ? SECOND_UNIT : MINUTE_UNIT);
    s_tick_timer_event_handle = events_tick_timer_service_subscribe(enamel_get_ENABLE_SECONDS() ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void window_load(Window *window) {
    window_set_background_color(window, GColorBlack);
    Layer *root_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root_layer);

    s_ticks_layer = layer_create(bounds);
    layer_set_update_proc(s_ticks_layer, ticks_update_proc);
    layer_add_child(root_layer, s_ticks_layer);

    s_hands_layer = layer_create(bounds);
    layer_set_update_proc(s_hands_layer, hands_update_proc);
    layer_add_child(root_layer, s_hands_layer);

    settings_received_handler(NULL);
    s_settings_event_handle = enamel_settings_received_subscribe(settings_received_handler, NULL);

    connection_handler(connection_service_peek_pebble_app_connection());
    s_connection_event_handle = events_connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = connection_handler
    });
}

static void window_unload(Window *window) {
    events_connection_service_unsubscribe(s_connection_event_handle);
    events_tick_timer_service_unsubscribe(s_tick_timer_event_handle);
    enamel_settings_received_unsubscribe(s_settings_event_handle);
    layer_destroy(s_hands_layer);
    layer_destroy(s_ticks_layer);
}

static void init(void) {
    enamel_init();
    connection_vibes_init();
    hourly_vibes_init();
    uint32_t const pattern[] = { 100 };
    hourly_vibes_set_pattern((VibePattern) {
        .durations = pattern,
        .num_segments = 1
    });

    events_app_message_open();

    s_font = ffont_create_from_resource(RESOURCE_ID_LECO_FFONT);

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload
    });
    window_stack_push(s_window, true);
}

static void deinit(void) {
    window_destroy(s_window);

    ffont_destroy(s_font);

    hourly_vibes_deinit();
    connection_vibes_deinit();
    enamel_deinit();
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
