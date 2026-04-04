#include "Wildlife_Core.h"
#include "Wildlife_UI.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FATFS/ff.h"

#define WL_DB_FILE "0:/wildlife_db.csv"
#define WL_EXPORT_FILE "0:/wildlife_export.csv"

typedef struct {
    wildlife_record_t records[WL_MAX_RECORDS];
    uint16_t view_idx[WL_MAX_RECORDS];
    uint16_t view_count;
    wildlife_mode_t mode;
    char filter_selected[24];
    char name_key[24];
    char filter_options[256];
    wildlife_stats_t stats;
    char last_msg[128];
} wildlife_core_ctx_t;

static wildlife_core_ctx_t g_core;

static const wildlife_record_t g_default_records[] = {
    {"Panda", "Mammal", "0:/img/panda.bin", "ClassI", "Sichuan", 1864, "recovering", WL_DEFAULT_UPDATED_AT, 1},
    {"Tiger", "Mammal", "0:/img/tiger.bin", "ClassI", "Northeast", 65, "critical", WL_DEFAULT_UPDATED_AT, 1},
    {"AsianElephant", "Mammal", "0:/img/elephant.bin", "ClassI", "Yunnan", 300, "recovering", WL_DEFAULT_UPDATED_AT, 1},
    {"ChineseAlligator", "Reptile", "0:/img/alligator.bin", "ClassI", "Anhui", 300, "recovering", WL_DEFAULT_UPDATED_AT, 1},
    {"GreenPeafowl", "Bird", "0:/img/peafowl.bin", "ClassI", "Yunnan", 550, "critical", WL_DEFAULT_UPDATED_AT, 1},
    {"RedCrownedCrane", "Bird", "0:/img/crane.bin", "ClassI", "Heilongjiang", 2600, "stable", WL_DEFAULT_UPDATED_AT, 1},
    {"YangtzeFinlessPorpoise", "Mammal", "0:/img/porpoise.bin", "ClassI", "Yangtze", 1249, "recovering", WL_DEFAULT_UPDATED_AT, 1},
    {"TibetanAntelope", "Mammal", "0:/img/antelope.bin", "ClassI", "Tibet", 300000, "stable", WL_DEFAULT_UPDATED_AT, 1},
    {"HainanGibbon", "Mammal", "0:/img/gibbon.bin", "ClassI", "Hainan", 42, "critical", WL_DEFAULT_UPDATED_AT, 1},
    {"SnowLeopard", "Mammal", "0:/img/snow_leopard.bin", "ClassI", "Qinghai", 1200, "stable", WL_DEFAULT_UPDATED_AT, 1},
    {"GiantSalamander", "Amphibian", "0:/img/salamander.bin", "ClassII", "Shaanxi", 800, "decreasing", WL_DEFAULT_UPDATED_AT, 1},
    {"SeaTurtle", "Reptile", "0:/img/turtle.bin", "ClassI", "Hainan", 2400, "stable", WL_DEFAULT_UPDATED_AT, 1},
    {"EagleOwl", "Bird", "0:/img/owl.bin", "ClassII", "Xinjiang", 3000, "stable", WL_DEFAULT_UPDATED_AT, 1},
    {"HorseshoeBat", "Mammal", "0:/img/bat.bin", "void", "Guangxi", 12000, "decreasing", WL_DEFAULT_UPDATED_AT, 1},
    {"CrestedIbis", "Bird", "0:/img/ibis.bin", "ClassI", "Shaanxi", 7000, "recovering", WL_DEFAULT_UPDATED_AT, 1}
};

static void wl_set_msg(const char *msg)
{
    if(msg == NULL) {
        g_core.last_msg[0] = '\0';
        return;
    }
    strncpy(g_core.last_msg, msg, sizeof(g_core.last_msg) - 1U);
    g_core.last_msg[sizeof(g_core.last_msg) - 1U] = '\0';
}

