#include "ui_styles.h"

void ui_styles_init(void)
{
    /* Set default dark theme */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
}
