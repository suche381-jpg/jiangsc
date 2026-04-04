#include "Wildlife_UI.h"
#include "Wildlife_Core.h"

#include "lvgl/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t *tabview;
    lv_obj_t *dd_mode;
    lv_obj_t *dd_filter;
    lv_obj_t *ta_name;
    lv_obj_t *table;

    lv_obj_t *label_records;
    lv_obj_t *label_risk;
    lv_obj_t *label_trend;
    lv_obj_t *label_report;

    lv_obj_t *label_detail_name;
    lv_obj_t *label_detail_category;
    lv_obj_t *label_detail_image;
    lv_obj_t *label_detail_level;
    lv_obj_t *label_detail_area;
    lv_obj_t *label_detail_qty;
    lv_obj_t *label_detail_status;
    lv_obj_t *label_detail_updated;

    lv_obj_t *pie_canvas;
    lv_obj_t *label_logs;

    lv_obj_t *ta_name_edit;
    lv_obj_t *ta_cat_edit;
    lv_obj_t *ta_img_edit;
    lv_obj_t *ta_level_edit;
    lv_obj_t *ta_area_edit;
    lv_obj_t *ta_qty_edit;
    lv_obj_t *ta_updated_edit;
    lv_obj_t *dd_status_edit;
    lv_obj_t *label_edit_tip;

    lv_obj_t *image_layer;
    lv_obj_t *image_view;
    lv_obj_t *label_image_tip;
} wildlife_ui_ctx_t;

static wildlife_ui_ctx_t g_ui;
static int16_t g_selected_view_idx = -1;
static char g_logs[1024];
static lv_obj_t *g_keyboard;

#define WL_PIE_CANVAS_W 260
#define WL_PIE_CANVAS_H 260
#define WL_PIE_RADIUS 108

static uint8_t g_pie_canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(WL_PIE_CANVAS_W, WL_PIE_CANVAS_H)];
static const char *g_mode_options = "ByCategory\nByLevel\nByArea";
static const char *g_status_options = "stable\nrecovering\ndecreasing\ncritical\nextinct";
static const char *g_status_titles[WL_STATUS_BUCKETS] = {"Critical", "Recovering", "Decreasing", "Stable", "Extinct"};
static const uint32_t g_status_colors_hex[WL_STATUS_BUCKETS] = {0xE74C3C, 0x2ECC71, 0xF39C12, 0x3498DB, 0x8E44AD};
static const int16_t g_cos30_x1000[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
static const int16_t g_sin30_x1000[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};

static void ui_apply_query(void);
static void ui_refresh_all(void);
static void ui_refresh_table(void);
static void ui_refresh_detail(void);
static void ui_refresh_dashboard(void);
static void ui_refresh_edit_form(void);
static void ui_refresh_filter_options(void);
static void ui_refresh_status_pie(const wildlife_stats_t *stats);

static void ui_copy(char *dst, uint32_t size, const char *src)
{
    if(dst == NULL || src == NULL || size == 0U) return;
    strncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static uint8_t ui_is_updated_at_valid(const char *text)
{
    uint8_t i;
    if(text == NULL) return 0U;
    if(strlen(text) != 14U) return 0U;

    for(i = 0; i < 14U; i++) {
        if(i == 2U || i == 5U || i == 8U || i == 11U) {
            if(text[i] != '/') return 0U;
        }
        else {
            if(text[i] < '0' || text[i] > '9') return 0U;
        }
    }
    return 1U;
}

void ui_push_log(const char *msg)
{
    size_t cur;
    size_t add;

    if(msg == NULL || msg[0] == '\0') return;

    cur = strlen(g_logs);
    add = strlen(msg);

    if(cur > 0U) {
        if(cur + 1U + add + 1U >= sizeof(g_logs)) {
            size_t remove_len = cur / 2U;
            memmove(g_logs, g_logs + remove_len, cur - remove_len + 1U);
            cur = strlen(g_logs);
        }
        if(cur + 1U < sizeof(g_logs)) {
            g_logs[cur++] = '\n';
            g_logs[cur] = '\0';
        }
    }

    strncat(g_logs, msg, sizeof(g_logs) - strlen(g_logs) - 1U);

    if(g_ui.label_logs != NULL) {
        lv_label_set_text(g_ui.label_logs, g_logs);
    }
}

void ui_on_data_updated(void)
{
    ui_refresh_all();
}

static void ui_canvas_fill_sector(lv_obj_t *canvas, int32_t start_deg, int32_t sweep_deg, lv_color_t color)
{
    lv_draw_arc_dsc_t arc;
    int32_t end_deg;
    lv_coord_t r;

    if(sweep_deg <= 0) return;

    while(start_deg < 0) start_deg += 360;
    while(start_deg >= 360) start_deg -= 360;

    end_deg = start_deg + sweep_deg - 1;
    lv_draw_arc_dsc_init(&arc);
    arc.color = color;
    arc.width = 1;

    for(r = 1; r <= WL_PIE_RADIUS; r++) {
        if(end_deg < 360) {
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, start_deg, end_deg, &arc);
        }
        else {
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, start_deg, 359, &arc);
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, 0, end_deg % 360, &arc);
        }
    }
}

