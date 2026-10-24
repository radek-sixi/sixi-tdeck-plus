// SPDX-License-Identifier: Apache-2.0
// Faro LVGL 9.3 configuration (shared by ESP32-S3 firmware and native SDL2 simulator)
#ifndef LV_CONF_H
#define LV_CONF_H

#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  16      /* [ms] ~60 FPS */
#define LV_DPI_DEF          160     /* [px/inch] 2.8" 320x240 IPS */

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_DRAW_BUF_STRIDE_ALIGN                1
#define LV_DRAW_BUF_ALIGN                       4
#define LV_USE_DRAW_SW                          1
#define LV_DRAW_SW_SUPPORT_RGB565               1
#define LV_DRAW_SW_SUPPORT_RGB565A8             1
#define LV_DRAW_SW_SUPPORT_RGB888               1
#define LV_DRAW_SW_SUPPORT_XRGB8888             1
#define LV_DRAW_SW_SUPPORT_ARGB8888             1
#define LV_DRAW_SW_SUPPORT_L8                   1
#define LV_DRAW_SW_SUPPORT_AL88                 1
#define LV_DRAW_SW_SUPPORT_A8                   1
#define LV_DRAW_SW_SUPPORT_I1                   1
#define LV_DRAW_SW_DRAW_UNIT_CNT                1
#define LV_DRAW_SW_COMPLEX                      1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE            0
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE            4

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

/*==================
 *   FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   1
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_UNSCII_8        1
#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE   1
#define LV_USE_FONT_COMPRESSED  0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 * WIDGETS
 *==================*/
#define LV_WIDGETS_HAS_DEFAULT_VALUE  1
#define LV_USE_ANIMIMG          1
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     1
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           1
#define LV_USE_CHART            1
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMAGE            1
#define LV_USE_IMAGEBUTTON      1
#define LV_USE_KEYBOARD         1
#define LV_USE_LABEL            1
#define LV_LABEL_TEXT_SELECTION 1
#define LV_LABEL_LONG_TXT_HINT  1
#define LV_LABEL_WAIT_CHAR_COUNT 3
#define LV_USE_LED              1
#define LV_USE_LINE             1
#define LV_USE_LIST             1
#define LV_USE_MENU             1
#define LV_USE_MSGBOX           1
#define LV_USE_ROLLER           1
#define LV_USE_SCALE            1
#define LV_USE_SLIDER           1
#define LV_USE_SPAN             1
#define LV_USE_SPINBOX          1
#define LV_USE_SPINNER          1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#define LV_USE_TABLE            1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         1
#define LV_USE_WIN              1

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_GROW   1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE     1
#define LV_USE_THEME_MONO       0

/*==================
 * LAYOUTS
 *==================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * 3RD PARTY LIBRARIES
 *==================*/
#define LV_USE_LODEPNG          1
#define LV_USE_QRCODE           1

/*==================
 * OTHERS
 *==================*/
#define LV_USE_SNAPSHOT         1
#define LV_USE_SYSMON           0
#define LV_USE_PROFILER         0
#define LV_USE_MONKEY           0
#define LV_USE_GRIDNAV          1
#define LV_USE_FRAGMENT         0
#define LV_USE_IMGFONT          0
#define LV_USE_OBSERVER         1
#define LV_USE_IME_PINYIN       0
#define LV_USE_FILE_EXPLORER    0

#endif /* LV_CONF_H */
