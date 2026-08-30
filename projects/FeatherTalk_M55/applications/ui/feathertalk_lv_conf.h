#ifndef FEATHERTALK_LV_CONF_H
#define FEATHERTALK_LV_CONF_H

/* Keep the SDK-wide LVGL defaults untouched.  This product overlay only
 * enables the file-backed image features used by the Gallery application. */
#include "../../../../libraries/Common/board/ports/lvgl/lv_conf.h"

#undef LV_FS_DEFAULT_DRIVE_LETTER
#define LV_FS_DEFAULT_DRIVE_LETTER 'P'

#undef LV_USE_FS_POSIX
#define LV_USE_FS_POSIX 1
#undef LV_FS_POSIX_LETTER
#define LV_FS_POSIX_LETTER 'P'
#undef LV_FS_POSIX_PATH
#define LV_FS_POSIX_PATH ""
#undef LV_FS_POSIX_CACHE_SIZE
#define LV_FS_POSIX_CACHE_SIZE 0

/* TJPGD decodes baseline JPEG a block at a time.  PNG is deliberately
 * accepted only under a tight pixel limit in the Gallery because LodePNG
 * expands the complete image to ARGB8888.  BMP decoding is streamed. */
#undef LV_USE_TJPGD
#define LV_USE_TJPGD 1
#undef LV_USE_LODEPNG
#define LV_USE_LODEPNG 1
#undef LV_USE_BMP
#define LV_USE_BMP 1

#endif /* FEATHERTALK_LV_CONF_H */
