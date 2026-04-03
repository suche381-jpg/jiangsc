#include "drivers.h"
#include "jiang.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DATA_DIR "0:/wildlife"
#define DATA_FILE "0:/wildlife/animals.csv"
#define EXPORT_DIR "0:/export"
#define CSV_LINE_BUF_SIZE 320
#define BASE_YEAR 2026U
#define BASE_MONTH 4U
#define BASE_DAY 2U

typedef enum {
    CLASSIFY_BY_SPECIES = 0,
    CLASSIFY_BY_PROTECTION = 1,
    CLASSIFY_BY_REGION = 2
} classify_mode_t;

typedef struct {
    wildlife_record_t records[WILDLIFE_MAX_RECORDS];
    uint16_t record_count;
    int query_indices[WILDLIFE_MAX_RECORDS];
    uint16_t query_count;
    uint32_t export_seq;
    int edit_index;
    uint8_t storage_warning;

    screen_ctx_t screens;

    lv_obj_t *class_mode_dd;
    lv_obj_t *class_value_dd;
    lv_obj_t *name_filter_ta;
    lv_obj_t *region_keyword_ta;
    lv_obj_t *level_filter_dd;
    lv_obj_t *region_filter_dd;
    lv_obj_t *result_list;
    lv_obj_t *stats_label;
    lv_obj_t *main_hint_label;
    lv_obj_t *main_kb;

    lv_obj_t *detail_title_label;
    lv_obj_t *detail_name_ta;
    lv_obj_t *detail_species_ta;
    lv_obj_t *detail_image_ta;
    lv_obj_t *detail_level_dd;
    lv_obj_t *detail_region_ta;
    lv_obj_t *detail_quantity_sb;
    lv_obj_t *detail_status_dd;
    lv_obj_t *detail_updated_label;
    lv_obj_t *detail_image_info_label;
    lv_obj_t *detail_hint_label;
    lv_obj_t *detail_delete_btn;
    lv_obj_t *detail_kb;
} wildlife_app_t;

static wildlife_app_t g_app;

static void on_list_item_clicked(lv_event_t *e);
static void ensure_keyboard_created(lv_obj_t **kb_ref, lv_obj_t *screen);

static const char *k_status_values[] = {
    "Stable",
    "Endangered",
    "Critical",
    "Recovering",
    "Vulnerable",
    "Unknown"
};

static const wildlife_record_t k_default_records[] = {
    {"Giant Panda", "Mammal", "/img/panda.bin", PROTECTION_LEVEL_NATIONAL_I, "Sichuan-Shaanxi-Gansu", 1864, "Recovering", "seed"},
    {"South China Tiger", "Mammal", "/img/south_china_tiger.bin", PROTECTION_LEVEL_NATIONAL_I, "South China Mountains", 20, "Critical", "seed"},
    {"Amur Tiger", "Mammal", "/img/amur_tiger.bin", PROTECTION_LEVEL_NATIONAL_I, "Northeast Forest", 70, "Endangered", "seed"},
    {"Snow Leopard", "Mammal", "/img/snow_leopard.bin", PROTECTION_LEVEL_NATIONAL_I, "Qinghai-Tibet Plateau", 1200, "Vulnerable", "seed"},
    {"Golden Snub-Nosed Monkey", "Mammal", "/img/snub_nosed_monkey.bin", PROTECTION_LEVEL_NATIONAL_I, "Qinling-Southwest Mountains", 4000, "Vulnerable", "seed"},
    {"Asian Elephant", "Mammal", "/img/asian_elephant.bin", PROTECTION_LEVEL_NATIONAL_I, "South Yunnan", 300, "Endangered", "seed"},
    {"Tibetan Antelope", "Mammal", "/img/tibetan_antelope.bin", PROTECTION_LEVEL_NATIONAL_I, "Qinghai-Tibet Plateau", 70000, "Recovering", "seed"},
    {"Crested Ibis", "Bird", "/img/crested_ibis.bin", PROTECTION_LEVEL_NATIONAL_I, "Hanzhong-Shaanxi", 7000, "Recovering", "seed"},
    {"Red-crowned Crane", "Bird", "/img/red_crowned_crane.bin", PROTECTION_LEVEL_NATIONAL_I, "Northeast Wetlands", 2000, "Vulnerable", "seed"},
    {"Chinese Alligator", "Reptile", "/img/chinese_alligator.bin", PROTECTION_LEVEL_NATIONAL_I, "Xuancheng-Anhui", 1200, "Endangered", "seed"},
    {"Black-necked Crane", "Bird", "/img/black_necked_crane.bin", PROTECTION_LEVEL_NATIONAL_I, "Plateau Wetlands", 17000, "Stable", "seed"},
    {"Saker Falcon", "Bird", "/img/saker_falcon.bin", PROTECTION_LEVEL_NATIONAL_II, "Northwest-North China", 5000, "Stable", "seed"},
    {"Macaque", "Mammal", "/img/macaque.bin", PROTECTION_LEVEL_NONE, "South China Hills", 200000, "Stable", "seed"},
    {"Red Fox", "Mammal", "/img/red_fox.bin", PROTECTION_LEVEL_NONE, "Northern Grassland-Forest", 80000, "Stable", "seed"},
    {"Mallard", "Bird", "/img/mallard.bin", PROTECTION_LEVEL_NONE, "Nationwide Wetlands", 150000, "Stable", "seed"}
};

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} datetime_t;