static void ui_refresh_status_pie(const wildlife_stats_t *stats)
{
    uint16_t i;
    int32_t start_deg = -90;
    int32_t used_deg = 0;
    int32_t active_last = -1;

    if(g_ui.pie_canvas == NULL || stats == NULL) return;

    lv_canvas_fill_bg(g_ui.pie_canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

    for(i = 0; i < WL_STATUS_BUCKETS; i++) {
        if(stats->status_percent[i] > 0U) active_last = (int32_t)i;
    }

    for(i = 0; i < WL_STATUS_BUCKETS; i++) {
        int32_t sweep_deg;
        int32_t mid_deg;
        uint8_t dir_idx;
        int32_t tx;
        int32_t ty;
        lv_draw_label_dsc_t text_dsc;
        char line[40];

        if(stats->status_percent[i] == 0U) continue;

        if((int32_t)i == active_last) sweep_deg = 360 - used_deg;
        else sweep_deg = ((int32_t)stats->status_percent[i] * 360) / 100;
        if(sweep_deg <= 0) sweep_deg = 1;

        ui_canvas_fill_sector(g_ui.pie_canvas, start_deg, sweep_deg, lv_color_hex(g_status_colors_hex[i]));

        lv_draw_label_dsc_init(&text_dsc);
        text_dsc.color = lv_color_hex(0x000000);
        text_dsc.align = LV_TEXT_ALIGN_CENTER;

        mid_deg = start_deg + (sweep_deg / 2);
        while(mid_deg < 0) mid_deg += 360;
        while(mid_deg >= 360) mid_deg -= 360;

        dir_idx = (uint8_t)(((uint32_t)mid_deg + 15U) / 30U);
        if(dir_idx >= 12U) dir_idx = 0U;

        snprintf(line, sizeof(line), "%s/%u%%", g_status_titles[i], (unsigned)stats->status_percent[i]);
        tx = (WL_PIE_CANVAS_W / 2) + ((WL_PIE_RADIUS * 62 / 100) * g_cos30_x1000[dir_idx]) / 1000 - 44;
        ty = (WL_PIE_CANVAS_H / 2) + ((WL_PIE_RADIUS * 62 / 100) * g_sin30_x1000[dir_idx]) / 1000 - 10;
        lv_canvas_draw_text(g_ui.pie_canvas, (lv_coord_t)tx, (lv_coord_t)ty, 88, &text_dsc, line);

        start_deg += sweep_deg;
        used_deg += sweep_deg;
    }
}

static void ui_set_report(const char *msg)
{
    if(g_ui.label_report != NULL) {
        lv_label_set_text(g_ui.label_report, (msg != NULL) ? msg : "");
    }
}

static void ui_refresh_filter_options(void)
{
    char opts[256];
    if(g_ui.dd_filter == NULL) return;

    if(core_get_filter_options(opts, sizeof(opts)) == WL_OK) {
        lv_dropdown_set_options(g_ui.dd_filter, opts);
        lv_dropdown_set_selected(g_ui.dd_filter, 0);
    }
}

static void ui_refresh_table(void)
{
    uint16_t i;
    uint16_t rows = core_get_view_count();

    if(g_ui.table == NULL) return;

    lv_table_set_col_cnt(g_ui.table, 7);
    lv_table_set_col_width(g_ui.table, 0, 34);
    lv_table_set_col_width(g_ui.table, 1, 120);
    lv_table_set_col_width(g_ui.table, 2, 90);
    lv_table_set_col_width(g_ui.table, 3, 70);
    lv_table_set_col_width(g_ui.table, 4, 90);
    lv_table_set_col_width(g_ui.table, 5, 52);
    lv_table_set_col_width(g_ui.table, 6, 84);
    lv_table_set_row_cnt(g_ui.table, rows + 1U);

    lv_table_set_cell_value(g_ui.table, 0, 0, "ID");
    lv_table_set_cell_value(g_ui.table, 0, 1, "Name");
    lv_table_set_cell_value(g_ui.table, 0, 2, "Category");
    lv_table_set_cell_value(g_ui.table, 0, 3, "Level");
    lv_table_set_cell_value(g_ui.table, 0, 4, "Area");
    lv_table_set_cell_value(g_ui.table, 0, 5, "Qty");
    lv_table_set_cell_value(g_ui.table, 0, 6, "Status");

    for(i = 0; i < rows; i++) {
        char id_buf[8];
        char qty_buf[16];
        const wildlife_record_t *rec = core_get_record(i);

        if(rec == NULL) continue;

        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)(i + 1U));
        snprintf(qty_buf, sizeof(qty_buf), "%ld", (long)rec->quantity);

        lv_table_set_cell_value(g_ui.table, i + 1U, 0, id_buf);
        lv_table_set_cell_value(g_ui.table, i + 1U, 1, rec->name);
        lv_table_set_cell_value(g_ui.table, i + 1U, 2, rec->category);
        lv_table_set_cell_value(g_ui.table, i + 1U, 3, rec->level);
        lv_table_set_cell_value(g_ui.table, i + 1U, 4, rec->area);
        lv_table_set_cell_value(g_ui.table, i + 1U, 5, qty_buf);
        lv_table_set_cell_value(g_ui.table, i + 1U, 6, rec->status);
    }

    if(g_selected_view_idx >= (int16_t)rows) g_selected_view_idx = -1;
}