static void wl_copy(char *dst, uint32_t dst_size, const char *src)
{
    if(dst == NULL || src == NULL || dst_size == 0U) return;
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static int wl_contains_nocase(const char *text, const char *key)
{
    uint32_t i;
    uint32_t j;
    uint32_t tlen;
    uint32_t klen;

    if(text == NULL || key == NULL) return 0;

    tlen = (uint32_t)strlen(text);
    klen = (uint32_t)strlen(key);
    if(klen == 0U) return 1;
    if(tlen < klen) return 0;

    for(i = 0; i <= tlen - klen; i++) {
        for(j = 0; j < klen; j++) {
            char a = text[i + j];
            char b = key[j];
            if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if(a != b) break;
        }
        if(j == klen) return 1;
    }

    return 0;
}

static const char *wl_field_by_mode(const wildlife_record_t *rec, wildlife_mode_t mode)
{
    if(mode == WL_MODE_BY_LEVEL) return rec->level;
    if(mode == WL_MODE_BY_AREA) return rec->area;
    return rec->category;
}

static uint16_t wl_status_bucket(const char *status)
{
    if(strcmp(status, "critical") == 0) return 0U;
    if(strcmp(status, "recovering") == 0) return 1U;
    if(strcmp(status, "decreasing") == 0) return 2U;
    if(strcmp(status, "stable") == 0) return 3U;
    return 4U;
}

static int16_t wl_find_free_slot(void)
{
    uint16_t i;
    for(i = 0; i < WL_MAX_RECORDS; i++) {
        if(!g_core.records[i].valid) return (int16_t)i;
    }
    return -1;
}

static void wl_reset_defaults(void)
{
    uint32_t i;
    memset(g_core.records, 0, sizeof(g_core.records));
    for(i = 0; i < (uint32_t)(sizeof(g_default_records) / sizeof(g_default_records[0])) && i < WL_MAX_RECORDS; i++) {
        g_core.records[i] = g_default_records[i];
        g_core.records[i].valid = 1U;
    }
}

static void wl_rebuild_filter_options(void)
{
    uint16_t i;
    wl_copy(g_core.filter_options, sizeof(g_core.filter_options), "ALL");

    for(i = 0; i < WL_MAX_RECORDS; i++) {
        uint16_t j;
        int exists = 0;
        const char *field;

        if(!g_core.records[i].valid) continue;
        field = wl_field_by_mode(&g_core.records[i], g_core.mode);

        for(j = 0; j < i; j++) {
            if(g_core.records[j].valid && strcmp(field, wl_field_by_mode(&g_core.records[j], g_core.mode)) == 0) {
                exists = 1;
                break;
            }
        }

        if(!exists) {
            if((strlen(g_core.filter_options) + strlen(field) + 2U) < sizeof(g_core.filter_options)) {
                strcat(g_core.filter_options, "\n");
                strcat(g_core.filter_options, field);
            }
        }
    }

    if(strcmp(g_core.filter_selected, "ALL") != 0 && strstr(g_core.filter_options, g_core.filter_selected) == NULL) {
        wl_copy(g_core.filter_selected, sizeof(g_core.filter_selected), "ALL");
    }
}

static void wl_recompute_stats(void)
{
    uint16_t i;
    uint16_t cnt[WL_STATUS_BUCKETS] = {0};
    uint16_t pct[WL_STATUS_BUCKETS] = {0};
    uint16_t rem[WL_STATUS_BUCKETS] = {0};
    uint16_t sum_pct = 0U;
    uint16_t remain;

    memset(&g_core.stats, 0, sizeof(g_core.stats));

    for(i = 0; i < WL_MAX_RECORDS; i++) {
        if(g_core.records[i].valid) g_core.stats.valid_count++;
    }

    g_core.stats.filtered_count = g_core.view_count;

    for(i = 0; i < g_core.view_count; i++) {
        const wildlife_record_t *rec = &g_core.records[g_core.view_idx[i]];
        g_core.stats.total_quantity += rec->quantity;

        if(strcmp(rec->status, "critical") == 0 || strcmp(rec->status, "extinct") == 0) {
            g_core.stats.critical_count++;
        }
        if(strcmp(rec->status, "decreasing") == 0) {
            g_core.stats.decreasing_count++;
        }

        cnt[wl_status_bucket(rec->status)]++;
    }

    if(g_core.view_count > 0U) {
        for(i = 0; i < WL_STATUS_BUCKETS; i++) {
            uint32_t mul = (uint32_t)cnt[i] * 100U;
            pct[i] = (uint16_t)(mul / g_core.view_count);
            rem[i] = (uint16_t)(mul % g_core.view_count);
            sum_pct = (uint16_t)(sum_pct + pct[i]);
        }

        remain = (uint16_t)(100U - sum_pct);
        while(remain > 0U) {
            uint16_t best = 0U;
            for(i = 1; i < WL_STATUS_BUCKETS; i++) {
                if(rem[i] > rem[best]) best = i;
            }
            pct[best]++;
            rem[best] = 0U;
            remain--;
        }
    }

    for(i = 0; i < WL_STATUS_BUCKETS; i++) {
        g_core.stats.status_percent[i] = pct[i];
    }
}

static const char *wl_csv_read_field(const char *src, char *dst, uint32_t dst_size, uint8_t *ok)
{
    const char *p = src;
    uint32_t i = 0U;
    uint8_t quoted = 0U;

    if(ok != NULL) *ok = 0U;
    if(src == NULL || dst == NULL || dst_size == 0U) return src;

    if(*p == '"') {
        quoted = 1U;
        p++;
    }

    while(*p != '\0') {
        if(quoted) {
            if(*p == '"') {
                if(*(p + 1) == '"') {
                    if(i + 1U < dst_size) dst[i++] = '"';
                    p += 2;
                    continue;
                }
                p++;
                quoted = 0U;
                continue;
            }
        }
        else {
            if(*p == ',') {
                p++;
                break;
            }
            if(*p == '\r' || *p == '\n') break;
        }

        if(i + 1U < dst_size) dst[i++] = *p;
        p++;
    }

    dst[i] = '\0';
    if(ok != NULL) *ok = 1U;
    return p;
}

static int wl_parse_line(char *line, wildlife_record_t *out)
{
    char qty_text[20];
    char *endptr = NULL;
    long qty;
    uint8_t ok = 0U;
    const char *p = line;
    char name[24];
    char cat[20];
    char img[48];
    char level[20];
    char area[24];
    char status[20];
    char updated[24];

    if(line == NULL || out == NULL || line[0] == '\0') return 0;

    p = wl_csv_read_field(p, name, sizeof(name), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, cat, sizeof(cat), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, img, sizeof(img), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, level, sizeof(level), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, area, sizeof(area), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, qty_text, sizeof(qty_text), &ok); if(!ok) return 0;
    p = wl_csv_read_field(p, status, sizeof(status), &ok); if(!ok) return 0;
    (void)wl_csv_read_field(p, updated, sizeof(updated), &ok); if(!ok) return 0;

    qty = strtol(qty_text, &endptr, 10);
    if(endptr == qty_text || *endptr != '\0') return 0;

    if(strcmp(name, "name") == 0 && strcmp(cat, "category") == 0) return 0;

    memset(out, 0, sizeof(*out));
    wl_copy(out->name, sizeof(out->name), name);
    wl_copy(out->category, sizeof(out->category), cat);
    wl_copy(out->image, sizeof(out->image), img);
    wl_copy(out->level, sizeof(out->level), level);
    wl_copy(out->area, sizeof(out->area), area);
    out->quantity = (int32_t)qty;
    wl_copy(out->status, sizeof(out->status), status);
    if(updated[0] == '\0') wl_copy(out->updated_at, sizeof(out->updated_at), WL_DEFAULT_UPDATED_AT);
    else wl_copy(out->updated_at, sizeof(out->updated_at), updated);
    out->valid = 1U;
    return 1;
}

static wildlife_result_t wl_write_csv(const char *path, uint8_t filtered_only)
{
    FIL file;
    FRESULT fr;
    UINT bw;
    uint16_t i;

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr != FR_OK) return WL_ERR_IO;

    if(filtered_only) {
        for(i = 0; i < g_core.view_count; i++) {
            const wildlife_record_t *rec = &g_core.records[g_core.view_idx[i]];
            char line[256];
            int len = snprintf(line,
                               sizeof(line),
                               "%s,%s,%s,%s,%s,%ld,%s,%s\\r\\n",
                               rec->name,
                               rec->category,
                               rec->image,
                               rec->level,
                               rec->area,
                               (long)rec->quantity,
                               rec->status,
                               rec->updated_at);
            if(len > 0) {
                fr = f_write(&file, line, (UINT)len, &bw);
                if(fr != FR_OK || bw != (UINT)len) {
                    f_close(&file);
                    return WL_ERR_IO;
                }
            }
        }
    }
    else {
        for(i = 0; i < WL_MAX_RECORDS; i++) {
            const wildlife_record_t *rec = &g_core.records[i];
            char line[256];
            int len;
            if(!rec->valid) continue;

            len = snprintf(line,
                           sizeof(line),
                           "%s,%s,%s,%s,%s,%ld,%s,%s\\r\\n",
                           rec->name,
                           rec->category,
                           rec->image,
                           rec->level,
                           rec->area,
                           (long)rec->quantity,
                           rec->status,
                           rec->updated_at);
            if(len > 0) {
                fr = f_write(&file, line, (UINT)len, &bw);
                if(fr != FR_OK || bw != (UINT)len) {
                    f_close(&file);
                    return WL_ERR_IO;
                }
            }
        }
    }

    f_close(&file);
    return WL_OK;
}

