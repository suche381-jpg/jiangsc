#ifndef JIANG_H
#define JIANG_H

#include "drivers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WILDLIFE_MAX_RECORDS 128

typedef enum {
    PROTECTION_LEVEL_NONE = 0,
    PROTECTION_LEVEL_NATIONAL_II = 1,
    PROTECTION_LEVEL_NATIONAL_I = 2
} protection_level_t;

typedef struct {
    char name[40];
    char species[24];
    char image_path[96];
    protection_level_t level;
    char region[48];
    int quantity;
    char status[24];
    char updated_at[32];
} wildlife_record_t;

typedef struct {
    lv_obj_t *display1;
    lv_obj_t *display2;
} screen_ctx_t;

void wildlife_app_start(void);

#ifdef __cplusplus
}
#endif

#endif