static void ui_fill_edit_form(int16_t view_idx)
{
    const wildlife_record_t *rec;
    char qty[16];

    if(g_ui.ta_name_edit == NULL) return;

    if(view_idx < 0) {
        lv_textarea_set_text(g_ui.ta_name_edit, "");
        lv_textarea_set_text(g_ui.ta_cat_edit, "");
        lv_textarea_set_text(g_ui.ta_img_edit, "");
        lv_textarea_set_text(g_ui.ta_level_edit, "");
        lv_textarea_set_text(g_ui.ta_area_edit, "");
        lv_textarea_set_text(g_ui.ta_qty_edit, "0");
        lv_textarea_set_text(g_ui.ta_updated_edit, WL_DEFAULT_UPDATED_AT);
        lv_dropdown_set_selected(g_ui.dd_status_edit, 0);
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: New record");
        return;
    }

    rec = core_get_record((uint16_t)view_idx);
    if(rec == NULL) return;

    snprintf(qty, sizeof(qty), "%ld", (long)rec->quantity);
    lv_textarea_set_text(g_ui.ta_name_edit, rec->name);
    lv_textarea_set_text(g_ui.ta_cat_edit, rec->category);
    lv_textarea_set_text(g_ui.ta_img_edit, rec->image);
    lv_textarea_set_text(g_ui.ta_level_edit, rec->level);
    lv_textarea_set_text(g_ui.ta_area_edit, rec->area);
    lv_textarea_set_text(g_ui.ta_qty_edit, qty);
    lv_textarea_set_text(g_ui.ta_updated_edit, rec->updated_at[0] ? rec->updated_at : WL_DEFAULT_UPDATED_AT);

    if(strcmp(rec->status, "stable") == 0) lv_dropdown_set_selected(g_ui.dd_status_edit, 0);
    else if(strcmp(rec->status, "recovering") == 0) lv_dropdown_set_selected(g_ui.dd_status_edit, 1);
    else if(strcmp(rec->status, "decreasing") == 0) lv_dropdown_set_selected(g_ui.dd_status_edit, 2);
    else if(strcmp(rec->status, "critical") == 0) lv_dropdown_set_selected(g_ui.dd_status_edit, 3);
    else lv_dropdown_set_selected(g_ui.dd_status_edit, 4);

    if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: Existing record");
}