static wildlife_result_t wl_load_csv(const char *path)
{
    FIL file;
    FRESULT fr;
    UINT br = 0U;
    char buf[4096];
    char *p;
    uint16_t loaded = 0U;

    fr = f_open(&file, path, FA_READ);
    if(fr != FR_OK) return WL_ERR_IO;

    fr = f_read(&file, buf, sizeof(buf) - 1U, &br);
    f_close(&file);
    if(fr != FR_OK) return WL_ERR_IO;

    buf[br] = '\0';
    memset(g_core.records, 0, sizeof(g_core.records));

    p = buf;
    while(*p != '\0') {
        char *eol;
        wildlife_record_t rec;
        int16_t slot;

        eol = strchr(p, '\n');
        if(eol != NULL) {
            *eol = '\0';
            if(eol > p && *(eol - 1) == '\r') *(eol - 1) = '\0';
        }

        if(*p != '\0' && wl_parse_line(p, &rec)) {
            slot = wl_find_free_slot();
            if(slot >= 0) {
                g_core.records[slot] = rec;
                g_core.records[slot].valid = 1U;
                loaded++;
            }
        }

        if(eol == NULL) break;
        p = eol + 1;
    }

    if(loaded == 0U) return WL_ERR_FORMAT;
    return WL_OK;
}

