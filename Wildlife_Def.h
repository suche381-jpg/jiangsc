#ifndef WILDLIFE_DEF_H
#define WILDLIFE_DEF_H

#include <stdint.h>

#define WL_MAX_RECORDS 32
#define WL_STATUS_BUCKETS 5
#define WL_DEFAULT_UPDATED_AT "00/00/00/00/00"

typedef enum {
    WL_MODE_BY_CATEGORY = 0,
    WL_MODE_BY_LEVEL = 1,
    WL_MODE_BY_AREA = 2
} wildlife_mode_t;

typedef enum {
    WL_OK = 0,
    WL_ERR_PARAM,
    WL_ERR_NOT_FOUND,
    WL_ERR_NO_SPACE,
    WL_ERR_IO,
    WL_ERR_FORMAT
} wildlife_result_t;

typedef struct {
    char name[24];
    char category[20];
    char image[48];
    char level[20];
    char area[24];
    int32_t quantity;
    char status[20];
    char updated_at[24];
    uint8_t valid;
} wildlife_record_t;

typedef struct {
    uint16_t valid_count;
    uint16_t filtered_count;
    uint16_t critical_count;
    uint16_t decreasing_count;
    int32_t total_quantity;
    uint16_t status_percent[WL_STATUS_BUCKETS];
} wildlife_stats_t;

#endif
