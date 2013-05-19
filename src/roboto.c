/****************************************************************************/
/**
* Pebble watch face that displays time similar to the way the Android 4.2
* clock does it.
*
* @file   roboto.c
*
* @author Bob Hauck <bobh@haucks.org>
*
* This code may be used or modified in any way whatsoever without permission
* from the author.
*
*****************************************************************************/
#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "http.h"
#include "util.h"
#include "weather_layer.h"
#include "link_monitor.h"
#include "config.h"

#define MY_UUID { 0x91, 0x41, 0xB6, 0x28, 0xBC, 0x89, 0x49, 0x8E, 0xB1, 0x47, 0x04, 0x9F, 0x49, 0xC0, 0x99, 0xAD }

PBL_APP_INFO(MY_UUID,
             "Roboto Weather", "Martin Rosinski",
             1, 5, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_WATCH_FACE);

#define TIME_FRAME      (GRect(0, 10, 144, 168-10))
#define DATE_FRAME      (GRect(0, 66, 144, 168-66))

// POST variables
#define WEATHER_KEY_LATITUDE 1
#define WEATHER_KEY_LONGITUDE 2
#define WEATHER_KEY_UNIT_SYSTEM 3
	
// Received variables
#define WEATHER_KEY_ICON 1
#define WEATHER_KEY_TEMPERATURE 2
	
#define WEATHER_HTTP_COOKIE 1949327671
#define TIME_HTTP_COOKIE 1131038282
	
/* Custom layer type for displaying time with different fonts for hour
* and minute.
*/
typedef struct _TimeLayer
{
    Layer layer;
    const char *hour_text;
    const char *minute_text;
    GFont hour_font;
    GFont minute_font;
    GTextLayoutCacheRef layout_cache;
    GColor text_color : 2;
    GColor background_color : 2;
    GTextOverflowMode overflow_mode : 2;
} TimeLayer;


Window window;          /* main window */
TextLayer date_layer;   /* layer for the date */
TimeLayer time_layer;   /* layer for the time */

GFont font_date;        /* font for date (normal) */
GFont font_hour;        /* font for hour (bold) */
GFont font_minute;      /* font for minute (thin) */

//Weather Stuff
static int our_latitude, our_longitude;
static bool located = false;

WeatherLayer weather_layer;

void request_weather();

void failed(int32_t cookie, int http_status, void* context) {
	if(cookie == 0 || cookie == WEATHER_HTTP_COOKIE) {
		weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		text_layer_set_text(&weather_layer.temp_layer, "--°");
	}
	
	link_monitor_handle_failure();
}

void success(int32_t cookie, int http_status, DictionaryIterator* received, void* context) {
	if(cookie != WEATHER_HTTP_COOKIE) return;
	Tuple* icon_tuple = dict_find(received, WEATHER_KEY_ICON);
	if(icon_tuple) {
		int icon = icon_tuple->value->int8;
		if(icon >= 0 && icon < 10) {
			weather_layer_set_icon(&weather_layer, icon);
		} else {
			weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		}
	}
	Tuple* temperature_tuple = dict_find(received, WEATHER_KEY_TEMPERATURE);
	if(temperature_tuple) {
		weather_layer_set_temperature(&weather_layer, temperature_tuple->value->int16);
	}
	
	link_monitor_handle_success();
}

void location(float latitude, float longitude, float altitude, float accuracy, void* context) {
	// Fix the floats
	our_latitude = latitude * 10000;
	our_longitude = longitude * 10000;
	located = true;
	request_weather();
}

void reconnect(void* context) {
	located = false;
	request_weather();
}

void request_weather();