static void ui_refresh_detail(void)
{
    const wildlife_record_t *rec = NULL;
    char qty[24];

    if(g_selected_view_idx >= 0) {
        rec = core_get_record((uint16_t)g_selected_view_idx);
    }

    if(rec == NULL) {
        if(g_ui.label_detail_name) lv_label_set_text(g_ui.label_detail_name, "-");
        if(g_ui.label_detail_category) lv_label_set_text(g_ui.label_detail_category, "-");
        if(g_ui.label_detail_image) lv_label_set_text(g_ui.label_detail_image, "-");
        if(g_ui.label_detail_level) lv_label_set_text(g_ui.label_detail_level, "-");
        if(g_ui.label_detail_area) lv_label_set_text(g_ui.label_detail_area, "-");
        if(g_ui.label_detail_qty) lv_label_set_text(g_ui.label_detail_qty, "-");
        if(g_ui.label_detail_status) lv_label_set_text(g_ui.label_detail_status, "-");
        if(g_ui.label_detail_updated) lv_label_set_text(g_ui.label_detail_updated, WL_DEFAULT_UPDATED_AT);
        ui_fill_edit_form(-1);
        return;
    }

    if(g_ui.label_detail_name) lv_label_set_text(g_ui.label_detail_name, rec->name);
    if(g_ui.label_detail_category) lv_label_set_text(g_ui.label_detail_category, rec->category);
    if(g_ui.label_detail_image) lv_label_set_text(g_ui.label_detail_image, rec->image);
    if(g_ui.label_detail_level) lv_label_set_text(g_ui.label_detail_level, rec->level);
    if(g_ui.label_detail_area) lv_label_set_text(g_ui.label_detail_area, rec->area);
    snprintf(qty, sizeof(qty), "%ld", (long)rec->quantity);
    if(g_ui.label_detail_qty) lv_label_set_text(g_ui.label_detail_qty, qty);
    if(g_ui.label_detail_status) lv_label_set_text(g_ui.label_detail_status, rec->status);
    if(g_ui.label_detail_updated) lv_label_set_text(g_ui.label_detail_updated, rec->updated_at);

    ui_fill_edit_form(g_selected_view_idx);
}

static void ui_refresh_dashboard(void)
{
    wildlife_stats_t stats = core_get_stats();
    char buf[96];

    if(g_ui.label_records) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)stats.filtered_count);
        lv_label_set_text(g_ui.label_records, buf);
    }

    if(g_ui.label_risk) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)stats.critical_count);
        lv_label_set_text(g_ui.label_risk, buf);
    }

    if(g_ui.label_trend) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)stats.decreasing_count);
        lv_label_set_text(g_ui.label_trend, buf);
    }

    snprintf(buf,
             sizeof(buf),
             "Summary: %u records, qty=%ld, valid=%u",
             (unsigned)stats.filtered_count,
             (long)stats.total_quantity,
             (unsigned)stats.valid_count);
    ui_set_report(buf);

    ui_refresh_status_pie(&stats);
}

static void ui_refresh_all(void)
{
    ui_refresh_filter_options();
    ui_refresh_table();
    ui_refresh_detail();
    ui_refresh_dashboard();

    ui_set_report(core_get_last_message());
    if(g_ui.label_logs) lv_label_set_text(g_ui.label_logs, g_logs);
}

static void ui_query_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_apply_query();
}

static void ui_mode_or_filter_cb(lv_event_t *e)
{
    (void)e;
    ui_apply_query();
}

static void ui_reset_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(g_ui.ta_name, "");
    lv_dropdown_set_selected(g_ui.dd_mode, 0);
    core_apply_filter(WL_MODE_BY_CATEGORY, "ALL", "");
}

static void ui_save_btn_cb(lv_event_t *e)
{
    (void)e;
    core_save_db();
}

static void ui_export_btn_cb(lv_event_t *e)
{
    (void)e;
    core_export_csv();
}

