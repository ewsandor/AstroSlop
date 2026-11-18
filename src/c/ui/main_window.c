#include "main_window.h"

#include "../logging.h"
#include "../services/l10n.h"

#include <stdio.h>
#include <string.h>

#define DEG "\xc2\xb0"

typedef struct {
  int16_t status_h, label_y, label_h, az_cap_y, az_y, el_cap_y, el_y, val_h, footer_y;
  const char *status_font, *label_font, *caption_font, *footer_font;
  const char *ladder[3];
  bool round;
} Layout;

static Window *s_window;
static Window *s_about_window;
static Layer *s_canvas;
static ScrollLayer *s_about_scroll;
static TextLayer *s_about_text;
static GBitmap *s_icons[6];
static const ScreenState *s_screen;
static GFont s_value_font;
static bool s_font_precise;
static bool s_font_initialized;
static bool s_icons_inverted;
static uint8_t s_icon_errors;

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
static const uint32_t s_icon_resources[] = {
  0, RESOURCE_ID_ICON_LOCATION_25, RESOURCE_ID_ICON_LOCATION_PROBLEM_25,
  RESOURCE_ID_ICON_CLOCK_25, RESOURCE_ID_ICON_WARNING_25, RESOURCE_ID_ICON_FLASHLIGHT_25};
#else
static const uint32_t s_icon_resources[] = {
   0, RESOURCE_ID_ICON_LOCATION_18, RESOURCE_ID_ICON_LOCATION_PROBLEM_18,
  RESOURCE_ID_ICON_CLOCK_18, RESOURCE_ID_ICON_WARNING_18, RESOURCE_ID_ICON_FLASHLIGHT_18};
#endif

static Layout layout(void) {
#if defined(PBL_PLATFORM_EMERY)
  return (Layout){24, 24, 30, 64, 70, 122, 128, 50, 202,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18,
    {FONT_KEY_LECO_42_NUMBERS, FONT_KEY_LECO_38_BOLD_NUMBERS, FONT_KEY_LECO_32_BOLD_NUMBERS}, false};
#elif defined(PBL_PLATFORM_GABBRO)
  return (Layout){0, 30, 28, 60, 72, 136, 148, 64, 214,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_18,
    {FONT_KEY_LECO_42_NUMBERS, FONT_KEY_LECO_38_BOLD_NUMBERS, FONT_KEY_LECO_32_BOLD_NUMBERS}, true};
#elif defined(PBL_ROUND)
  return (Layout){0, 27, 22, 49, 59, 103, 113, 44, 0,
    FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_14_BOLD, NULL,
    {FONT_KEY_LECO_38_BOLD_NUMBERS, FONT_KEY_LECO_32_BOLD_NUMBERS, FONT_KEY_LECO_28_LIGHT_NUMBERS}, true};
#else
  return (Layout){20, 18, 22, 40, 48, 90, 96, 44, 146,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_18,
    {FONT_KEY_LECO_38_BOLD_NUMBERS, FONT_KEY_LECO_32_BOLD_NUMBERS, FONT_KEY_LECO_28_LIGHT_NUMBERS}, false};
#endif
}