static void wl_apply_filter_internal(void)
{
    uint16_t i;
    g_core.view_count = 0U;

    for(i = 0; i < WL_MAX_RECORDS; i++) {
        wildlife_record_t *rec = &g_core.records[i];
        const char *field;
        int match_filter;
        int match_name;

        if(!rec->valid) continue;

        field = wl_field_by_mode(rec, g_core.mode);
        match_filter = (strcmp(g_core.filter_selected, "ALL") == 0) || (strcmp(field, g_core.filter_selected) == 0);
        match_name = wl_contains_nocase(rec->name, g_core.name_key);

        if(match_filter && match_name) {
            if(g_core.view_count < WL_MAX_RECORDS) {
                g_core.view_idx[g_core.view_count++] = i;
            }
        }
    }

    wl_recompute_stats();
}

void wildlife_core_init(void)
{
    memset(&g_core, 0, sizeof(g_core));
    g_core.mode = WL_MODE_BY_CATEGORY;
    wl_copy(g_core.filter_selected, sizeof(g_core.filter_selected), "ALL");
    wl_copy(g_core.name_key, sizeof(g_core.name_key), "");
    wl_reset_defaults();
    wl_rebuild_filter_options();
    wl_apply_filter_internal();
    wl_set_msg("Core initialized");
}