static void ui_stat_btn_cb(lv_event_t *e)
{
    wildlife_stats_t s;
    char buf[128];
    (void)e;

    s = core_get_stats();
    snprintf(buf,
             sizeof(buf),
             "Report: result=%u, critical=%u, decreasing=%u",
             (unsigned)s.filtered_count,
             (unsigned)s.critical_count,
             (unsigned)s.decreasing_count);
    ui_set_report(buf);
    ui_push_log(buf);
}

static void ui_table_event_cb(lv_event_t *e)
{
    uint16_t row;
    uint16_t col;
    lv_event_code_t code = lv_event_get_code(e);
    (void)e;

    if(code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED) return;

    lv_table_get_selected_cell(g_ui.table, &row, &col);
    (void)col;
    if(row == 0U || row > core_get_view_count()) return;

    g_selected_view_idx = (int16_t)(row - 1U);
    ui_refresh_detail();

    if(g_ui.image_layer != NULL && g_ui.image_view != NULL && g_ui.label_image_tip != NULL) {
        const wildlife_record_t *rec = core_get_record((uint16_t)g_selected_view_idx);
        if(rec != NULL && rec->image[0] != '\0') {
            lv_img_set_src(g_ui.image_view, rec->image);
            lv_obj_clear_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_ui.image_layer);
            lv_label_set_text_fmt(g_ui.label_image_tip, "Image: %s (tap image to return)", rec->name);
        }
    }
}

static void ui_image_back_cb(lv_event_t *e)
{
    (void)e;
    if(g_ui.image_layer != NULL) lv_obj_add_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
}

static void ui_new_btn_cb(lv_event_t *e)
{
    (void)e;
    g_selected_view_idx = -1;
    ui_fill_edit_form(-1);
    ui_set_report("Edit mode: New record");
}

static void ui_apply_edit_btn_cb(lv_event_t *e)
{
    wildlife_record_t rec;
    const char *name;
    const char *cat;
    const char *img;
    const char *level;
    const char *area;
    const char *qty;
    const char *updated;
    char status[20];

    (void)e;

    name = lv_textarea_get_text(g_ui.ta_name_edit);
    cat = lv_textarea_get_text(g_ui.ta_cat_edit);
    img = lv_textarea_get_text(g_ui.ta_img_edit);
    level = lv_textarea_get_text(g_ui.ta_level_edit);
    area = lv_textarea_get_text(g_ui.ta_area_edit);
    qty = lv_textarea_get_text(g_ui.ta_qty_edit);
    updated = lv_textarea_get_text(g_ui.ta_updated_edit);
    lv_dropdown_get_selected_str(g_ui.dd_status_edit, status, sizeof(status));

    if(strlen(name) == 0U || strlen(cat) == 0U || strlen(level) == 0U || strlen(area) == 0U) {
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: required fields are empty");
        return;
    }

    if(updated == NULL || updated[0] == '\0') {
        updated = WL_DEFAULT_UPDATED_AT;
    }
    if(!ui_is_updated_at_valid(updated)) {
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: Updated format invalid, use 26/04/04/16/51");
        return;
    }

    memset(&rec, 0, sizeof(rec));
    ui_copy(rec.name, sizeof(rec.name), name);
    ui_copy(rec.category, sizeof(rec.category), cat);
    ui_copy(rec.image, sizeof(rec.image), img);
    ui_copy(rec.level, sizeof(rec.level), level);
    ui_copy(rec.area, sizeof(rec.area), area);
    rec.quantity = (int32_t)atoi(qty);
    ui_copy(rec.status, sizeof(rec.status), status);
    ui_copy(rec.updated_at, sizeof(rec.updated_at), updated);
    rec.valid = 1U;

    if(core_update_record(g_selected_view_idx, &rec) == WL_OK) {
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: apply success");
    }
    else {
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: apply failed");
    }
}

static void ui_delete_btn_cb(lv_event_t *e)
{
    (void)e;
    if(core_delete_record(g_selected_view_idx) == WL_OK) {
        g_selected_view_idx = -1;
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: record deleted");
    }
    else {
        if(g_ui.label_edit_tip) lv_label_set_text(g_ui.label_edit_tip, "Edit mode: no record selected");
    }
}

