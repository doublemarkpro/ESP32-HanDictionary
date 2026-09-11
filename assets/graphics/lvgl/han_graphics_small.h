#ifndef HAN_GRAPHICS_SMALL_H
#define HAN_GRAPHICS_SMALL_H
#include <stddef.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char* id;
    const lv_image_dsc_t* image;
} han_graphic_entry_t;
size_t han_graphics_count(void);
const han_graphic_entry_t* han_graphics_at(size_t index);
/* Returns NULL for missing ID. Descriptors and PNG data have static lifetime. */
const lv_image_dsc_t* han_graphics_find(const char* id);
#ifdef __cplusplus
}
#endif
#endif