static void safe_copy(char *dst, uint32_t dst_size, const char *src)
{
    if(dst == NULL || dst_size == 0U) {
        return;
    }

    if(src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void trim_line_ending(char *text)
{
    uint32_t len;
    if(text == NULL) {
        return;
    }

    len = (uint32_t)strlen(text);
    while(len > 0U) {
        char c = text[len - 1U];
        if(c == '\r' || c == '\n') {
            text[len - 1U] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static void sanitize_csv_field(char *text)
{
    uint32_t i;
    if(text == NULL) {
        return;
    }

    for(i = 0U; text[i] != '\0'; i++) {
        if(text[i] == ',' || text[i] == '\r' || text[i] == '\n') {
            text[i] = ';';
        }
    }
}

static void trim_spaces_inplace(char *text)
{
    char *start;
    char *end;

    if(text == NULL) {
        return;
    }

    start = text;
    while(*start != '\0' && isspace((unsigned char)*start) != 0) {
        start++;
    }

    if(start != text) {
        memmove(text, start, strlen(start) + 1U);
    }

    end = text + strlen(text);
    while(end > text && isspace((unsigned char)*(end - 1)) != 0) {
        end--;
    }
    *end = '\0';
}

static void copy_normalized_text(char *dst, uint32_t dst_size, const char *src, const char *fallback)
{
    if(dst == NULL || dst_size == 0U) {
        return;
    }

    safe_copy(dst, dst_size, src);
    trim_spaces_inplace(dst);
    sanitize_csv_field(dst);

    if(dst[0] == '\0' && fallback != NULL) {
        safe_copy(dst, dst_size, fallback);
        trim_spaces_inplace(dst);
        sanitize_csv_field(dst);
    }
}

static int text_equals_ignore_case(const char *left, const char *right)
{
    uint32_t i = 0U;

    if(left == NULL || right == NULL) {
        return 0;
    }

    while(left[i] != '\0' && right[i] != '\0') {
        char c1 = (char)tolower((unsigned char)left[i]);
        char c2 = (char)tolower((unsigned char)right[i]);
        if(c1 != c2) {
            return 0;
        }
        i++;
    }

    return (left[i] == '\0' && right[i] == '\0') ? 1 : 0;
}

static int text_contains_ignore_case(const char *text, const char *pattern)
{
    uint32_t i;
    uint32_t j;

    if(text == NULL || pattern == NULL) {
        return 0;
    }

    if(pattern[0] == '\0') {
        return 1;
    }

    for(i = 0U; text[i] != '\0'; i++) {
        j = 0U;
        while(text[i + j] != '\0' && pattern[j] != '\0') {
            char c1 = (char)tolower((unsigned char)text[i + j]);
            char c2 = (char)tolower((unsigned char)pattern[j]);
            if(c1 != c2) {
                break;
            }
            j++;
        }

        if(pattern[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

static int find_duplicate_record(const char *name, const char *region, int skip_index)
{
    uint16_t i;

    if(name == NULL || region == NULL) {
        return -1;
    }

    for(i = 0U; i < g_app.record_count; i++) {
        if((int)i == skip_index) {
            continue;
        }

          if(text_equals_ignore_case(g_app.records[i].name, name) != 0 &&
              text_equals_ignore_case(g_app.records[i].region, region) != 0) {
            return (int)i;
        }
    }

    return -1;
}

static const char *level_to_text(protection_level_t level)
{
    switch(level) {
    case PROTECTION_LEVEL_NATIONAL_I:
        return "National I";
    case PROTECTION_LEVEL_NATIONAL_II:
        return "National II";
    case PROTECTION_LEVEL_NONE:
    default:
        return "None";
    }
}

static protection_level_t detail_level_from_selected(uint16_t sel)
{
    if(sel == 0U) {
        return PROTECTION_LEVEL_NATIONAL_I;
    }
    if(sel == 1U) {
        return PROTECTION_LEVEL_NATIONAL_II;
    }
    return PROTECTION_LEVEL_NONE;
}

static uint16_t detail_level_to_selected(protection_level_t level)
{
    if(level == PROTECTION_LEVEL_NATIONAL_I) {
        return 0U;
    }
    if(level == PROTECTION_LEVEL_NATIONAL_II) {
        return 1U;
    }
    return 2U;
}

static const char *status_from_selected(uint16_t sel)
{
    if(sel >= (sizeof(k_status_values) / sizeof(k_status_values[0]))) {
        return "Unknown";
    }
    return k_status_values[sel];
}

static uint16_t status_to_selected(const char *status)
{
    uint16_t i;
    if(status == NULL) {
        return 5U;
    }

    for(i = 0U; i < (sizeof(k_status_values) / sizeof(k_status_values[0])); i++) {
        if(text_equals_ignore_case(status, k_status_values[i]) != 0) {
            return i;
        }
    }

    return 5U;
}

static protection_level_t parse_level_field(const char *text)
{
    char normalized[24];

    if(text == NULL) {
        return PROTECTION_LEVEL_NONE;
    }

    copy_normalized_text(normalized, sizeof(normalized), text, "");

    if(strcmp(normalized, "2") == 0 ||
       text_equals_ignore_case(normalized, "National I") != 0 ||
       text_equals_ignore_case(normalized, "Level I") != 0 ||
       text_equals_ignore_case(normalized, "China I") != 0) {
        return PROTECTION_LEVEL_NATIONAL_I;
    }

    if(strcmp(normalized, "1") == 0 ||
       text_equals_ignore_case(normalized, "National II") != 0 ||
       text_equals_ignore_case(normalized, "Level II") != 0 ||
       text_equals_ignore_case(normalized, "China II") != 0) {
        return PROTECTION_LEVEL_NATIONAL_II;
    }

    return PROTECTION_LEVEL_NONE;
}

static uint8_t is_leap_year(uint16_t year)
{
    if((year % 400U) == 0U) {
        return 1U;
    }
    if((year % 100U) == 0U) {
        return 0U;
    }
    return ((year % 4U) == 0U) ? 1U : 0U;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t k_days_table[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    uint8_t days;

    if(month < 1U || month > 12U) {
        return 30U;
    }

    days = k_days_table[month - 1U];
    if(month == 2U && is_leap_year(year) != 0U) {
        days = 29U;
    }

    return days;
}

static void get_runtime_datetime(datetime_t *dt)
{
    uint32_t sec;
    uint32_t day_offset;

    if(dt == NULL) {
        return;
    }

    sec = lv_tick_get() / 1000U;
    day_offset = sec / 86400U;

    dt->year = BASE_YEAR;
    dt->month = (uint8_t)BASE_MONTH;
    dt->day = (uint8_t)BASE_DAY;

    while(day_offset > 0U) {
        uint8_t dim = days_in_month(dt->year, dt->month);
        if(dt->day < dim) {
            dt->day++;
            day_offset--;
        } else {
            dt->day = 1U;
            if(dt->month < 12U) {
                dt->month++;
            } else {
                dt->month = 1U;
                dt->year++;
            }
            day_offset--;
        }
    }

    dt->hour = (uint8_t)((sec % 86400U) / 3600U);
    dt->minute = (uint8_t)((sec % 3600U) / 60U);
    dt->second = (uint8_t)(sec % 60U);
}

static void make_update_time(char *buf, uint32_t len)
{
    datetime_t dt;

    if(buf == NULL || len == 0U) {
        return;
    }

    get_runtime_datetime(&dt);

    snprintf(buf,
             len,
             "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned int)dt.year,
             (unsigned int)dt.month,
             (unsigned int)dt.day,
             (unsigned int)dt.hour,
             (unsigned int)dt.minute,
             (unsigned int)dt.second);
}

static void set_main_hint(const char *text)
{
    if(g_app.main_hint_label != NULL) {
        lv_label_set_text(g_app.main_hint_label, text);
    }
}

static void set_detail_hint(const char *text)
{
    if(g_app.detail_hint_label != NULL) {
        lv_label_set_text(g_app.detail_hint_label, text);
    }
}

static void print_stats_report(void)
{
    uint16_t i;
    uint16_t lv1 = 0U;
    uint16_t lv2 = 0U;
    uint16_t none = 0U;

    for(i = 0U; i < g_app.record_count; i++) {
        if(g_app.records[i].level == PROTECTION_LEVEL_NATIONAL_I) {
            lv1++;
        } else if(g_app.records[i].level == PROTECTION_LEVEL_NATIONAL_II) {
            lv2++;
        } else {
            none++;
        }
    }

    printf("\r\n========== Wildlife Statistics Report ==========\r\n");
    printf("Total records            : %u\r\n", g_app.record_count);
    printf("National I animals       : %u\r\n", lv1);
    printf("National II animals      : %u\r\n", lv2);
    printf("Unprotected animals      : %u\r\n", none);
    printf("Current query result set : %u\r\n", g_app.query_count);
    printf("===============================================\r\n");
}

static void ensure_data_dirs(void)
{
    FRESULT fr;

    fr = f_mkdir(DATA_DIR);
    if(fr != FR_OK && fr != FR_EXIST) {
        return;
    }

    fr = f_mkdir(EXPORT_DIR);
    if(fr != FR_OK && fr != FR_EXIST) {
        return;
    }
}

static int parse_csv_line(char *line, wildlife_record_t *record)
{
    char *fields[8];
    uint16_t field_count = 0U;
    char *p;

    if(line == NULL || record == NULL) {
        return 0;
    }

    trim_line_ending(line);
    fields[field_count++] = line;
    for(p = line; *p != '\0' && field_count < 8U; p++) {
        if(*p == ',') {
            *p = '\0';
            fields[field_count++] = p + 1;
        }
    }

    if(field_count < 8U) {
        return 0;
    }

    copy_normalized_text(record->name, sizeof(record->name), fields[0], "");
    copy_normalized_text(record->species, sizeof(record->species), fields[1], "Unknown");
    copy_normalized_text(record->image_path, sizeof(record->image_path), fields[2], "/img/placeholder.bin");
    record->level = parse_level_field(fields[3]);
    copy_normalized_text(record->region, sizeof(record->region), fields[4], "Unknown");
    record->quantity = atoi(fields[5]);
    if(record->quantity < 0) {
        record->quantity = 0;
    }
    copy_normalized_text(record->status, sizeof(record->status), fields[6], "Unknown");
    copy_normalized_text(record->updated_at, sizeof(record->updated_at), fields[7], "seed");

    if(record->name[0] == '\0') {
        return 0;
    }

    return 1;
}

static int load_records_from_file(void)
{
    FIL file;
    FRESULT fr;
    char line[CSV_LINE_BUF_SIZE];
    int header_checked = 0;

    g_app.record_count = 0U;

    fr = f_open(&file, DATA_FILE, FA_READ);
    if(fr != FR_OK) {
        return -1;
    }

    while(f_gets(line, sizeof(line), &file) != NULL) {
        if(header_checked == 0) {
            header_checked = 1;
            if(strncmp(line, "name,species,image,level,region,quantity,status,updated_at", 57U) == 0) {
                continue;
            }
        }

        if(g_app.record_count >= WILDLIFE_MAX_RECORDS) {
            break;
        }

        if(parse_csv_line(line, &g_app.records[g_app.record_count]) != 0) {
            g_app.record_count++;
        }
    }

    f_close(&file);
    return (int)g_app.record_count;
}

static int save_records_to_file(void)
{
    FIL file;
    FRESULT fr;
    uint16_t i;

    ensure_data_dirs();

    fr = f_open(&file, DATA_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr != FR_OK) {
        return -1;
    }

    if(f_printf(&file, "name,species,image,level,region,quantity,status,updated_at\n") < 0) {
        f_close(&file);
        return -1;
    }

    for(i = 0U; i < g_app.record_count; i++) {
        char name[sizeof(g_app.records[i].name)];
        char species[sizeof(g_app.records[i].species)];
        char image_path[sizeof(g_app.records[i].image_path)];
        char region[sizeof(g_app.records[i].region)];
        char status[sizeof(g_app.records[i].status)];
        char updated_at[sizeof(g_app.records[i].updated_at)];

        safe_copy(name, sizeof(name), g_app.records[i].name);
        safe_copy(species, sizeof(species), g_app.records[i].species);
        safe_copy(image_path, sizeof(image_path), g_app.records[i].image_path);
        safe_copy(region, sizeof(region), g_app.records[i].region);
        safe_copy(status, sizeof(status), g_app.records[i].status);
        safe_copy(updated_at, sizeof(updated_at), g_app.records[i].updated_at);

        sanitize_csv_field(name);
        sanitize_csv_field(species);
        sanitize_csv_field(image_path);
        sanitize_csv_field(region);
        sanitize_csv_field(status);
        sanitize_csv_field(updated_at);

        if(f_printf(&file,
                    "%s,%s,%s,%d,%s,%d,%s,%s\n",
                    name,
                    species,
                    image_path,
                    (int)g_app.records[i].level,
                    region,
                    g_app.records[i].quantity,
                    status,
                    updated_at) < 0) {
            f_close(&file);
            return -1;
        }
    }

    f_close(&file);
    return 0;
}

static int export_query_results(char *out_path, uint32_t out_path_size)
{
    FIL file;
    FRESULT fr;
    uint16_t i;
    char path[96];
    datetime_t dt;

    ensure_data_dirs();

    get_runtime_datetime(&dt);

    snprintf(path,
             sizeof(path),
             "0:/export/query_%04u%02u%02u_%02u%02u%02u_%03lu.csv",
             (unsigned int)dt.year,
             (unsigned int)dt.month,
             (unsigned int)dt.day,
             (unsigned int)dt.hour,
             (unsigned int)dt.minute,
             (unsigned int)dt.second,
             (unsigned long)g_app.export_seq++);

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr != FR_OK) {
        return -1;
    }

    if(f_printf(&file, "name,species,image,level,region,quantity,status,updated_at\n") < 0) {
        f_close(&file);
        return -1;
    }

    for(i = 0U; i < g_app.query_count; i++) {
        wildlife_record_t *record;
        int idx = g_app.query_indices[i];
        if(idx < 0 || idx >= (int)g_app.record_count) {
            continue;
        }
        record = &g_app.records[idx];

        {
            char name[sizeof(record->name)];
            char species[sizeof(record->species)];
            char image_path[sizeof(record->image_path)];
            char region[sizeof(record->region)];
            char status[sizeof(record->status)];
            char updated_at[sizeof(record->updated_at)];

            safe_copy(name, sizeof(name), record->name);
            safe_copy(species, sizeof(species), record->species);
            safe_copy(image_path, sizeof(image_path), record->image_path);
            safe_copy(region, sizeof(region), record->region);
            safe_copy(status, sizeof(status), record->status);
            safe_copy(updated_at, sizeof(updated_at), record->updated_at);

            sanitize_csv_field(name);
            sanitize_csv_field(species);
            sanitize_csv_field(image_path);
            sanitize_csv_field(region);
            sanitize_csv_field(status);
            sanitize_csv_field(updated_at);

            if(f_printf(&file,
                        "%s,%s,%s,%d,%s,%d,%s,%s\n",
                        name,
                        species,
                        image_path,
                        (int)record->level,
                        region,
                        record->quantity,
                        status,
                        updated_at) < 0) {
                f_close(&file);
                return -1;
            }
        }
    }

    f_close(&file);
    safe_copy(out_path, out_path_size, path);
    return 0;
}

static void load_default_records(void)
{
    uint16_t i;
    uint16_t count = (uint16_t)(sizeof(k_default_records) / sizeof(k_default_records[0]));

    if(count > WILDLIFE_MAX_RECORDS) {
        count = WILDLIFE_MAX_RECORDS;
    }

    g_app.record_count = count;
    for(i = 0U; i < count; i++) {
        g_app.records[i] = k_default_records[i];
    }
}

static void append_dropdown_option(char *options, uint32_t options_size, const char *text)
{
    uint32_t used;
    if(options == NULL || text == NULL || options_size == 0U) {
        return;
    }

    used = (uint32_t)strlen(options);
    if(used + 1U >= options_size) {
        return;
    }

    if(used > 0U) {
        options[used++] = '\n';
        options[used] = '\0';
    }

    snprintf(options + used, options_size - used, "%s", text);
}

static void rebuild_region_filter_options(void)
{
    uint16_t i;
    uint16_t j;
    char options[1024];

    options[0] = '\0';
    append_dropdown_option(options, sizeof(options), "All");

    for(i = 0U; i < g_app.record_count; i++) {
        uint8_t duplicate = 0U;
        for(j = 0U; j < i; j++) {
            if(text_equals_ignore_case(g_app.records[i].region, g_app.records[j].region) != 0) {
                duplicate = 1U;
                break;
            }
        }
        if(duplicate == 0U) {
            append_dropdown_option(options, sizeof(options), g_app.records[i].region);
        }
    }

    lv_dropdown_set_options(g_app.region_filter_dd, options);
    lv_dropdown_set_selected(g_app.region_filter_dd, 0U);
}

static void rebuild_class_value_options(void)
{
    uint16_t i;
    uint16_t j;
    uint16_t mode;
    char options[1024];

    options[0] = '\0';
    append_dropdown_option(options, sizeof(options), "All");

    mode = lv_dropdown_get_selected(g_app.class_mode_dd);
    if(mode == CLASSIFY_BY_SPECIES) {
        for(i = 0U; i < g_app.record_count; i++) {
            uint8_t duplicate = 0U;
            for(j = 0U; j < i; j++) {
                if(text_equals_ignore_case(g_app.records[i].species, g_app.records[j].species) != 0) {
                    duplicate = 1U;
                    break;
                }
            }
            if(duplicate == 0U) {
                append_dropdown_option(options, sizeof(options), g_app.records[i].species);
            }
        }
    } else if(mode == CLASSIFY_BY_PROTECTION) {
        append_dropdown_option(options, sizeof(options), "National I");
        append_dropdown_option(options, sizeof(options), "National II");
        append_dropdown_option(options, sizeof(options), "None");
    } else {
        for(i = 0U; i < g_app.record_count; i++) {
            uint8_t duplicate = 0U;
            for(j = 0U; j < i; j++) {
                if(text_equals_ignore_case(g_app.records[i].region, g_app.records[j].region) != 0) {
                    duplicate = 1U;
                    break;
                }
            }
            if(duplicate == 0U) {
                append_dropdown_option(options, sizeof(options), g_app.records[i].region);
            }
        }
    }

    lv_dropdown_set_options(g_app.class_value_dd, options);
    lv_dropdown_set_selected(g_app.class_value_dd, 0U);
}

static int record_matches_filters(const wildlife_record_t *record)
{
    uint16_t level_sel;
    uint16_t mode;
    char name_filter[64];
    char region_keyword[64];
    char class_value[64];
    char region_filter[64];
    const char *name_filter_src;
    const char *region_keyword_src;

    if(record == NULL) {
        return 0;
    }

    name_filter_src = lv_textarea_get_text(g_app.name_filter_ta);
    copy_normalized_text(name_filter, sizeof(name_filter), name_filter_src, "");
    if(name_filter[0] != '\0') {
        if(text_contains_ignore_case(record->name, name_filter) == 0) {
            return 0;
        }
    }

    region_keyword_src = lv_textarea_get_text(g_app.region_keyword_ta);
    copy_normalized_text(region_keyword, sizeof(region_keyword), region_keyword_src, "");
    if(region_keyword[0] != '\0') {
        if(text_contains_ignore_case(record->region, region_keyword) == 0) {
            return 0;
        }
    }

    level_sel = lv_dropdown_get_selected(g_app.level_filter_dd);
    if(level_sel == 1U && record->level != PROTECTION_LEVEL_NATIONAL_I) {
        return 0;
    }
    if(level_sel == 2U && record->level != PROTECTION_LEVEL_NATIONAL_II) {
        return 0;
    }
    if(level_sel == 3U && record->level != PROTECTION_LEVEL_NONE) {
        return 0;
    }

    lv_dropdown_get_selected_str(g_app.region_filter_dd, region_filter, sizeof(region_filter));
    if(strcmp(region_filter, "All") != 0) {
        if(text_equals_ignore_case(record->region, region_filter) == 0) {
            return 0;
        }
    }

    mode = lv_dropdown_get_selected(g_app.class_mode_dd);
    lv_dropdown_get_selected_str(g_app.class_value_dd, class_value, sizeof(class_value));
    if(strcmp(class_value, "All") != 0) {
        if(mode == CLASSIFY_BY_SPECIES) {
            if(text_equals_ignore_case(record->species, class_value) == 0) {
                return 0;
            }
        } else if(mode == CLASSIFY_BY_PROTECTION) {
            if(text_equals_ignore_case(level_to_text(record->level), class_value) == 0) {
                return 0;
            }
        } else {
            if(text_equals_ignore_case(record->region, class_value) == 0) {
                return 0;
            }
        }
    }

    return 1;
}

static void refresh_stats(void)
{
    uint16_t i;
    uint16_t lv1 = 0U;
    uint16_t lv2 = 0U;
    uint16_t none = 0U;

    for(i = 0U; i < g_app.record_count; i++) {
        if(g_app.records[i].level == PROTECTION_LEVEL_NATIONAL_I) {
            lv1++;
        } else if(g_app.records[i].level == PROTECTION_LEVEL_NATIONAL_II) {
            lv2++;
        } else {
            none++;
        }
    }

    lv_label_set_text_fmt(g_app.stats_label,
                          "Total: %u   National I: %u   National II: %u   None: %u   Query: %u",
                          g_app.record_count,
                          lv1,
                          lv2,
                          none,
                          g_app.query_count);
}

static void refresh_result_list(void)
{
    uint16_t i;

    lv_obj_clean(g_app.result_list);
    if(g_app.query_count == 0U) {
        lv_list_add_text(g_app.result_list, "No records matched current filters.");
        return;
    }

    for(i = 0U; i < g_app.query_count; i++) {
        char line[192];
        int idx = g_app.query_indices[i];
        wildlife_record_t *record;
        lv_obj_t *btn;

        if(idx < 0 || idx >= (int)g_app.record_count) {
            continue;
        }

        record = &g_app.records[idx];
        snprintf(line,
                 sizeof(line),
                 "%s | %s | %s | Qty:%d",
                 record->name,
                 level_to_text(record->level),
                 record->region,
                 record->quantity);

        btn = lv_list_add_btn(g_app.result_list, NULL, line);
        lv_obj_add_event_cb(btn, on_list_item_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    }
}

static void apply_query(void)
{
    uint16_t i;
    g_app.query_count = 0U;

    for(i = 0U; i < g_app.record_count; i++) {
        if(record_matches_filters(&g_app.records[i]) != 0) {
            if(g_app.query_count < WILDLIFE_MAX_RECORDS) {
                g_app.query_indices[g_app.query_count++] = (int)i;
            }
        }
    }

    refresh_result_list();
    refresh_stats();
    print_stats_report();
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, NULL);
    }
}

static void ensure_keyboard_created(lv_obj_t **kb_ref, lv_obj_t *screen)
{
    lv_coord_t screen_w;
    lv_coord_t screen_h;

    if(kb_ref == NULL || screen == NULL || *kb_ref != NULL) {
        return;
    }

    *kb_ref = lv_keyboard_create(screen);
    if(*kb_ref == NULL) {
        return;
    }

    screen_w = lv_obj_get_width(screen);
    screen_h = lv_obj_get_height(screen);

    lv_obj_set_size(*kb_ref, screen_w, 220);
    lv_obj_set_pos(*kb_ref, 0, screen_h - 220);
    lv_obj_add_flag(*kb_ref, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(*kb_ref, keyboard_event_cb, LV_EVENT_ALL, NULL);
}

static void textarea_focus_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t **kb_ref = (lv_obj_t **)lv_event_get_user_data(e);
    lv_obj_t *screen = lv_obj_get_screen(ta);
    lv_obj_t *kb = NULL;

    if(kb_ref == NULL) {
        return;
    }

    ensure_keyboard_created(kb_ref, screen);
    kb = *kb_ref;
    if(kb == NULL) {
        return;
    }

    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_detail_screen(int index)
{
    wildlife_record_t *record;

    g_app.edit_index = index;

    if(index >= 0 && index < (int)g_app.record_count) {
        record = &g_app.records[index];

        lv_label_set_text(g_app.detail_title_label, "Animal Detail / Edit");
        lv_textarea_set_text(g_app.detail_name_ta, record->name);
        lv_textarea_set_text(g_app.detail_species_ta, record->species);
        lv_textarea_set_text(g_app.detail_image_ta, record->image_path);
        lv_dropdown_set_selected(g_app.detail_level_dd, detail_level_to_selected(record->level));
        lv_textarea_set_text(g_app.detail_region_ta, record->region);
        lv_spinbox_set_value(g_app.detail_quantity_sb, record->quantity);
        lv_dropdown_set_selected(g_app.detail_status_dd, status_to_selected(record->status));
        lv_label_set_text_fmt(g_app.detail_updated_label, "Updated At: %s", record->updated_at);
        lv_label_set_text_fmt(g_app.detail_image_info_label, "Image Info: %s", record->image_path);
        lv_obj_clear_flag(g_app.detail_delete_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_app.detail_title_label, "New Animal Record");
        lv_textarea_set_text(g_app.detail_name_ta, "");
        lv_textarea_set_text(g_app.detail_species_ta, "");
        lv_textarea_set_text(g_app.detail_image_ta, "/img/placeholder.bin");
        lv_dropdown_set_selected(g_app.detail_level_dd, 2U);
        lv_textarea_set_text(g_app.detail_region_ta, "");
        lv_spinbox_set_value(g_app.detail_quantity_sb, 1);
        lv_dropdown_set_selected(g_app.detail_status_dd, 5U);
        lv_label_set_text(g_app.detail_updated_label, "Updated At: (new record)");
        lv_label_set_text(g_app.detail_image_info_label, "Image Info: /img/placeholder.bin");
        lv_obj_add_flag(g_app.detail_delete_btn, LV_OBJ_FLAG_HIDDEN);
    }

    set_detail_hint("Edit fields and press Save.");
    if(g_app.detail_kb != NULL) {
        lv_obj_add_flag(g_app.detail_kb, LV_OBJ_FLAG_HIDDEN);
    }
    lv_scr_load(g_app.screens.display2);
}

static void on_list_item_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        int idx = (int)(uintptr_t)lv_event_get_user_data(e);
        open_detail_screen(idx);
    }
}

static void on_query_btn_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        apply_query();
        set_main_hint("Query completed.");
    }
}

static void on_reset_btn_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_textarea_set_text(g_app.name_filter_ta, "");
        lv_textarea_set_text(g_app.region_keyword_ta, "");
        lv_dropdown_set_selected(g_app.level_filter_dd, 0U);
        lv_dropdown_set_selected(g_app.region_filter_dd, 0U);
        lv_dropdown_set_selected(g_app.class_mode_dd, 0U);
        rebuild_class_value_options();
        apply_query();
        set_main_hint("Filters reset.");
    }
}

static void on_class_mode_changed(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        rebuild_class_value_options();
        apply_query();
        set_main_hint("Classification mode changed.");
    }
}

static void on_filter_value_changed(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        apply_query();
    }
}

static void on_text_filter_changed(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_READY) {
        apply_query();
    }
}