static void ui_apply_query(void)
{
    uint16_t mode = lv_dropdown_get_selected(g_ui.dd_mode);
    char filter_val[24];
    const char *name_key = lv_textarea_get_text(g_ui.ta_name);

    lv_dropdown_get_selected_str(g_ui.dd_filter, filter_val, sizeof(filter_val));
    core_apply_filter((wildlife_mode_t)mode, filter_val, name_key);
}

static lv_obj_t *ui_labeled_ta(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_label_set_text(label, label_text);
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_width(ta, lv_pct(98));
    return ta;
}

static void ui_create_main_tab(lv_obj_t *tab)
{
    lv_obj_t *root = lv_obj_create(tab);
    lv_obj_t *left = lv_obj_create(root);
    lv_obj_t *right = lv_obj_create(root);
    lv_obj_t *btn_row;
    lv_obj_t *btn;
    lv_obj_t *detail;
    lv_obj_t *table_wrap;

    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_style_pad_column(root, 10, 0);

    lv_obj_set_size(left, 0, lv_pct(100));
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 8, 0);

    g_ui.dd_mode = lv_dropdown_create(left);
    lv_dropdown_set_options(g_ui.dd_mode, g_mode_options);
    lv_obj_set_width(g_ui.dd_mode, 180);
    lv_obj_add_event_cb(g_ui.dd_mode, ui_mode_or_filter_cb, LV_EVENT_VALUE_CHANGED, NULL);

    g_ui.dd_filter = lv_dropdown_create(left);
    lv_obj_set_width(g_ui.dd_filter, 180);
    lv_obj_add_event_cb(g_ui.dd_filter, ui_mode_or_filter_cb, LV_EVENT_VALUE_CHANGED, NULL);

    g_ui.ta_name = lv_textarea_create(left);
    lv_textarea_set_one_line(g_ui.ta_name, 1);
    lv_textarea_set_placeholder_text(g_ui.ta_name, "Name keyword");
    lv_obj_set_width(g_ui.ta_name, lv_pct(100));

    btn_row = lv_obj_create(left);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, 58);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(btn_row, 6, 0);
    lv_obj_set_style_pad_row(btn_row, 6, 0);

    btn = lv_btn_create(btn_row);
    lv_obj_set_size(btn, 96, 30);
    lv_obj_add_event_cb(btn, ui_query_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Query");

    btn = lv_btn_create(btn_row);
    lv_obj_set_size(btn, 96, 30);
    lv_obj_add_event_cb(btn, ui_stat_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Statistics");

    btn = lv_btn_create(btn_row);
    lv_obj_set_size(btn, 96, 30);
    lv_obj_add_event_cb(btn, ui_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Save DB");

    btn = lv_btn_create(btn_row);
    lv_obj_set_size(btn, 96, 30);
    lv_obj_add_event_cb(btn, ui_export_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Export");

    btn = lv_btn_create(btn_row);
    lv_obj_set_size(btn, 96, 30);
    lv_obj_add_event_cb(btn, ui_reset_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Reset");

    table_wrap = lv_obj_create(left);
    lv_obj_set_size(table_wrap, lv_pct(100), 0);
    lv_obj_set_flex_grow(table_wrap, 1);

    g_ui.table = lv_table_create(table_wrap);
    lv_obj_set_size(g_ui.table, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(g_ui.table, ui_table_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_ui.table, ui_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_size(right, 280, lv_pct(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 8, 0);

    g_ui.label_records = lv_label_create(right);
    g_ui.label_risk = lv_label_create(right);
    g_ui.label_trend = lv_label_create(right);
    g_ui.label_report = lv_label_create(right);

    g_ui.pie_canvas = lv_canvas_create(right);
    lv_canvas_set_buffer(g_ui.pie_canvas, g_pie_canvas_buf, WL_PIE_CANVAS_W, WL_PIE_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(g_ui.pie_canvas, WL_PIE_CANVAS_W, WL_PIE_CANVAS_H);

    detail = lv_obj_create(right);
    lv_obj_set_size(detail, lv_pct(100), 0);
    lv_obj_set_flex_grow(detail, 1);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);

    g_ui.label_detail_name = lv_label_create(detail);
    g_ui.label_detail_category = lv_label_create(detail);
    g_ui.label_detail_image = lv_label_create(detail);
    g_ui.label_detail_level = lv_label_create(detail);
    g_ui.label_detail_area = lv_label_create(detail);
    g_ui.label_detail_qty = lv_label_create(detail);
    g_ui.label_detail_status = lv_label_create(detail);
    g_ui.label_detail_updated = lv_label_create(detail);

    g_ui.label_logs = lv_label_create(right);
    lv_obj_set_width(g_ui.label_logs, lv_pct(100));
    lv_label_set_long_mode(g_ui.label_logs, LV_LABEL_LONG_WRAP);

    g_ui.image_layer = lv_obj_create(tab);
    lv_obj_set_size(g_ui.image_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui.image_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_ui.image_layer, LV_OPA_90, 0);
    lv_obj_add_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_ui.image_layer, ui_image_back_cb, LV_EVENT_CLICKED, NULL);

    g_ui.image_view = lv_img_create(g_ui.image_layer);
    lv_obj_center(g_ui.image_view);
    lv_obj_add_event_cb(g_ui.image_view, ui_image_back_cb, LV_EVENT_CLICKED, NULL);

    g_ui.label_image_tip = lv_label_create(g_ui.image_layer);
    lv_obj_set_width(g_ui.label_image_tip, lv_pct(100));
    lv_obj_align(g_ui.label_image_tip, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_text_color(g_ui.label_image_tip, lv_color_white(), 0);
    lv_obj_set_style_text_align(g_ui.label_image_tip, LV_TEXT_ALIGN_CENTER, 0);
}

static void ui_create_edit_tab(lv_obj_t *tab)
{
    lv_obj_t *root = lv_obj_create(tab);
    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_t *row_btn = lv_obj_create(root);
    lv_obj_t *btn;

    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(cont, lv_pct(98), lv_pct(76));
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);

    g_ui.ta_name_edit = ui_labeled_ta(cont, "Name");
    g_ui.ta_cat_edit = ui_labeled_ta(cont, "Category");
    g_ui.ta_img_edit = ui_labeled_ta(cont, "Image Path");
    g_ui.ta_level_edit = ui_labeled_ta(cont, "Protection Level");
    g_ui.ta_area_edit = ui_labeled_ta(cont, "Distribution Area");
    g_ui.ta_qty_edit = ui_labeled_ta(cont, "Quantity");
    g_ui.ta_updated_edit = ui_labeled_ta(cont, "Updated (YY/MM/DD/HH/MM)");
    lv_textarea_set_text(g_ui.ta_updated_edit, WL_DEFAULT_UPDATED_AT);

    {
        lv_obj_t *label = lv_label_create(cont);
        lv_label_set_text(label, "Status");
        g_ui.dd_status_edit = lv_dropdown_create(cont);
        lv_dropdown_set_options(g_ui.dd_status_edit, g_status_options);
        lv_obj_set_width(g_ui.dd_status_edit, lv_pct(98));
    }

    lv_obj_set_size(row_btn, lv_pct(98), 56);
    lv_obj_align_to(row_btn, cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_set_flex_flow(row_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 130, 34);
    lv_obj_add_event_cb(btn, ui_new_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "New");

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 130, 34);
    lv_obj_add_event_cb(btn, ui_apply_edit_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Apply");

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 130, 34);
    lv_obj_add_event_cb(btn, ui_delete_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Delete");

    g_ui.label_edit_tip = lv_label_create(root);
    lv_obj_align_to(g_ui.label_edit_tip, row_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(g_ui.label_edit_tip, "Edit mode: ready");
}

void wildlife_ui_init(void)
{
    lv_obj_t *tab_main;
    lv_obj_t *tab_edit;

    memset(&g_ui, 0, sizeof(g_ui));
    memset(g_logs, 0, sizeof(g_logs));
    g_keyboard = NULL;

    lv_obj_clean(lv_scr_act());
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    g_ui.tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 46);
    lv_obj_set_size(g_ui.tabview, lv_pct(100), lv_pct(100));

    tab_main = lv_tabview_add_tab(g_ui.tabview, "Main");
    tab_edit = lv_tabview_add_tab(g_ui.tabview, "Edit");

    ui_create_main_tab(tab_main);
    ui_create_edit_tab(tab_edit);

    wildlife_core_boot(1U);
}

void wildlife_app_start(void)
{
    wildlife_ui_init();
}

void Hang2Hang(void)
{
    wildlife_ui_init();
}