/* Called by the graphics layers when the time layer needs to be updated.
*/
void time_layer_update_proc(TimeLayer *tl, GContext* ctx)
{
    if (tl->background_color != GColorClear)
    {
        graphics_context_set_fill_color(ctx, tl->background_color);
        graphics_fill_rect(ctx, tl->layer.bounds, 0, GCornerNone);
    }
    graphics_context_set_text_color(ctx, tl->text_color);

    if (tl->hour_text && tl->minute_text)
    {
        GSize hour_sz =
            graphics_text_layout_get_max_used_size(ctx,
                                                   tl->hour_text,
                                                   tl->hour_font,
                                                   tl->layer.bounds,
                                                   tl->overflow_mode,
                                                   GTextAlignmentLeft,
                                                   tl->layout_cache);
        GSize minute_sz =
            graphics_text_layout_get_max_used_size(ctx,
                                                   tl->minute_text,
                                                   tl->minute_font,
                                                   tl->layer.bounds,
                                                   tl->overflow_mode,
                                                   GTextAlignmentLeft,
                                                   tl->layout_cache);
        int width = minute_sz.w + hour_sz.w;
        int half = tl->layer.bounds.size.w / 2;
        GRect hour_bounds = tl->layer.bounds;
        GRect minute_bounds = tl->layer.bounds;

        hour_bounds.size.w = half - (width / 2) + hour_sz.w;
        minute_bounds.origin.x = hour_bounds.size.w + 1;
        minute_bounds.size.w = minute_sz.w;

        graphics_text_draw(ctx,
                           tl->hour_text,
                           tl->hour_font,
                           hour_bounds,
                           tl->overflow_mode,
                           GTextAlignmentRight,
                           tl->layout_cache);
        graphics_text_draw(ctx,
                           tl->minute_text,
                           tl->minute_font,
                           minute_bounds,
                           tl->overflow_mode,
                           GTextAlignmentLeft,
                           tl->layout_cache);
    }
}


/* Set the hour and minute text and mark the layer dirty. NOTE that the
* two strings must be static because we're only storing a pointer to them,
* not making a copy.
*/
void time_layer_set_text(TimeLayer *tl, char *hour_text, char *minute_text)
{
    tl->hour_text = hour_text;
    tl->minute_text = minute_text;

    layer_mark_dirty(&(tl->layer));
}


/* Set the time fonts. Hour and minute fonts can be different.
*/
void time_layer_set_fonts(TimeLayer *tl, GFont hour_font, GFont minute_font)
{
    tl->hour_font = hour_font;
    tl->minute_font = minute_font;

    if (tl->hour_text && tl->minute_text)
    {
        layer_mark_dirty(&(tl->layer));
    }
}


/* Set the text color of the time layer.
*/
void time_layer_set_text_color(TimeLayer *tl, GColor color)
{
    tl->text_color = color;

    if (tl->hour_text && tl->minute_text)
    {
        layer_mark_dirty(&(tl->layer));
    }
}


/* Set the background color of the time layer.
*/
void time_layer_set_background_color(TimeLayer *tl, GColor color)
{
    tl->background_color = color;

    if (tl->hour_text && tl->minute_text)
    {
        layer_mark_dirty(&(tl->layer));
    }
}


/* Initialize the time layer with default colors and fonts.
*/
void time_layer_init(TimeLayer *tl, GRect frame)
{
    layer_init(&tl->layer, frame);
    tl->layer.update_proc = (LayerUpdateProc)time_layer_update_proc;
    tl->text_color = GColorWhite;
    tl->background_color = GColorClear;
    tl->overflow_mode = GTextOverflowModeWordWrap;

    tl->hour_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    tl->minute_font = tl->hour_font;
}


/* Called by the OS once per minute. Update the time and date.
*/
void handle_minute_tick(AppContextRef ctx, PebbleTickEvent *t)
{
    /* Need to be static because pointers to them are stored in the text
    * layers.
    */
    static char date_text[] = "XXX, XXX 00";
    static char hour_text[] = "00";
    static char minute_text[] = ":00";

    (void)ctx;  /* prevent "unused parameter" warning */

    if (t->units_changed & DAY_UNIT)
    {
        string_format_time(date_text,
                           sizeof(date_text),
                           "%a, %b %d",
                           t->tick_time);
        text_layer_set_text(&date_layer, date_text);
    }

    if (clock_is_24h_style())
    {
        string_format_time(hour_text, sizeof(hour_text), "%H", t->tick_time);
    }
    else
    {
        string_format_time(hour_text, sizeof(hour_text), "%I", t->tick_time);
        if (hour_text[0] == '0')
        {
            /* This is a hack to get rid of the leading zero.
            */
            memmove(&hour_text[0], &hour_text[1], sizeof(hour_text) - 1);
        }
    }

    string_format_time(minute_text, sizeof(minute_text), ":%M", t->tick_time);
    time_layer_set_text(&time_layer, hour_text, minute_text);
	
	if(!located || !(t->tick_time->tm_min % 15))
	{
		//Every 15 minutes, request updated weather
		http_location_request();
	}
	else
	{
		//Every minute, ping the phone
		link_monitor_ping();
	}
}