wildlife_result_t wildlife_core_boot(uint8_t try_load_db)
{
    wildlife_result_t rs;

    wildlife_core_init();

    if(try_load_db) {
        rs = core_load_db();
        if(rs == WL_OK) {
            wl_set_msg("Report: boot auto-load success -> 0:/wildlife_db.csv");
        }
        else {
            wl_set_msg("Report: boot auto-load failed, using built-in default data");
            rs = WL_OK;
        }
    }
    else {
        wl_set_msg("Report: using built-in default data");
        rs = WL_OK;
    }

    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return rs;
}

wildlife_result_t core_apply_filter(wildlife_mode_t mode, const char *val, const char *key)
{
    g_core.mode = mode;
    wl_copy(g_core.filter_selected, sizeof(g_core.filter_selected), (val != NULL) ? val : "ALL");
    wl_copy(g_core.name_key, sizeof(g_core.name_key), (key != NULL) ? key : "");

    wl_rebuild_filter_options();
    wl_apply_filter_internal();

    wl_set_msg("Report: filter applied");
    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return WL_OK;
}

uint16_t core_get_view_count(void)
{
    return g_core.view_count;
}

const wildlife_record_t *core_get_record(uint16_t view_idx)
{
    if(view_idx >= g_core.view_count) return NULL;
    return &g_core.records[g_core.view_idx[view_idx]];
}

wildlife_stats_t core_get_stats(void)
{
    return g_core.stats;
}

wildlife_result_t core_get_filter_options(char *out, uint32_t out_size)
{
    if(out == NULL || out_size == 0U) return WL_ERR_PARAM;
    wl_copy(out, out_size, g_core.filter_options);
    return WL_OK;
}

const char *core_get_last_message(void)
{
    return g_core.last_msg;
}

wildlife_result_t core_update_record(int16_t view_idx, const wildlife_record_t *rec)
{
    int16_t slot;

    if(rec == NULL) return WL_ERR_PARAM;

    if(view_idx >= 0 && (uint16_t)view_idx < g_core.view_count) {
        slot = (int16_t)g_core.view_idx[(uint16_t)view_idx];
    }
    else {
        slot = wl_find_free_slot();
        if(slot < 0) {
            wl_set_msg("Report: apply failed, no free slot");
            ui_push_log(g_core.last_msg);
            return WL_ERR_NO_SPACE;
        }
    }

    g_core.records[slot] = *rec;
    g_core.records[slot].valid = 1U;

    wl_rebuild_filter_options();
    wl_apply_filter_internal();

    wl_set_msg("Report: record applied");
    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return WL_OK;
}

wildlife_result_t core_delete_record(int16_t view_idx)
{
    if(view_idx < 0 || (uint16_t)view_idx >= g_core.view_count) {
        wl_set_msg("Report: delete failed, no selection");
        ui_push_log(g_core.last_msg);
        return WL_ERR_PARAM;
    }

    g_core.records[g_core.view_idx[(uint16_t)view_idx]].valid = 0U;
    wl_rebuild_filter_options();
    wl_apply_filter_internal();

    wl_set_msg("Report: record deleted");
    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return WL_OK;
}

wildlife_result_t core_save_db(void)
{
    wildlife_result_t rs = wl_write_csv(WL_DB_FILE, 0U);
    if(rs == WL_OK) wl_set_msg("Report: DB overwrite saved -> 0:/wildlife_db.csv");
    else wl_set_msg("Report: save failed");

    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return rs;
}

wildlife_result_t core_export_csv(void)
{
    wildlife_result_t rs = wl_write_csv(WL_EXPORT_FILE, 1U);
    if(rs == WL_OK) wl_set_msg("Report: export filtered -> 0:/wildlife_export.csv");
    else wl_set_msg("Report: export failed");

    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return rs;
}

wildlife_result_t core_load_db(void)
{
    wildlife_result_t rs = wl_load_csv(WL_DB_FILE);

    if(rs == WL_OK) {
        wl_rebuild_filter_options();
        wl_apply_filter_internal();
        wl_set_msg("Report: load DB success");
    }
    else {
        wl_set_msg("Report: load DB failed");
    }

    ui_push_log(g_core.last_msg);
    ui_on_data_updated();
    return rs;
}