static void draw_text(GContext *context, const char *text, const char *font,
                      GRect rect, GTextAlignment alignment) {
  if (!text || !text[0] || !font) {
    return;
  }
  graphics_draw_text(context, text, fonts_get_system_font(font), rect,
                     GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

static int text_width(const char *text, const char *font) {
  if (!text || !text[0]) {
    return 0;
  }
  return graphics_text_layout_get_content_size(text, fonts_get_system_font(font),
      GRect(0, 0, 500, 60), GTextOverflowModeWordWrap, GTextAlignmentLeft).w;
}

static const char *fit_font(const char *text, int width, const char *const *fonts,
                            unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    if (text_width(text, fonts[i]) <= width) return fonts[i];
  }
  return fonts[count - 1];
}

static void choose_font(const Layout *item, int16_t width) {
  const char *widest = s_screen->precise ? "359.99" DEG : "359.9" DEG;
  for (unsigned i = 0; i < ARRAY_LENGTH(item->ladder); ++i) {
    if (text_width(widest, item->ladder[i]) <= width) {
      s_value_font = fonts_get_system_font(item->ladder[i]);
      break;
    }
  }
  if (!s_value_font) {
    s_value_font = fonts_get_system_font(item->ladder[2]);
  }
  s_font_precise = s_screen->precise;
  s_font_initialized = true;
}

static void draw_icon(GContext *context, unsigned icon, int x, int y, int size) {
  if (!icon || icon >= ARRAY_LENGTH(s_icons)) {
    return;
  }
  if (!s_icons[icon]) {
    draw_text(context, "!", FONT_KEY_GOTHIC_18_BOLD, GRect(x, y, size, size),
              GTextAlignmentCenter);
    return;
  }
  graphics_context_set_compositing_mode(context, GCompOpAssign);
  graphics_draw_bitmap_in_rect(context, s_icons[icon], GRect(x, y, size, size));
}

static bool invert_bitmap(GBitmap *bitmap) {
  GBitmapFormat format = gbitmap_get_format(bitmap);
  if (format == GBitmapFormat1Bit || format == GBitmapFormat8Bit) {
    uint8_t *data = gbitmap_get_data(bitmap);
    if (!data) return false;
    size_t bytes = gbitmap_get_bytes_per_row(bitmap) * gbitmap_get_bounds(bitmap).size.h;
    uint8_t mask = format == GBitmapFormat1Bit ? 0xff : 0x3f;
    for (size_t i = 0; i < bytes; ++i) data[i] ^= mask;
    return true;
  }
  unsigned colors = format == GBitmapFormat1BitPalette ? 2 :
                    format == GBitmapFormat2BitPalette ? 4 :
                    format == GBitmapFormat4BitPalette ? 16 : 0;
  GColor *palette = colors ? gbitmap_get_palette(bitmap) : NULL;
  if (palette) {
    for (unsigned i = 0; i < colors; ++i) palette[i].argb ^= 0x3f;
    return true;
  }
  return false;
}

static void set_icon_theme(bool inverted) {
  if (s_icons_inverted == inverted) return;
  for (unsigned i = 1; i < ARRAY_LENGTH(s_icons); ++i) {
    if (s_icons[i] && !invert_bitmap(s_icons[i])) {
      ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not invert status icon %u", i);
      gbitmap_destroy(s_icons[i]);
      s_icons[i] = NULL;
    }
  }
  s_icons_inverted = inverted;
}

static void load_icon(unsigned icon) {
  if (!icon || icon >= ARRAY_LENGTH(s_icons) || s_icons[icon]) return;
  s_icons[icon] = gbitmap_create_with_resource(s_icon_resources[icon]);
  if (s_icons[icon] && s_icons_inverted && !invert_bitmap(s_icons[icon])) {
    gbitmap_destroy(s_icons[icon]);
    s_icons[icon] = NULL;
  }
  if (!s_icons[icon]) {
    if (!(s_icon_errors & (1u << icon))) {
      ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not load or theme status icon %u", icon);
      s_icon_errors |= 1u << icon;
    }
  } else {
    s_icon_errors &= ~(1u << icon);
  }
}

static void update_icons(const ScreenState *state) {
  if (state->icon != STATUS_ICON_NONE) {
    for (unsigned i = 1; i <= STATUS_ICON_WARNING; ++i) {
      if (i != state->icon && s_icons[i]) {
        gbitmap_destroy(s_icons[i]);
        s_icons[i] = NULL;
      }
    }
    load_icon(state->icon);
  }
#if defined(PBL_ROUND)
  bool flashlight_visible = state->flashlight && state->icon == STATUS_ICON_NONE;
#else
  bool flashlight_visible = state->flashlight;
#endif
  if (flashlight_visible) {
    load_icon(5);
  } else if (s_icons[5]) {
    gbitmap_destroy(s_icons[5]);
    s_icons[5] = NULL;
  }
}

static void draw_footer(GContext *context, GRect bounds, const Layout *item) {
  if (!item->footer_font) {
    return;
  }
  int width = bounds.size.w;
  if (!item->round) {
    graphics_draw_line(context, GPoint(0, item->footer_y - 2),
                       GPoint(width, item->footer_y - 2));
  }
  if (s_screen->no_coordinates) {
    if (!item->round) {
      draw_text(context, s_screen->reason, item->footer_font,
                GRect(4, item->footer_y - 2, width - 8, 24), GTextAlignmentCenter);
    }
    return;
  }
  char magnitude[32] = "";
  if (s_screen->magnitude[0]) {
    snprintf(magnitude, sizeof(magnitude), "%s %s", l10n(STR_MAGNITUDE), s_screen->magnitude);
    int available = item->round ? width - 80 : width - 8;
    if (text_width(s_screen->event, item->footer_font) + text_width(magnitude, item->footer_font) +
        (s_screen->event[0] ? 12 : 0) > available) {
      snprintf(magnitude, sizeof(magnitude), "%s %s", l10n(STR_MAG_SHORT), s_screen->magnitude);
    }
    if (text_width(s_screen->event, item->footer_font) + text_width(magnitude, item->footer_font) +
        (s_screen->event[0] ? 12 : 0) > available) {
      magnitude[0] = '\0';
    }
  }
  if (item->round) {
    char footer[72];
    snprintf(footer, sizeof(footer), "%s%s%s", s_screen->event,
             s_screen->event[0] && magnitude[0] ? "   " : "", magnitude);
    draw_text(context, footer, item->footer_font,
              GRect(40, item->footer_y, width - 80, 22), GTextAlignmentCenter);
  } else {
    draw_text(context, s_screen->event, item->footer_font,
              GRect(4, item->footer_y - 2, width - 8, 24), GTextAlignmentLeft);
    draw_text(context, magnitude, item->footer_font,
              GRect(4, item->footer_y - 2, width - 8, 24), GTextAlignmentRight);
  }
}

static void draw_main(GContext *context, GRect bounds, const Layout *item) {
  int width = bounds.size.w;
  int icon_size = width >= 200 ? 25 : 18;
  if (!s_font_initialized || s_font_precise != s_screen->precise) {
    s_value_font = NULL;
    choose_font(item, item->round ? width - 40 : width - 8);
  }
  if (!item->round) {
    draw_text(context, s_screen->clock, item->status_font,
              GRect(4, -2, 100, item->status_h), GTextAlignmentLeft);
    int icons = (s_screen->icon != STATUS_ICON_NONE) + s_screen->flashlight;
    draw_text(context, s_screen->temperature, item->status_font,
              GRect(0, -2, width - 4 - icons * icon_size, item->status_h),
              GTextAlignmentRight);
    int x = width - icon_size;
    if (s_screen->icon != STATUS_ICON_NONE) {
      draw_icon(context, s_screen->icon, x, 0, icon_size);
      x -= icon_size;
    }
    if (s_screen->flashlight) draw_icon(context, 5, x, 0, icon_size);
    graphics_draw_line(context, GPoint(0, item->status_h), GPoint(width, item->status_h));
    draw_text(context, s_screen->label, item->label_font,
              GRect(4, item->label_y - 2, width - 8, item->label_h), GTextAlignmentLeft);
    draw_text(context, l10n(STR_AZ), item->caption_font,
              GRect(4, item->az_cap_y, 30, 20), GTextAlignmentLeft);
    draw_text(context, l10n(STR_EL), item->caption_font,
              GRect(4, item->el_cap_y, 30, 20), GTextAlignmentLeft);
    graphics_draw_text(context, s_screen->azimuth, s_value_font,
                       GRect(0, item->az_y, width - 4, item->val_h),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
    graphics_draw_text(context, s_screen->elevation, s_value_font,
                       GRect(0, item->el_y, width - 4, item->val_h),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  } else {
    char status[40];
    bool show_temperature = width >= 200 && s_screen->temperature[0];
    snprintf(status, sizeof(status), "%s%s%s", s_screen->clock,
             s_screen->clock[0] && show_temperature ? "  " : "",
             show_temperature ? s_screen->temperature : "");
    int time_y = item->label_y - (width < 200 ? 19 : 22);
    draw_text(context, status, item->status_font,
              GRect(0, time_y, width, 22), GTextAlignmentCenter);
    unsigned icon = s_screen->icon ? s_screen->icon : (s_screen->flashlight ? 5 : 0);
    if (icon) {
      int x = width / 2 + text_width(status, item->status_font) / 2 + 3;
      if (x + icon_size > width - 35) x = width - 35 - icon_size;
      draw_icon(context, icon, x, time_y, icon_size);
    }
    draw_text(context, s_screen->label, item->label_font,
              GRect(36, item->label_y - 2, width - 72, item->label_h), GTextAlignmentCenter);
    draw_text(context, l10n(STR_AZ), item->caption_font,
              GRect(0, item->az_cap_y, width, 18), GTextAlignmentCenter);
    draw_text(context, l10n(STR_EL), item->caption_font,
              GRect(0, item->el_cap_y, width, 18), GTextAlignmentCenter);
    graphics_draw_text(context, s_screen->azimuth, s_value_font,
                       GRect(0, item->az_y, width, item->val_h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    graphics_draw_text(context, s_screen->elevation, s_value_font,
                       GRect(0, item->el_y, width, item->val_h),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  draw_footer(context, bounds, item);
}

static void draw_banner(GContext *context, GRect bounds, const Layout *item) {
  int width = bounds.size.w, height = bounds.size.h;
  bool small = width < 200;
  int top = item->round ? (small ? 18 : 40) : 0;
  int side = item->round ? (small ? 24 : 36) : 4;
  if (s_screen->banner_message[0]) {
    GRect message_rect = GRect(side, height / 2 - 32, width - 2 * side, 80);
    if (s_screen->filter_notice) {
      int margin = item->round ? (small ? 30 : 40) : 8;
      message_rect = GRect(side, margin, width - 2 * side, height - 2 * margin);
    }
    GFont message_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GSize message_size = graphics_text_layout_get_content_size(s_screen->banner_message,
      message_font, GRect(0, 0, message_rect.size.w, 1000),
      GTextOverflowModeWordWrap, GTextAlignmentCenter);
    if (message_size.h > message_rect.size.h) {
      message_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      message_size = graphics_text_layout_get_content_size(s_screen->banner_message,
        message_font, GRect(0, 0, message_rect.size.w, 1000),
        GTextOverflowModeWordWrap, GTextAlignmentCenter);
      if (message_size.h > message_rect.size.h) {
        message_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        message_size = graphics_text_layout_get_content_size(s_screen->banner_message,
          message_font, GRect(0, 0, message_rect.size.w, 1000),
          GTextOverflowModeWordWrap, GTextAlignmentCenter);
      }
    }
    if (s_screen->filter_notice && message_size.h < message_rect.size.h) {
      message_rect.origin.y += (message_rect.size.h - message_size.h) / 2;
      message_rect.size.h = message_size.h;
    }
    graphics_draw_text(context, s_screen->banner_message,
      message_font, message_rect,
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }
  char index[24];
  snprintf(index, sizeof(index), "%u / %u", s_screen->banner_index, s_screen->banner_count);
  const char *title_fonts[] = {FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_14_BOLD};
  const char *title_font = fit_font(s_screen->banner_catalog, width - 2 * side,
                                    title_fonts, ARRAY_LENGTH(title_fonts));
  bool separate_index = item->round ||
    text_width(s_screen->banner_catalog, title_font) + text_width(index, FONT_KEY_GOTHIC_18_BOLD) >
    width - 2 * side - 8;
  draw_text(context, s_screen->banner_catalog, title_font,
            GRect(side, top, width - 2 * side, 22),
            separate_index ? GTextAlignmentCenter : GTextAlignmentLeft);
  draw_text(context, index, item->round ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD,
            separate_index ? GRect(0, top + 18, width, 22) : GRect(0, top, width - 4, 22),
            separate_index ? GTextAlignmentCenter : GTextAlignmentRight);
  int id_y = item->round && small ? 58 : height / 2 - (small ? 44 : 50);
  const char *primary = s_screen->banner_id[0] ? s_screen->banner_id : s_screen->banner_name;
  const char *secondary = s_screen->banner_id[0] ? s_screen->banner_name : "";
  const char *primary_fonts[] = {FONT_KEY_BITHAM_42_BOLD, FONT_KEY_GOTHIC_28_BOLD,
                                 FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_18_BOLD};
  const char *primary_font = fit_font(primary, width - 2 * side,
                                     primary_fonts, ARRAY_LENGTH(primary_fonts));
  draw_text(context, primary, primary_font,
            GRect(side, id_y, width - 2 * side, 50), GTextAlignmentCenter);
  if (item->round && small) id_y -= 4;
  int secondary_y = id_y + 52;
  graphics_draw_text(context, secondary,
    fonts_get_system_font(width >= 200 ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD),
    GRect(side, secondary_y, width - 2 * side, 70),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  if (width >= 200 && height >= 220 && s_screen->banner_coordinates[0]) {
    const char *preview_fonts[] = {FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_14};
    const char *preview_font = fit_font(s_screen->banner_coordinates, width - 2 * side,
                                        preview_fonts, ARRAY_LENGTH(preview_fonts));
    draw_text(context, s_screen->banner_coordinates, preview_font,
              GRect(side, secondary_y + 72, width - 2 * side, 30),
              GTextAlignmentCenter);
  }
}

static void canvas_update(Layer *layer, GContext *context) {
  if (!s_screen) return;
  GRect bounds = layer_get_bounds(layer);
  Layout item = layout();
  GColor background = s_screen->inverted ? GColorBlack : GColorWhite;
  GColor foreground = s_screen->inverted ? GColorWhite : GColorBlack;
  graphics_context_set_fill_color(context, background);
  graphics_fill_rect(context, bounds, 0, GCornerNone);
  graphics_context_set_text_color(context, foreground);
  graphics_context_set_stroke_color(context, foreground);
  if (s_screen->banner) draw_banner(context, bounds, &item);
  else draw_main(context, bounds, &item);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  if (!s_canvas) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not create main display layer");
    return;
  }
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  (void)window;
  for (unsigned i = 1; i < ARRAY_LENGTH(s_icons); ++i) {
    if (s_icons[i]) gbitmap_destroy(s_icons[i]);
    s_icons[i] = NULL;
  }
  if (s_canvas) layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void about_load(Window *window) {
  if (!s_screen) return;
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int side = PBL_IF_ROUND_ELSE(bounds.size.w / 7, 4);
  int top = PBL_IF_ROUND_ELSE(bounds.size.h / 8, 0);
  s_about_scroll = scroll_layer_create(bounds);
  s_about_text = text_layer_create(GRect(side, top, bounds.size.w - 2 * side, 1000));
  if (!s_about_scroll || !s_about_text) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not create About screen");
    return;
  }
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  text_layer_set_font(s_about_text, font);
  const char *about_text = l10n(STR_ABOUT_BODY);
  text_layer_set_text(s_about_text, about_text);
  text_layer_set_background_color(s_about_text, s_screen->inverted ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_about_text, s_screen->inverted ? GColorWhite : GColorBlack);
  text_layer_set_text_alignment(s_about_text,
                                PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  GSize size = graphics_text_layout_get_content_size(about_text, font,
    GRect(0, 0, bounds.size.w - 2 * side, 1500),
    GTextOverflowModeWordWrap, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  layer_set_frame(text_layer_get_layer(s_about_text),
                  GRect(side, top, bounds.size.w - 2 * side, size.h + 8));
  scroll_layer_add_child(s_about_scroll, text_layer_get_layer(s_about_text));
  scroll_layer_set_content_size(s_about_scroll, GSize(bounds.size.w, size.h + top + 16));
  scroll_layer_set_click_config_onto_window(s_about_scroll, window);
  layer_add_child(root, scroll_layer_get_layer(s_about_scroll));
  window_set_background_color(window, s_screen->inverted ? GColorBlack : GColorWhite);
}

static void about_unload(Window *window) {
  (void)window;
  if (s_about_text) text_layer_destroy(s_about_text);
  if (s_about_scroll) scroll_layer_destroy(s_about_scroll);
  s_about_text = NULL;
  s_about_scroll = NULL;
}

bool ui_init(ClickConfigProvider click_provider) {
  s_window = window_create();
  s_about_window = window_create();
  if (!s_window || !s_about_window) {
    ASTRO_LOG(APP_LOG_LEVEL_ERROR, "Could not create application windows");
    return false;
  }
  window_set_click_config_provider(s_window, click_provider);
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_set_window_handlers(s_about_window, (WindowHandlers){.load = about_load, .unload = about_unload});
  window_stack_push(s_window, true);
  return s_canvas != NULL;
}

void ui_deinit(void) {
  if (s_about_window) window_destroy(s_about_window);
  if (s_window) window_destroy(s_window);
  s_about_window = NULL;
  s_window = NULL;
  s_screen = NULL;
}

void ui_update(const ScreenState *state) {
  if (!state) return;
  set_icon_theme(state->inverted);
  update_icons(state);
  s_screen = state;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

void ui_show_about(void) {
  if (s_about_window && !window_stack_contains_window(s_about_window)) {
    window_stack_push(s_about_window, true);
  }
}