static void on_export_btn_clicked(lv_event_t *e)
{
    char path[96];
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if(export_query_results(path, sizeof(path)) == 0) {
            set_main_hint("Export completed.");
            printf("Exported query results to: %s\r\n", path);
        } else {
            set_main_hint("Export failed: check SD card status.");
        }
    }
}

static void on_new_btn_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        open_detail_screen(-1);
    }
}

static void on_detail_back_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if(g_app.detail_kb != NULL) {
            lv_obj_add_flag(g_app.detail_kb, LV_OBJ_FLAG_HIDDEN);
        }
        lv_scr_load(g_app.screens.display1);
        set_main_hint("Returned to main page.");
    }
}

static void on_detail_image_ta_changed(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(g_app.detail_image_info_label,
                              "Image Info: %s",
                              lv_textarea_get_text(g_app.detail_image_ta));
    }
}

static void on_quantity_inc_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *spinbox = (lv_obj_t *)lv_event_get_user_data(e);
        lv_spinbox_increment(spinbox);
    }
}

static void on_quantity_dec_clicked(lv_event_t *e)
{
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *spinbox = (lv_obj_t *)lv_event_get_user_data(e);
        lv_spinbox_decrement(spinbox);
    }
}

static void on_save_btn_clicked(lv_event_t *e)
{
    wildlife_record_t record;
    char normalized_name[sizeof(record.name)];
    char normalized_species[sizeof(record.species)];
    char normalized_image[sizeof(record.image_path)];
    char normalized_region[sizeof(record.region)];
    const char *name;
    const char *species;
    const char *image_path;
    const char *region;
    uint16_t level_sel;
    uint16_t status_sel;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    name = lv_textarea_get_text(g_app.detail_name_ta);
    species = lv_textarea_get_text(g_app.detail_species_ta);
    image_path = lv_textarea_get_text(g_app.detail_image_ta);
    region = lv_textarea_get_text(g_app.detail_region_ta);

    copy_normalized_text(normalized_name, sizeof(normalized_name), name, "");
    copy_normalized_text(normalized_species, sizeof(normalized_species), species, "Unknown");
    copy_normalized_text(normalized_image, sizeof(normalized_image), image_path, "/img/placeholder.bin");
    copy_normalized_text(normalized_region, sizeof(normalized_region), region, "Unknown");

    if(normalized_name[0] == '\0') {
        set_detail_hint("Name cannot be empty.");
        return;
    }

    safe_copy(record.name, sizeof(record.name), normalized_name);
    safe_copy(record.species, sizeof(record.species), normalized_species);
    safe_copy(record.image_path, sizeof(record.image_path), normalized_image);
    safe_copy(record.region, sizeof(record.region), normalized_region);

    if(find_duplicate_record(record.name, record.region, g_app.edit_index) >= 0) {
        set_detail_hint("Duplicate record: same name and region already exists.");
        return;
    }

    level_sel = lv_dropdown_get_selected(g_app.detail_level_dd);
    record.level = detail_level_from_selected(level_sel);

    record.quantity = lv_spinbox_get_value(g_app.detail_quantity_sb);
    if(record.quantity < 0) {
        record.quantity = 0;
    }

    status_sel = lv_dropdown_get_selected(g_app.detail_status_dd);
    safe_copy(record.status, sizeof(record.status), status_from_selected(status_sel));
    make_update_time(record.updated_at, sizeof(record.updated_at));

    if(g_app.edit_index >= 0 && g_app.edit_index < (int)g_app.record_count) {
        g_app.records[g_app.edit_index] = record;
    } else {
        if(g_app.record_count >= WILDLIFE_MAX_RECORDS) {
            set_detail_hint("Record capacity reached.");
            return;
        }
        g_app.records[g_app.record_count++] = record;
    }

    if(save_records_to_file() != 0) {
        set_detail_hint("Save failed: check SD card status.");
        return;
    }

    rebuild_region_filter_options();
    rebuild_class_value_options();
    apply_query();
    lv_scr_load(g_app.screens.display1);
    set_main_hint("Record saved successfully.");
}

