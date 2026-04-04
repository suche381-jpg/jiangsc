#ifndef WILDLIFE_UI_H
#define WILDLIFE_UI_H

#include "Wildlife_Def.h"

/* Core -> UI bridge APIs */
void ui_on_data_updated(void);
void ui_push_log(const char *msg);

/* UI entry */
void wildlife_ui_init(void);
void wildlife_app_start(void);
void Hang2Hang(void);

#endif
