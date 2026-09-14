#pragma once
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(han_font_28);
LV_FONT_DECLARE(han_font_40);
LV_FONT_DECLARE(han_font_large);
LV_FONT_DECLARE(han_font_character);
LV_FONT_DECLARE(han_font_stroke_name);
LV_IMAGE_DECLARE(han_icon_dictionary);
LV_IMAGE_DECLARE(han_icon_phonetics);
LV_IMAGE_DECLARE(han_icon_timetable);
LV_IMAGE_DECLARE(han_icon_timer);
LV_IMAGE_DECLARE(han_icon_alarm);
LV_IMAGE_DECLARE(han_icon_weather);
LV_IMAGE_DECLARE(han_icon_settings);
LV_IMAGE_DECLARE(han_icon_pinyin_search);
LV_IMAGE_DECLARE(han_icon_definition_detail);
extern const lv_image_dsc_t han_gui_strokes[8];
#ifdef __cplusplus
}
#endif