static void on_delete_btn_clicked(lv_event_t *e)
{
    int i;

    if(lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if(g_app.edit_index < 0 || g_app.edit_index >= (int)g_app.record_count) {
        set_detail_hint("No record selected for deletion.");
        return;
    }

    for(i = g_app.edit_index; i < (int)g_app.record_count - 1; i++) {
        g_app.records[i] = g_app.records[i + 1];
    }
    g_app.record_count--;

    if(save_records_to_file() != 0) {
        set_detail_hint("Delete failed while writing file.");
        return;
    }

    g_app.edit_index = -1;
    rebuild_region_filter_options();
    rebuild_class_value_options();
    apply_query();
    lv_scr_load(g_app.screens.display1);
    set_main_hint("Record deleted (physical removal).");
}

static void create_button_with_text(lv_obj_t *parent,
                                    lv_obj_t **out_btn,
                                    const char *text,
                                    lv_coord_t x,
                                    lv_coord_t y,
                                    lv_coord_t w,
                                    lv_coord_t h)
{
    lv_obj_t *btn;
    lv_obj_t *label;

    btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);

    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if(out_btn != NULL) {
        *out_btn = btn;
    }
}

static void create_main_screen(void)
{
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *query_btn;
    lv_obj_t *reset_btn;
    lv_obj_t *export_btn;
    lv_obj_t *new_btn;
    lv_obj_t *label;

    g_app.screens.display1 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_app.screens.display1, lv_color_hex(0xEEF3F6), 0);

    title = lv_label_create(g_app.screens.display1);
    lv_label_set_text(title, "Wildlife Protection Archive System");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title, 16, 10);

    g_app.stats_label = lv_label_create(g_app.screens.display1);
    lv_label_set_text(g_app.stats_label, "Statistics loading...");
    lv_obj_set_pos(g_app.stats_label, 360, 15);

    panel = lv_obj_create(g_app.screens.display1);
    lv_obj_set_pos(panel, 10, 50);
    lv_obj_set_size(panel, 340, 540);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Classification");
    lv_obj_set_pos(label, 10, 10);

    g_app.class_mode_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.class_mode_dd, 10, 30);
    lv_obj_set_size(g_app.class_mode_dd, 155, 34);
    lv_dropdown_set_options(g_app.class_mode_dd, "By Species\nBy Protection\nBy Region");
    lv_obj_add_event_cb(g_app.class_mode_dd, on_class_mode_changed, LV_EVENT_VALUE_CHANGED, NULL);

    g_app.class_value_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.class_value_dd, 175, 30);
    lv_obj_set_size(g_app.class_value_dd, 155, 34);
    lv_dropdown_set_options(g_app.class_value_dd, "All");
    lv_obj_add_event_cb(g_app.class_value_dd, on_filter_value_changed, LV_EVENT_VALUE_CHANGED, NULL);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Name Query");
    lv_obj_set_pos(label, 10, 75);

    g_app.name_filter_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.name_filter_ta, 10, 95);
    lv_obj_set_size(g_app.name_filter_ta, 320, 36);
    lv_textarea_set_one_line(g_app.name_filter_ta, true);
    lv_textarea_set_placeholder_text(g_app.name_filter_ta, "Input animal name keyword");

    label = lv_label_create(panel);
    lv_label_set_text(label, "Protection Level Filter");
    lv_obj_set_pos(label, 10, 140);

    g_app.level_filter_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.level_filter_dd, 10, 160);
    lv_obj_set_size(g_app.level_filter_dd, 320, 34);
    lv_dropdown_set_options(g_app.level_filter_dd, "All\nNational I\nNational II\nNone");
    lv_obj_add_event_cb(g_app.level_filter_dd, on_filter_value_changed, LV_EVENT_VALUE_CHANGED, NULL);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Region Keyword");
    lv_obj_set_pos(label, 10, 205);

    g_app.region_keyword_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.region_keyword_ta, 10, 225);
    lv_obj_set_size(g_app.region_keyword_ta, 320, 36);
    lv_textarea_set_one_line(g_app.region_keyword_ta, true);
    lv_textarea_set_placeholder_text(g_app.region_keyword_ta, "Input region keyword (contains match)");

    label = lv_label_create(panel);
    lv_label_set_text(label, "Region Exact Filter");
    lv_obj_set_pos(label, 10, 270);

    g_app.region_filter_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.region_filter_dd, 10, 290);
    lv_obj_set_size(g_app.region_filter_dd, 320, 34);
    lv_dropdown_set_options(g_app.region_filter_dd, "All");
    lv_obj_add_event_cb(g_app.region_filter_dd, on_filter_value_changed, LV_EVENT_VALUE_CHANGED, NULL);

    create_button_with_text(panel, &query_btn, "Query", 10, 335, 74, 38);
    create_button_with_text(panel, &reset_btn, "Reset", 92, 335, 74, 38);
    create_button_with_text(panel, &export_btn, "Export", 174, 335, 74, 38);
    create_button_with_text(panel, &new_btn, "New", 256, 335, 74, 38);

    lv_obj_add_event_cb(query_btn, on_query_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(reset_btn, on_reset_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(export_btn, on_export_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(new_btn, on_new_btn_clicked, LV_EVENT_CLICKED, NULL);

    g_app.main_hint_label = lv_label_create(panel);
    lv_label_set_long_mode(g_app.main_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(g_app.main_hint_label, 320, 130);
    lv_obj_set_pos(g_app.main_hint_label, 10, 390);
    lv_label_set_text(g_app.main_hint_label, "System ready. Use name/region keyword and filters to query records.");

    g_app.result_list = lv_list_create(g_app.screens.display1);
    lv_obj_set_pos(g_app.result_list, 360, 50);
    lv_obj_set_size(g_app.result_list, 654, 510);

    g_app.main_kb = NULL;
    lv_obj_add_event_cb(g_app.name_filter_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.main_kb);
    lv_obj_add_event_cb(g_app.region_keyword_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.main_kb);
    lv_obj_add_event_cb(g_app.name_filter_ta, on_text_filter_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_app.name_filter_ta, on_text_filter_changed, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_app.region_keyword_ta, on_text_filter_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_app.region_keyword_ta, on_text_filter_changed, LV_EVENT_READY, NULL);
}

static void create_detail_screen(void)
{
    lv_obj_t *panel;
    lv_obj_t *back_btn;
    lv_obj_t *save_btn;
    lv_obj_t *delete_btn;
    lv_obj_t *label;
    lv_obj_t *qty_inc_btn;
    lv_obj_t *qty_dec_btn;

    g_app.screens.display2 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_app.screens.display2, lv_color_hex(0xF8F9EE), 0);

    g_app.detail_title_label = lv_label_create(g_app.screens.display2);
    lv_label_set_text(g_app.detail_title_label, "Animal Detail / Edit");
    lv_obj_set_style_text_font(g_app.detail_title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_app.detail_title_label, 16, 10);

    create_button_with_text(g_app.screens.display2, &back_btn, "Back", 760, 10, 74, 36);
    create_button_with_text(g_app.screens.display2, &save_btn, "Save", 842, 10, 74, 36);
    create_button_with_text(g_app.screens.display2, &delete_btn, "Delete", 924, 10, 90, 36);
    g_app.detail_delete_btn = delete_btn;

    lv_obj_add_event_cb(back_btn, on_detail_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(save_btn, on_save_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(delete_btn, on_delete_btn_clicked, LV_EVENT_CLICKED, NULL);

    panel = lv_obj_create(g_app.screens.display2);
    lv_obj_set_pos(panel, 10, 55);
    lv_obj_set_size(panel, 1004, 535);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Name");
    lv_obj_set_pos(label, 10, 10);
    g_app.detail_name_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.detail_name_ta, 10, 30);
    lv_obj_set_size(g_app.detail_name_ta, 430, 36);
    lv_textarea_set_one_line(g_app.detail_name_ta, true);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Species Category");
    lv_obj_set_pos(label, 10, 75);
    g_app.detail_species_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.detail_species_ta, 10, 95);
    lv_obj_set_size(g_app.detail_species_ta, 430, 36);
    lv_textarea_set_one_line(g_app.detail_species_ta, true);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Image Path");
    lv_obj_set_pos(label, 10, 140);
    g_app.detail_image_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.detail_image_ta, 10, 160);
    lv_obj_set_size(g_app.detail_image_ta, 430, 36);
    lv_textarea_set_one_line(g_app.detail_image_ta, true);
    lv_obj_add_event_cb(g_app.detail_image_ta, on_detail_image_ta_changed, LV_EVENT_VALUE_CHANGED, NULL);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Protection Level");
    lv_obj_set_pos(label, 10, 205);
    g_app.detail_level_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.detail_level_dd, 10, 225);
    lv_obj_set_size(g_app.detail_level_dd, 430, 34);
    lv_dropdown_set_options(g_app.detail_level_dd, "National I\nNational II\nNone");

    label = lv_label_create(panel);
    lv_label_set_text(label, "Region");
    lv_obj_set_pos(label, 10, 270);
    g_app.detail_region_ta = lv_textarea_create(panel);
    lv_obj_set_pos(g_app.detail_region_ta, 10, 290);
    lv_obj_set_size(g_app.detail_region_ta, 430, 36);
    lv_textarea_set_one_line(g_app.detail_region_ta, true);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Quantity Estimate");
    lv_obj_set_pos(label, 10, 335);
    g_app.detail_quantity_sb = lv_spinbox_create(panel);
    lv_obj_set_pos(g_app.detail_quantity_sb, 10, 355);
    lv_obj_set_size(g_app.detail_quantity_sb, 330, 40);
    lv_spinbox_set_range(g_app.detail_quantity_sb, 0, 999999);
    lv_spinbox_set_digit_format(g_app.detail_quantity_sb, 6, 0);
    lv_spinbox_set_value(g_app.detail_quantity_sb, 1);

    create_button_with_text(panel, &qty_inc_btn, "+", 350, 355, 40, 40);
    create_button_with_text(panel, &qty_dec_btn, "-", 400, 355, 40, 40);
    lv_obj_add_event_cb(qty_inc_btn, on_quantity_inc_clicked, LV_EVENT_CLICKED, g_app.detail_quantity_sb);
    lv_obj_add_event_cb(qty_dec_btn, on_quantity_dec_clicked, LV_EVENT_CLICKED, g_app.detail_quantity_sb);

    label = lv_label_create(panel);
    lv_label_set_text(label, "Living Status");
    lv_obj_set_pos(label, 10, 405);
    g_app.detail_status_dd = lv_dropdown_create(panel);
    lv_obj_set_pos(g_app.detail_status_dd, 10, 425);
    lv_obj_set_size(g_app.detail_status_dd, 430, 34);
    lv_dropdown_set_options(g_app.detail_status_dd, "Stable\nEndangered\nCritical\nRecovering\nVulnerable\nUnknown");

    g_app.detail_updated_label = lv_label_create(panel);
    lv_label_set_text(g_app.detail_updated_label, "Updated At: -");
    lv_obj_set_pos(g_app.detail_updated_label, 10, 470);

    g_app.detail_hint_label = lv_label_create(panel);
    lv_label_set_long_mode(g_app.detail_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(g_app.detail_hint_label, 430, 46);
    lv_obj_set_pos(g_app.detail_hint_label, 10, 490);
    lv_label_set_text(g_app.detail_hint_label, "Use Save to persist changes.");

    label = lv_label_create(panel);
    lv_label_set_text(label, "Detail Preview");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, 520, 20);

    g_app.detail_image_info_label = lv_label_create(panel);
    lv_label_set_long_mode(g_app.detail_image_info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(g_app.detail_image_info_label, 450, 120);
    lv_obj_set_pos(g_app.detail_image_info_label, 520, 60);
    lv_label_set_text(g_app.detail_image_info_label, "Image Info: /img/placeholder.bin");

    label = lv_label_create(panel);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label, 450, 280);
    lv_obj_set_pos(label, 520, 200);
    lv_label_set_text(label,
                      "Fields in this page correspond to one wildlife record:\n"
                      "1. Name\n"
                      "2. Species category\n"
                      "3. Image path (placeholder supported)\n"
                      "4. Protection level\n"
                      "5. Region\n"
                      "6. Quantity estimate\n"
                      "7. Living status\n"
                      "8. Updated timestamp");

    g_app.detail_kb = NULL;

    lv_obj_add_event_cb(g_app.detail_name_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.detail_kb);
    lv_obj_add_event_cb(g_app.detail_species_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.detail_kb);
    lv_obj_add_event_cb(g_app.detail_image_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.detail_kb);
    lv_obj_add_event_cb(g_app.detail_region_ta, textarea_focus_event_cb, LV_EVENT_FOCUSED, &g_app.detail_kb);
}

void wildlife_app_start(void)
{
    int loaded;

    memset(&g_app, 0, sizeof(g_app));
    g_app.edit_index = -1;

    loaded = load_records_from_file();
    if(loaded <= 0) {
        load_default_records();
        if(save_records_to_file() != 0) {
            g_app.storage_warning = 1U;
        }
    }

    create_main_screen();
    create_detail_screen();

    rebuild_region_filter_options();
    rebuild_class_value_options();
    apply_query();

    if(g_app.storage_warning != 0U) {
        set_main_hint("Storage unavailable, running with RAM data only.");
    }

    lv_scr_load(g_app.screens.display1);
}