/* Initialize the application.
*/
void handle_init(AppContextRef ctx)
{
    PblTm tm;
    PebbleTickEvent t;
    ResHandle res_d;
    ResHandle res_h;
    ResHandle res_m;

    window_init(&window, "Roboto");
    window_stack_push(&window, true /* Animated */);
    window_set_background_color(&window, GColorBlack);

    resource_init_current_app(&APP_RESOURCES);

    res_d = resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_21);
    res_h = resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_SUBSET_49);
    res_m = resource_get_handle(RESOURCE_ID_FONT_ROBOTO_THIN_SUBSET_49);

    font_date = fonts_load_custom_font(res_d);
    font_hour = fonts_load_custom_font(res_h);
    font_minute = fonts_load_custom_font(res_m);

    time_layer_init(&time_layer, window.layer.frame);
    time_layer_set_text_color(&time_layer, GColorWhite);
    time_layer_set_background_color(&time_layer, GColorClear);
    time_layer_set_fonts(&time_layer, font_hour, font_minute);
    layer_set_frame(&time_layer.layer, TIME_FRAME);
    layer_add_child(&window.layer, &time_layer.layer);

    text_layer_init(&date_layer, window.layer.frame);
    text_layer_set_text_color(&date_layer, GColorWhite);
    text_layer_set_background_color(&date_layer, GColorClear);
    text_layer_set_font(&date_layer, font_date);
    text_layer_set_text_alignment(&date_layer, GTextAlignmentCenter);
    layer_set_frame(&date_layer.layer, DATE_FRAME);
    layer_add_child(&window.layer, &date_layer.layer);

    get_time(&tm);
    t.tick_time = &tm;
    t.units_changed = SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT;

	// Add weather layer
	weather_layer_init(&weather_layer, GPoint(0, 100));
	layer_add_child(&window.layer, &weather_layer.layer);
	
	http_register_callbacks((HTTPCallbacks){.failure=failed,.success=success,.reconnect=reconnect,.location=location}, (void*)ctx);
	
	// Refresh time
	handle_minute_tick(ctx, &t);
}

/* Shut down the application
*/
void handle_deinit(AppContextRef ctx)
{
    fonts_unload_custom_font(font_date);
    fonts_unload_custom_font(font_hour);
    fonts_unload_custom_font(font_minute);
	
	weather_layer_deinit(&weather_layer);
}


/********************* Main Program *******************/

void pbl_main(void *params)
{
    PebbleAppHandlers handlers =
    {
        .init_handler = &handle_init,
        .deinit_handler = &handle_deinit,
        .tick_info =
        {
            .tick_handler = &handle_minute_tick,
            .tick_units = MINUTE_UNIT
        },
		.messaging_info = {
			.buffer_sizes = {
				.inbound = 256,
				.outbound = 256,
			}
		}
    };

    app_event_loop(params, &handlers);
}

void request_weather() {
	if(!located) {
		http_location_request();
		return;
	}
	// Build the HTTP request
	DictionaryIterator *body;
	HTTPResult result = http_out_get("http://pwdb.kathar.in/pebble/weather2.php", WEATHER_HTTP_COOKIE, &body);
	if(result != HTTP_OK) {
		weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		return;
	}
	dict_write_int32(body, WEATHER_KEY_LATITUDE, our_latitude);
	dict_write_int32(body, WEATHER_KEY_LONGITUDE, our_longitude);
	dict_write_cstring(body, WEATHER_KEY_UNIT_SYSTEM, UNIT_SYSTEM);
	// Send it.
	if(http_out_send() != HTTP_OK) {
		weather_layer_set_icon(&weather_layer, WEATHER_ICON_NO_WEATHER);
		return;
	}
}