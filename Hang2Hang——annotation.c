#include "lvgl/lvgl.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * 本文件专用于 GD32H7 嵌入式工程
 * 这里统一FATFS + SDRAM 路径实现
 * 以减少条件编译复杂度并提升维护可读
 */
#include "FATFS/ff.h"
#include "interaction_shell.h"

/*
 * 单条野生动物档案的数据模型
 *
 * 这个结构体既用于初始化默认数据，也用于运行时的新增编辑删除
 * 扢有字符串字段都采用定长数组，原因是该工程运行在嵌入式环境中，
 * 定长缓冲区可以避免频繁堆分配，并且更容易控制 UI 文本长度
 */
typedef struct {
    char name[24];
    char category[20];
    char image[48];
    char level[20];
    char area[24];
    int quantity;
    char status[20];
    char updated_at[24];
    uint8_t valid;
} wildlife_record_t;

typedef struct {
    char name[64];
    uint8_t is_dir;
} wl_file_item_t;

/*
 * 页面UI 句柄集合
 *
 * 这里集中保存扢有关键控件的指针，便于事件回调和刷新函数直接访问界面状
 * 这种写法是典型的“单例式 UI 上下文，适合此类演示型应用
 */
typedef struct {
    lv_obj_t *tabview;
    lv_obj_t *sidebar_buttons[4];
    uint8_t sidebar_active;
    lv_obj_t *center_col;
    lv_obj_t *right_col;
    lv_obj_t *ctrl_wrap;
    lv_obj_t *table_wrap;
    lv_obj_t *kpi_wrap;
    lv_obj_t *pie_panel;
    lv_obj_t *pie_canvas;
    lv_obj_t *pie_legend[5];
    lv_obj_t *detail_panel;
    lv_obj_t *footer_panel;
    lv_obj_t *log_panel;
    lv_obj_t *label_log_history;
    lv_obj_t *file_panel;
    lv_obj_t *file_path;
    lv_obj_t *file_table;
    lv_obj_t *file_tip;
    lv_obj_t *dd_mode;
    lv_obj_t *dd_filter;
    lv_obj_t *ta_name;
    lv_obj_t *table;
    lv_obj_t *label_card_records;
    lv_obj_t *label_card_risk;
    lv_obj_t *label_card_trend;
    lv_obj_t *label_detail;
    lv_obj_t *label_detail_name;
    lv_obj_t *label_detail_category;
    lv_obj_t *label_detail_image;
    lv_obj_t *label_detail_level;
    lv_obj_t *label_detail_area;
    lv_obj_t *label_detail_qty;
    lv_obj_t *label_detail_status;
    lv_obj_t *label_detail_updated;
    lv_obj_t *label_detail_valid;
    lv_obj_t *label_report;
    lv_obj_t *image_layer;
    lv_obj_t *image_view;
    lv_obj_t *label_image_tip;
    lv_obj_t *btn_modify;
    lv_obj_t *ta_name_edit;
    lv_obj_t *ta_cat_edit;
    lv_obj_t *ta_img_edit;
    lv_obj_t *ta_level_edit;
    lv_obj_t *ta_area_edit;
    lv_obj_t *ta_qty_edit;
    lv_obj_t *ta_updated_edit;
    lv_obj_t *dd_status_edit;
    lv_obj_t *label_edit_tip;
} wildlife_ui_t;

/*
 * 设计约束
 * - 数据上限32 条，足够覆盖当前演示场景
 * - 过滤结果数量与原始记录上限保持一致，避免额外动内存管理
 */
#define WL_MAX_RECORDS 32
#define WL_MAX_FILTERED WL_MAX_RECORDS
#define WL_DB_FILE "0:/wildlife_db.csv"
#define WL_EXPORT_FILE "0:/wildlife_export.csv"
#define WL_DEFAULT_UPDATED_AT "00/00/00/00/00"
#define WL_ENABLE_BOOT_LOAD 1
#define WL_FILE_MAX_ITEMS 48
#define WL_STATUS_BUCKETS 5
#define WL_PIE_CANVAS_W 280
#define WL_PIE_CANVAS_H 280
#define WL_PIE_RADIUS 118

/*
 * 查询模式选项
 * - ByCategory：按物种类别聚合或过滤；
 * - ByLevel：按保护等级查看
 * - ByArea：按分布区域查看
 */
static const char *g_mode_options = "ByCategory\nByLevel\nByArea";

/*
 * 编辑页允许设置的状
 * 这些值会被写入记录结构中status 字段，并参与统计和筛选显示
 */
static const char *g_status_options = "stable\nrecovering\ndecreasing\ncritical\nextinct";

/*
 * 默认内置数据集
 *
 * 这是应用启动时恢复的基线数据，便于模拟有初始档案库的真实场景
 * 第九valid 固定1，表示该条记录可参与查询和统计
 */
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

static wildlife_record_t g_records[WL_MAX_RECORDS];
static uint16_t g_filtered_idx[WL_MAX_FILTERED];
static uint16_t g_filtered_count;
static int16_t g_selected_idx = -1;
static char g_filter_opts[256];
static core_state_snapshot_t g_core_snapshot;
static wildlife_ui_t g_ui;
/*
 * 这些全局对象共同构成了整个页面的运行时上下文。
 * 它们会被初始化函数创建，再被过滤回调、文件浏览器、编辑页和统计视图反复读取与更新，
 * 所以不能放在局部变量里，而要在全生命周期内统一保存。
 */
static char g_image_path[80];
static char g_log_history[1024];
static wl_file_item_t g_file_items[WL_FILE_MAX_ITEMS];
static uint16_t g_file_count;
static int16_t g_file_selected = -1;
static char g_file_path[96] = "0:/";
static lv_obj_t *g_keyboard;
static uint8_t g_pie_canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(WL_PIE_CANVAS_W, WL_PIE_CANVAS_H)];
static const char *g_status_titles[WL_STATUS_BUCKETS] = {"Critical", "Recovering", "Decreasing", "Stable", "Extinct"};
static const uint32_t g_status_colors_hex[WL_STATUS_BUCKETS] = {0xE74C3C, 0x2ECC71, 0xF39C12, 0x3498DB, 0x8E44AD};
static const int16_t g_cos30_x1000[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
static const int16_t g_sin30_x1000[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};

/*
 * 全局样式对象
 *
 * LVGL 的样式常建议预先初始化并复用，避免重复创建导致的弢锢和状态混乱
 * 这个界面采用“卡+ 侧边+ 高亮按钮”的视觉风格，因此样式对象较多
 */
static lv_style_t g_style_screen;
static lv_style_t g_style_panel;
static lv_style_t g_style_sidebar;
static lv_style_t g_style_sidebar_btn;
static lv_style_t g_style_sidebar_btn_pressed;
static lv_style_t g_style_sidebar_btn_checked;
static lv_style_t g_style_control;
static lv_style_t g_style_input;
static lv_style_t g_style_action_btn;
static lv_style_t g_style_action_btn_pressed;
static lv_style_t g_style_table;
static lv_style_t g_style_card;
static lv_style_t g_style_tab_btn;
static lv_style_t g_style_tab_btn_checked;
static lv_style_t g_style_dropdown_button_rtl;
static lv_style_t g_style_dropdown_list;
static lv_style_t g_style_dropdown_list_selected;
static lv_style_t g_style_kpi_card;
static lv_style_t g_style_kpi_value;
static lv_style_t g_style_kpi_label;
static lv_style_t g_style_detail_panel;
static lv_style_t g_style_detail_title;
static lv_style_t g_style_detail_key;
static lv_style_t g_style_detail_value;
static lv_style_t g_style_badge;
static lv_style_t g_style_badge_text;
static uint8_t g_styles_ready;

/*
 * 数据准备与视图刷新相关的核心函数声明
 * 这些函数构成了页面的主要数据流：初始化默认数-> 更新筛项 -> 应用过滤 -> 刷新表格和仪表盘
 */
static void wl_update_filter_options(void);
static void wl_apply_filter(void);
static void wl_refresh_dashboard(void);
static void wl_style_dropdown_list(lv_obj_t * dropdown);
static void wl_style_dropdown_button(lv_obj_t * dropdown);
static uint8_t wl_load_db(void);
static uint8_t wl_write_file(const char *path, uint8_t filtered_only);
static void wl_show_selected_image(void);
static void wl_set_report_text(const char *text);
static void wl_sync_sidebar_view(uint8_t index);
static void wl_release_image_buffer(void);
static uint8_t wl_build_sd_path(const char *image_field, char *out, uint32_t out_size);
static void wl_hide_image_layer(void);
static void wl_image_back_event_cb(lv_event_t *e);
static void wl_save_btn_cb(lv_event_t *e);
static void wl_export_btn_cb(lv_event_t *e);
static void wl_to_edit_btn_cb(lv_event_t *e);
static void wl_keyboard_ensure_created(lv_obj_t *screen);
static void wl_keyboard_attach_to_textarea(lv_obj_t *ta);
static void wl_keyboard_hide(void);
static void wl_textarea_focus_cb(lv_event_t *e);
static void wl_screen_click_cb(lv_event_t *e);
static void wl_keyboard_event_cb(lv_event_t *e);
static void wl_file_action_cb(lv_event_t *e);
static void wl_file_table_event_cb(lv_event_t *e);
static void wl_tab_changed_cb(lv_event_t *e);
static void wl_file_refresh(void);
static void wl_file_open_selected(void);
static void wl_file_up(void);
static void wl_file_delete_selected(void);
static void wl_refresh_status_pie(void);
static lv_obj_t *wl_create_kpi_card(lv_obj_t *parent, const char *caption, lv_obj_t **out_value);
static lv_obj_t *wl_create_detail_row(lv_obj_t *parent, const char *key, lv_obj_t **out_value);
static const char *wl_status_title(const char *status);
static void wl_modify_btn_cb(lv_event_t *e);
static const char *wl_mode_title(uint16_t mode);
static void wl_make_safe_token(const char *src, char *dst, uint32_t dst_size, const char *fallback);
static void wl_build_export_path(char *out, uint32_t out_size);
static const char *wl_csv_read_field(const char *src, char *dst, uint32_t dst_size, uint8_t *ok);
static uint8_t wl_is_updated_at_valid(const char *text);

static void wl_append_log(const char *text)
{
    size_t cur_len;
    size_t add_len;

    if(text == NULL || text[0] == '\0') return;

    cur_len = strlen(g_log_history);
    add_len = strlen(text);

    if(cur_len > 0U) {
        if(cur_len + 1U + add_len + 1U >= sizeof(g_log_history)) {
            size_t remove_len = (cur_len / 2U);
            memmove(g_log_history, g_log_history + remove_len, cur_len - remove_len + 1U);
            cur_len = strlen(g_log_history);
        }

        if(cur_len + 1U < sizeof(g_log_history)) {
            g_log_history[cur_len++] = '\n';
            g_log_history[cur_len] = '\0';
        }
    }

    if(cur_len + add_len + 1U >= sizeof(g_log_history)) {
        size_t keep = sizeof(g_log_history) - 1U;
        if(add_len >= keep) {
            memcpy(g_log_history, text + (add_len - keep), keep);
            g_log_history[keep] = '\0';
            return;
        }
    }

    strncat(g_log_history, text, sizeof(g_log_history) - strlen(g_log_history) - 1U);
}

static void wl_init_styles(void)
{
    if(g_styles_ready) return;

    lv_style_init(&g_style_screen);
    lv_style_set_bg_color(&g_style_screen, lv_color_hex(0xF5F5F5));
    lv_style_set_bg_opa(&g_style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_screen, lv_color_hex(0x2C3E50));
    lv_style_set_radius(&g_style_screen, 24);
    lv_style_set_clip_corner(&g_style_screen, true);
    lv_style_set_pad_all(&g_style_screen, 12);

    lv_style_init(&g_style_panel);
    lv_style_set_bg_color(&g_style_panel, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_panel, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_panel, 0);
    lv_style_set_shadow_width(&g_style_panel, 30);
    lv_style_set_shadow_color(&g_style_panel, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_panel, 10);
    lv_style_set_radius(&g_style_panel, 16);
    lv_style_set_pad_all(&g_style_panel, 12);
    lv_style_set_text_color(&g_style_panel, lv_color_hex(0x2C3E50));

    lv_style_init(&g_style_sidebar);
    lv_style_set_bg_color(&g_style_sidebar, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_sidebar, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_sidebar, 0);
    lv_style_set_shadow_width(&g_style_sidebar, 30);
    lv_style_set_shadow_color(&g_style_sidebar, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_sidebar, 10);
    lv_style_set_radius(&g_style_sidebar, 16);
    lv_style_set_pad_all(&g_style_sidebar, 12);

    lv_style_init(&g_style_sidebar_btn);
    lv_style_set_radius(&g_style_sidebar_btn, 14);
    lv_style_set_bg_opa(&g_style_sidebar_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&g_style_sidebar_btn, 0);
    lv_style_set_text_color(&g_style_sidebar_btn, lv_color_hex(0x2C3E50));

    lv_style_init(&g_style_sidebar_btn_pressed);
    lv_style_set_bg_color(&g_style_sidebar_btn_pressed, lv_color_hex(0x8BA88E));
    lv_style_set_bg_opa(&g_style_sidebar_btn_pressed, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_sidebar_btn_pressed, lv_color_white());
    lv_style_set_radius(&g_style_sidebar_btn_pressed, 14);

    lv_style_init(&g_style_sidebar_btn_checked);
    lv_style_set_bg_color(&g_style_sidebar_btn_checked, lv_color_hex(0xEEF4EF));
    lv_style_set_bg_opa(&g_style_sidebar_btn_checked, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_sidebar_btn_checked, lv_color_hex(0x22313F));
    lv_style_set_border_width(&g_style_sidebar_btn_checked, 4);
    lv_style_set_border_side(&g_style_sidebar_btn_checked, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_color(&g_style_sidebar_btn_checked, lv_color_hex(0x8BA88E));
    lv_style_set_radius(&g_style_sidebar_btn_checked, 14);

    lv_style_init(&g_style_control);
    lv_style_set_bg_color(&g_style_control, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_control, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_control, 0);
    lv_style_set_shadow_width(&g_style_control, 18);
    lv_style_set_shadow_color(&g_style_control, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_control, 8);
    lv_style_set_radius(&g_style_control, 16);
    lv_style_set_text_color(&g_style_control, lv_color_hex(0x2C3E50));

    lv_style_init(&g_style_input);
    lv_style_set_bg_color(&g_style_input, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_input, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_input, 1);
    lv_style_set_border_color(&g_style_input, lv_color_hex(0xE0E5EA));
    lv_style_set_radius(&g_style_input, 12);
    lv_style_set_text_color(&g_style_input, lv_color_hex(0x2C3E50));
    lv_style_set_pad_left(&g_style_input, 10);
    lv_style_set_pad_right(&g_style_input, 10);

    lv_style_init(&g_style_action_btn);
    lv_style_set_radius(&g_style_action_btn, 14);
    lv_style_set_bg_color(&g_style_action_btn, lv_color_hex(0x8BA88E));
    lv_style_set_bg_opa(&g_style_action_btn, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_action_btn, lv_color_white());
    lv_style_set_border_width(&g_style_action_btn, 0);
    lv_style_set_shadow_width(&g_style_action_btn, 16);
    lv_style_set_shadow_color(&g_style_action_btn, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_action_btn, 12);

    lv_style_init(&g_style_action_btn_pressed);
    lv_style_set_bg_color(&g_style_action_btn_pressed, lv_color_hex(0x76927A));
    lv_style_set_bg_opa(&g_style_action_btn_pressed, LV_OPA_COVER);

    lv_style_init(&g_style_table);
    lv_style_set_bg_color(&g_style_table, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_table, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_table, 0);
    lv_style_set_radius(&g_style_table, 12);
    lv_style_set_text_color(&g_style_table, lv_color_hex(0x2C3E50));

    lv_style_init(&g_style_card);
    lv_style_set_bg_color(&g_style_card, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_card, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_card, 0);
    lv_style_set_shadow_width(&g_style_card, 30);
    lv_style_set_shadow_color(&g_style_card, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_card, 10);
    lv_style_set_radius(&g_style_card, 20);
    lv_style_set_text_color(&g_style_card, lv_color_hex(0x2C3E50));
    lv_style_set_pad_all(&g_style_card, 10);

    lv_style_init(&g_style_tab_btn);
    lv_style_set_bg_color(&g_style_tab_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_tab_btn, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_tab_btn, lv_color_hex(0x7F8C8D));
    lv_style_set_border_width(&g_style_tab_btn, 0);
    lv_style_set_radius(&g_style_tab_btn, 10);

    lv_style_init(&g_style_tab_btn_checked);
    lv_style_set_bg_color(&g_style_tab_btn_checked, lv_color_hex(0x8BA88E));
    lv_style_set_bg_opa(&g_style_tab_btn_checked, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_tab_btn_checked, lv_color_white());

    lv_style_init(&g_style_dropdown_list);
    lv_style_set_bg_color(&g_style_dropdown_list, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_dropdown_list, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_dropdown_list, 1);
    lv_style_set_border_color(&g_style_dropdown_list, lv_color_hex(0xD7E0D9));
    lv_style_set_radius(&g_style_dropdown_list, 12);
    lv_style_set_shadow_width(&g_style_dropdown_list, 12);
    lv_style_set_shadow_color(&g_style_dropdown_list, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_dropdown_list, 10);
    lv_style_set_text_color(&g_style_dropdown_list, lv_color_hex(0x2C3E50));

    lv_style_init(&g_style_dropdown_list_selected);
    lv_style_set_bg_color(&g_style_dropdown_list_selected, lv_color_hex(0x8BA88E));
    lv_style_set_bg_opa(&g_style_dropdown_list_selected, LV_OPA_COVER);
    lv_style_set_text_color(&g_style_dropdown_list_selected, lv_color_white());
    lv_style_set_radius(&g_style_dropdown_list_selected, 8);

    lv_style_init(&g_style_dropdown_button_rtl);
    lv_style_set_base_dir(&g_style_dropdown_button_rtl, LV_BASE_DIR_RTL);

    lv_style_init(&g_style_kpi_card);
    lv_style_set_bg_color(&g_style_kpi_card, lv_color_hex(0xF9FBFC));
    lv_style_set_bg_opa(&g_style_kpi_card, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_kpi_card, 1);
    lv_style_set_border_color(&g_style_kpi_card, lv_color_hex(0xE2E8EC));
    lv_style_set_radius(&g_style_kpi_card, 14);
    lv_style_set_shadow_width(&g_style_kpi_card, 10);
    lv_style_set_shadow_color(&g_style_kpi_card, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_kpi_card, 8);
    lv_style_set_pad_all(&g_style_kpi_card, 8);

    lv_style_init(&g_style_kpi_value);
    lv_style_set_text_color(&g_style_kpi_value, lv_color_hex(0x22313F));
    lv_style_set_text_align(&g_style_kpi_value, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_line_space(&g_style_kpi_value, 0);

    lv_style_init(&g_style_kpi_label);
    lv_style_set_text_color(&g_style_kpi_label, lv_color_hex(0x7F8C8D));
    lv_style_set_text_align(&g_style_kpi_label, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&g_style_detail_panel);
    lv_style_set_bg_color(&g_style_detail_panel, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&g_style_detail_panel, LV_OPA_COVER);
    lv_style_set_border_width(&g_style_detail_panel, 0);
    lv_style_set_radius(&g_style_detail_panel, 16);
    lv_style_set_shadow_width(&g_style_detail_panel, 14);
    lv_style_set_shadow_color(&g_style_detail_panel, lv_color_hex(0xD0D4DC));
    lv_style_set_shadow_opa(&g_style_detail_panel, 8);
    lv_style_set_pad_all(&g_style_detail_panel, 20);

    lv_style_init(&g_style_detail_title);
    lv_style_set_text_color(&g_style_detail_title, lv_color_hex(0x22313F));

    lv_style_init(&g_style_detail_key);
    lv_style_set_text_color(&g_style_detail_key, lv_color_hex(0x666666));

    lv_style_init(&g_style_detail_value);
    lv_style_set_text_color(&g_style_detail_value, lv_color_hex(0x333333));

    lv_style_init(&g_style_badge);
    lv_style_set_bg_color(&g_style_badge, lv_color_hex(0x8BA88E));
    lv_style_set_bg_opa(&g_style_badge, LV_OPA_COVER);
    lv_style_set_radius(&g_style_badge, 999);
    lv_style_set_pad_left(&g_style_badge, 10);
    lv_style_set_pad_right(&g_style_badge, 10);
    lv_style_set_pad_top(&g_style_badge, 4);
    lv_style_set_pad_bottom(&g_style_badge, 4);

    lv_style_init(&g_style_badge_text);
    lv_style_set_text_color(&g_style_badge_text, lv_color_white());

    g_styles_ready = 1U;
}

/*
 * 安全字符串拷贝封装
 *
 * 目标是统丢处理定长缓冲区写入，避免每次都手写截断辑
 * 该函数保证末尾始终补 '\0'，即使源字符串超长也不会越界
 */
static void wl_strcpy(char *dst, uint32_t dst_size, const char *src)
{
    if(dst_size == 0U) return;
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void wl_core_copy_snapshot_to_ui_cache(void)
{
    uint16_t i;
    uint16_t rows = g_core_snapshot.table_rows;

    memset(g_records, 0, sizeof(g_records));
    memset(g_filtered_idx, 0, sizeof(g_filtered_idx));
    g_filtered_count = 0U;

    if(rows > WL_MAX_FILTERED) rows = WL_MAX_FILTERED;

    for(i = 0; i < rows; i++) {
        const core_record_t *src = &g_core_snapshot.table_data[i];
        wildlife_record_t *dst = &g_records[i];
        wl_strcpy(dst->name, sizeof(dst->name), src->name);
        wl_strcpy(dst->category, sizeof(dst->category), src->category);
        wl_strcpy(dst->image, sizeof(dst->image), src->image);
        wl_strcpy(dst->level, sizeof(dst->level), src->level);
        wl_strcpy(dst->area, sizeof(dst->area), src->area);
        dst->quantity = (int)src->quantity;
        wl_strcpy(dst->status, sizeof(dst->status), src->status);
        wl_strcpy(dst->updated_at, sizeof(dst->updated_at), src->updated_at);
        dst->valid = src->valid;
        g_filtered_idx[i] = i;
        g_filtered_count++;
    }

    wl_strcpy(g_filter_opts, sizeof(g_filter_opts), g_core_snapshot.query.filter_options);
}

static void wl_core_render_from_snapshot(void)
{
    int16_t selected = -1;

    wl_core_copy_snapshot_to_ui_cache();
    wl_refresh_table();
    lv_obj_update_layout(g_ui.table);
    lv_obj_scroll_to_y(g_ui.table, 0, LV_ANIM_OFF);

    if(g_filtered_count > 0U) {
        if(g_core_snapshot.dashboard.selected_index >= 0 &&
           g_core_snapshot.dashboard.selected_index < (int16_t)g_filtered_count) {
            selected = (int16_t)g_filtered_idx[g_core_snapshot.dashboard.selected_index];
        }
        else {
            selected = (int16_t)g_filtered_idx[0];
        }
    }

    wl_set_selected_record(selected);
    wl_refresh_dashboard();
}

static void wl_core_event_cb(const char *text, void *user)
{
    (void)user;
    if(text == NULL) return;
    if(text[0] != '\0') {
        wl_set_report_text(text);
    }
}

static void wl_set_report_text(const char *text)
{
    const char *safe_text = (text != NULL) ? text : "";

    /* 这里同时维护“当前报告”和“历史日志”，这样用户既能看到最新反馈，也能回看之前发生过什么。 */
    if(g_ui.label_report != NULL) {
        lv_label_set_text(g_ui.label_report, safe_text);
    }

    if(safe_text[0] != '\0') {
        wl_append_log(safe_text);
    }

    if(g_ui.label_log_history != NULL) {
        lv_label_set_text(g_ui.label_log_history, g_log_history);
    }
}

static void wl_file_build_path(const char *name, char *out, uint32_t out_size)
{
    /* 当前目录状态保存在 g_file_path，这里只是把“目录 + 文件名”拼成 FatFs 可以直接识别的完整路径。 */
    if(strcmp(g_file_path, "0:/") == 0) snprintf(out, out_size, "0:/%s", name);
    else snprintf(out, out_size, "%s/%s", g_file_path, name);
}

static void wl_file_refresh(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    uint16_t i;
    char msg[128];

    /* 文件浏览器把 g_file_path 当作当前目录状态，刷新时要重新扫描目录并重建表格。 */
    g_file_count = 0;
    g_file_selected = -1;

    fr = f_opendir(&dir, g_file_path);
    if(fr != FR_OK) {
        snprintf(msg, sizeof(msg), "File: open dir failed (%d)", (int)fr);
        wl_set_report_text(msg);
        return;
    }

    while(g_file_count < WL_FILE_MAX_ITEMS) {
        fr = f_readdir(&dir, &fno);
        if(fr != FR_OK || fno.fname[0] == '\0') break;
        if(strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
        wl_strcpy(g_file_items[g_file_count].name, sizeof(g_file_items[g_file_count].name), fno.fname);
        g_file_items[g_file_count].is_dir = ((fno.fattrib & AM_DIR) != 0U) ? 1U : 0U;
        g_file_count++;
    }
    f_closedir(&dir);

    if(g_ui.file_path) lv_label_set_text(g_ui.file_path, g_file_path);
    if(g_ui.file_tip) lv_label_set_text(g_ui.file_tip, "Selected: -");

    if(g_ui.file_table) {
        lv_table_set_col_cnt(g_ui.file_table, 2);
        lv_table_set_col_width(g_ui.file_table, 0, 56);
        lv_table_set_col_width(g_ui.file_table, 1, 236);
        lv_table_set_row_cnt(g_ui.file_table, g_file_count + 1U);
        lv_table_set_cell_value(g_ui.file_table, 0, 0, "Type");
        lv_table_set_cell_value(g_ui.file_table, 0, 1, "Name");
        for(i = 0; i < g_file_count; i++) {
            lv_table_set_cell_value(g_ui.file_table, i + 1U, 0, g_file_items[i].is_dir ? "DIR" : "FILE");
            lv_table_set_cell_value(g_ui.file_table, i + 1U, 1, g_file_items[i].name);
            lv_table_add_cell_ctrl(g_ui.file_table, i + 1U, 0, LV_TABLE_CELL_CTRL_TEXT_CROP);
            lv_table_add_cell_ctrl(g_ui.file_table, i + 1U, 1, LV_TABLE_CELL_CTRL_TEXT_CROP);
        }
        lv_obj_update_layout(g_ui.file_table);
        lv_obj_scroll_to_y(g_ui.file_table, 0, LV_ANIM_OFF);
    }

    snprintf(msg, sizeof(msg), "File: %u items", (unsigned)g_file_count);
    wl_set_report_text(msg);
}

static void wl_file_up(void)
{
    size_t len;
    if(strcmp(g_file_path, "0:/") == 0) {
        wl_set_report_text("File: already at root");
        return;
    }

    /* 这里要回退到上一级目录边界，而不是简单删掉最后一个字符，否则路径会变成非法字符串。 */
    len = strlen(g_file_path);
    while(len > 3U && g_file_path[len - 1U] != '/') len--;
    if(len > 3U) len--;
    g_file_path[len] = '\0';
    wl_file_refresh();
}

static void wl_file_open_selected(void)
{
    char full[128];
    if(g_file_selected < 0 || g_file_selected >= (int16_t)g_file_count) {
        wl_set_report_text("File: no item selected");
        return;
    }

    /* 目录和普通文件的交互语义不同：目录进入下一层，普通文件只给提示，不强行进入。 */
    if(!g_file_items[g_file_selected].is_dir) {
        char msg[128];
        snprintf(msg, sizeof(msg), "File: selected -> %s", g_file_items[g_file_selected].name);
        wl_set_report_text(msg);
        return;
    }

    wl_file_build_path(g_file_items[g_file_selected].name, full, sizeof(full));
    wl_strcpy(g_file_path, sizeof(g_file_path), full);
    wl_file_refresh();
}

static void wl_file_delete_selected(void)
{
    char full[128];
    char msg[128];
    FRESULT fr;

    if(g_file_selected < 0 || g_file_selected >= (int16_t)g_file_count) {
        wl_set_report_text("File: no item selected");
        return;
    }

    /* 先拼出完整路径，再调用 FatFs 删除接口，这样删除的是当前目录下的真实目标。 */
    wl_file_build_path(g_file_items[g_file_selected].name, full, sizeof(full));
    fr = f_unlink(full);
    if(fr == FR_OK) {
        snprintf(msg, sizeof(msg), "File: deleted -> %s", g_file_items[g_file_selected].name);
        wl_set_report_text(msg);
        wl_file_refresh();
    }
    else {
        snprintf(msg, sizeof(msg), "File: delete failed (%d)", (int)fr);
        wl_set_report_text(msg);
    }
}

static void wl_file_action_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t action = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if(code != LV_EVENT_CLICKED) return;

    /* action 来自按钮创建时传入的 user_data，用来把同一个回调分发成四种文件操作。 */
    if(action == 0U) wl_file_up();
    else if(action == 1U) wl_file_refresh();
    else if(action == 2U) wl_file_open_selected();
    else if(action == 3U) wl_file_delete_selected();
}

static void wl_file_table_event_cb(lv_event_t *e)
{
    uint16_t row;
    uint16_t col;
    lv_event_code_t code = lv_event_get_code(e);
    (void)e;

    if(g_ui.file_table == NULL) return;
    lv_table_get_selected_cell(g_ui.file_table, &row, &col);
    (void)col;
    /* 第 0 行是表头，所以真正的数据行要减 1 后再映射回 g_file_items。 */
    if(row == 0U || row > g_file_count) return;
    g_file_selected = (int16_t)(row - 1U);

    if(g_ui.file_tip) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Selected: [%s] %s", g_file_items[g_file_selected].is_dir ? "DIR" : "FILE", g_file_items[g_file_selected].name);
        lv_label_set_text(g_ui.file_tip, msg);
    }

    if(code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_CLICKED) {
        /* no-op: selection text updated above */
    }
}

static const char *wl_mode_title(uint16_t mode)
{
    /* 下拉框保存的是数值索引，这里把索引翻译成更适合导出文件名的英文短标识。 */
    if(mode == 1U) return "ByLevel";
    if(mode == 2U) return "ByArea";
    return "ByCategory";
}

static void wl_make_safe_token(const char *src, char *dst, uint32_t dst_size, const char *fallback)
{
    uint32_t i = 0;
    uint32_t j = 0;
    const char *safe = (src != NULL && src[0] != '\0') ? src : fallback;

    if(dst_size == 0U) return;
    if(safe == NULL) safe = "NA";

    /* 导出路径不能包含太多特殊字符，所以这里把原始文本压成只保留安全字符的 token。 */
    while(safe[i] != '\0' && j + 1U < dst_size) {
        char c = safe[i++];
        uint8_t keep = 0U;

        if((c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-') {
            keep = 1U;
        }

        if(keep) {
            dst[j++] = c;
        }
        else if(c == ' ' || c == '\t' || c == '.') {
            if(j == 0U || dst[j - 1U] != '_') dst[j++] = '_';
        }
        else {
            if(j == 0U || dst[j - 1U] != '_') dst[j++] = '_';
        }
    }

    if(j == 0U) {
        wl_strcpy(dst, dst_size, fallback != NULL ? fallback : "NA");
        return;
    }

    while(j > 1U && dst[j - 1U] == '_') j--;
    dst[j] = '\0';
}

static void wl_build_export_path(char *out, uint32_t out_size)
{
    char filter_raw[32];
    char mode_tok[20];
    char filter_tok[32];
    char name_tok[32];
    char base[120];
    char candidate[128];
    const char *name_key = "";
    uint16_t mode = 0;
    uint16_t seq = 0;
    FILINFO fno;

    if(out == NULL || out_size == 0U) return;

    /* 导出文件名把当前查询上下文编码进去，这样用户一看名字就能知道它是按什么条件导出的。 */
    if(g_ui.dd_mode != NULL) mode = lv_dropdown_get_selected(g_ui.dd_mode);
    wl_make_safe_token(wl_mode_title(mode), mode_tok, sizeof(mode_tok), "ByCategory");

    filter_raw[0] = '\0';
    if(g_ui.dd_filter != NULL) {
        lv_dropdown_get_selected_str(g_ui.dd_filter, filter_raw, sizeof(filter_raw));
    }
    wl_make_safe_token(filter_raw, filter_tok, sizeof(filter_tok), "ALL");

    if(g_ui.ta_name != NULL) {
        name_key = lv_textarea_get_text(g_ui.ta_name);
    }
    wl_make_safe_token(name_key, name_tok, sizeof(name_tok), "ALL");

    snprintf(base,
             sizeof(base),
             "0:/wildlife_export_%s_%s_(%s)",
             mode_tok,
             filter_tok,
             name_tok);

    snprintf(candidate, sizeof(candidate), "%s.csv", base);
    /* 若目标文件已存在，就递增序号，避免覆盖历史导出结果。 */
    while(f_stat(candidate, &fno) == FR_OK && seq < 999U) {
        seq++;
        snprintf(candidate, sizeof(candidate), "%s_%u.csv", base, (unsigned)seq);
    }

    wl_strcpy(out, out_size, candidate);
}

static const char *wl_csv_read_field(const char *src, char *dst, uint32_t dst_size, uint8_t *ok)
{
    const char *p = src;
    uint32_t i = 0;
    uint8_t quoted = 0U;

    if(ok != NULL) *ok = 0U;
    if(src == NULL || dst == NULL || dst_size == 0U) return src;

    /* 支持带引号的 CSV 字段，因为图片路径这类字段里可能包含逗号，不能直接按逗号切分。 */
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
            if(*p == '\r' || *p == '\n') {
                break;
            }
        }

        if(i + 1U < dst_size) dst[i++] = *p;
        p++;
    }

    dst[i] = '\0';
    if(ok != NULL) *ok = 1U;
    return p;
}

static uint8_t wl_is_updated_at_valid(const char *text)
{
    uint8_t i;

    /* 更新时间字段采用固定格式 YY/MM/DD/HH/MM，先看长度，再逐个校验分隔符位置。 */
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

/*
 * 将状态字符串映射到下拉框索引
 *
 * 这个函数让编辑页能够在加载已有记录时，恢复正确的状项
 */
static int wl_status_to_index(const char *status)
{
    /* 编辑页下拉框需要稳定索引，因此这里把字符串状态映射回固定的菜单位置。 */
    if(strcmp(status, "stable") == 0) return 0;
    if(strcmp(status, "recovering") == 0) return 1;
    if(strcmp(status, "decreasing") == 0) return 2;
    if(strcmp(status, "critical") == 0) return 3;
    if(strcmp(status, "extinct") == 0) return 4;
    return 0;
}

/*
 * 按查询模式提取当前应该比较的字段
 *
 * 例如
 * - mode=0 -> category
 * - mode=1 -> level
 * - mode=2 -> area
 *
 * 这个封装让过滤逻辑只关心比较什么字段，不关心具体字段来源。
 */
static const char *wl_field_by_mode(const wildlife_record_t *rec, uint16_t mode)
{
    /* 查询模式只决定“比较哪个字段”，这样过滤逻辑就可以复用同一套代码。 */
    if(mode == 1U) return rec->level;
    if(mode == 2U) return rec->area;
    return rec->category;
}

/*
 * 寻找可用空槽位
 *
 * 新增记录时优先复用已删除的位置，而不是盲目追加，
 * 这样能保持固定上限数组的利用率，并避免分配额外内存
 */
static int wl_find_free_slot(void)
{
    int i;
    /* 软删除后留下的位置会再次被新记录复用，这里就是固定数组的空槽回收机制。 */
    for(i = 0; i < WL_MAX_RECORDS; i++) {
        if(!g_records[i].valid) return i;
    }
    return -1;
}

/*
 * 不区分大小写的子串匹配
 *
 * 用于“Name keyword”搜索框，允许用户输入部分关键字来模糊查询名称
 */
static int wl_contains_nocase(const char *text, const char *key)
{
    uint32_t i;
    uint32_t j;
    uint32_t tlen = (uint32_t)strlen(text);
    uint32_t klen = (uint32_t)strlen(key);
    if(klen == 0U) return 1;
    if(tlen < klen) return 0;

    /* 这里用最朴素的大小写折叠比较实现模糊搜索，适合嵌入式场景下的轻量关键字匹配。 */
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

/*
 * 将运行中的记录库恢复为默认数据集
 *
 * 先整体清零，再条拷贝默认记录，最后显式置 valid=1
 * 这样可以避免残留脏数据影响后续统计和显示
 */
static void wl_reset_defaults(void)
{
    uint32_t i;
    /* 先整体清零，再拷贝默认记录，这样能避免残留脏数据影响后续统计和显示。 */
    memset(g_records, 0, sizeof(g_records));
    for(i = 0; i < (uint32_t)(sizeof(g_default_records) / sizeof(g_default_records[0])) && i < WL_MAX_RECORDS; i++) {
        g_records[i] = g_default_records[i];
        g_records[i].valid = 1;
    }
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

    /* CSV 读取必须和导出格式一一对应：字段顺序不能变，每个字符串都要保留宽度限制，避免异常长行破坏栈空间。 */

    if(line == NULL || out == NULL || line[0] == '\0') return 0;

    p = wl_csv_read_field(p, name, sizeof(name), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, cat, sizeof(cat), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, img, sizeof(img), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, level, sizeof(level), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, area, sizeof(area), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, qty_text, sizeof(qty_text), &ok);
    if(!ok) return 0;
    p = wl_csv_read_field(p, status, sizeof(status), &ok);
    if(!ok) return 0;
    (void)wl_csv_read_field(p, updated, sizeof(updated), &ok);
    if(!ok) return 0;

    qty = strtol(qty_text, &endptr, 10);
    if(endptr == qty_text || *endptr != '\0') return 0;
    if(qty > 2147483647L || qty < -2147483647L - 1L) return 0;

    if(strcmp(name, "name") == 0 && strcmp(cat, "category") == 0) return 0;

    memset(out, 0, sizeof(*out));
    wl_strcpy(out->name, sizeof(out->name), name);
    wl_strcpy(out->category, sizeof(out->category), cat);
    wl_strcpy(out->image, sizeof(out->image), img);
    wl_strcpy(out->level, sizeof(out->level), level);
    wl_strcpy(out->area, sizeof(out->area), area);
    out->quantity = (int)qty;
    wl_strcpy(out->status, sizeof(out->status), status);
    if(updated[0] == '\0') wl_strcpy(out->updated_at, sizeof(out->updated_at), WL_DEFAULT_UPDATED_AT);
    else wl_strcpy(out->updated_at, sizeof(out->updated_at), updated);
    out->valid = 1;
    return 1;
}

static uint8_t wl_load_db(void)
{
    /*
    * 从 SD 卡一次性读取 CSV，再逐行解析到 `g_records`。
    * 适合当前记录量不大的演示场景，逻辑简单、状态清晰。
     */
    FIL file;
    FRESULT fr;
    UINT br = 0;
    char buf[4096];
    char *p;
    uint16_t loaded = 0;

    fr = f_open(&file, WL_DB_FILE, FA_READ);
    if(fr != FR_OK) return 0;

    /* 先一次性读入内存，再按行切分，能减少反复文件访问，也更容易一次性校验整份 CSV。 */
    fr = f_read(&file, buf, sizeof(buf) - 1U, &br);
    f_close(&file);
    if(fr != FR_OK) return 0;

    buf[br] = '\0';
    memset(g_records, 0, sizeof(g_records));

    p = buf;
    while(*p != '\0') {
        char *eol;
        wildlife_record_t rec;
        int slot;

        eol = strchr(p, '\n');
        if(eol != NULL) {
            *eol = '\0';
            if(eol > p && *(eol - 1) == '\r') *(eol - 1) = '\0';
        }

        if(*p != '\0' && wl_parse_line(p, &rec)) {
            slot = wl_find_free_slot();
            if(slot >= 0) {
                g_records[slot] = rec;
                g_records[slot].valid = 1;
                loaded++;
            }
        }

        if(eol == NULL) break;
        p = eol + 1;
    }

    return (loaded > 0U) ? 1U : 0U;
}

static uint8_t wl_write_file(const char *path, uint8_t filtered_only)
{
    /*
    * 根据标志输出两种文件：
    * - filtered_only=0：保存完整有效库；
    * - filtered_only=1：仅导出当前过滤结果，便于单独分析。
     */
    FIL file;
    FRESULT fr;
    UINT bw;
    uint16_t i;

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if(fr != FR_OK) return 0;

    /* 两种导出模式共用同一写文件框架，只是写入的数据源不同：全部有效记录或者当前过滤结果。 */
    if(filtered_only) {
        for(i = 0; i < g_filtered_count; i++) {
            int idx = (int)g_filtered_idx[i];
            wildlife_record_t *rec = &g_records[idx];
            char line[256];
            int len = snprintf(line,
                               sizeof(line),
                               "%s,%s,%s,%s,%s,%d,%s,%s\r\n",
                               rec->name,
                               rec->category,
                               rec->image,
                               rec->level,
                               rec->area,
                               rec->quantity,
                               rec->status,
                               rec->updated_at);
            if(len > 0) {
                fr = f_write(&file, line, (UINT)len, &bw);
                if(fr != FR_OK || bw != (UINT)len) {
                    f_close(&file);
                    return 0;
                }
            }
        }
    }
    else {
        for(i = 0; i < WL_MAX_RECORDS; i++) {
            wildlife_record_t *rec = &g_records[i];
            char line[256];
            int len;
            if(!rec->valid) continue;

            len = snprintf(line,
                           sizeof(line),
                           "%s,%s,%s,%s,%s,%d,%s,%s\r\n",
                           rec->name,
                           rec->category,
                           rec->image,
                           rec->level,
                           rec->area,
                           rec->quantity,
                           rec->status,
                           rec->updated_at);
            if(len > 0) {
                fr = f_write(&file, line, (UINT)len, &bw);
                if(fr != FR_OK || bw != (UINT)len) {
                    f_close(&file);
                    return 0;
                }
            }
        }
    }

    f_close(&file);
    return 1;
}

static void wl_release_image_buffer(void)
{
    /* 这里清掉的是图片路径状态；真正的图片对象由 LVGL 管理，后续重新选中时再覆盖即可。 */
    g_image_path[0] = '\0';
}

static uint8_t wl_build_sd_path(const char *image_field, char *out, uint32_t out_size)
{
    /* 图片路径既可能是绝对路径，也可能只是文件名。 */
    if(image_field == NULL || out == NULL || out_size == 0U) return 0;
    /* 已经带盘符的路径直接复用，不要重复加 0:/；只有纯文件名才补齐成 SD 卡路径。 */
    if(strchr(image_field, ':') != NULL) {
        wl_strcpy(out, out_size, image_field);
    }
    else {
        snprintf(out, out_size, "0:/%s", image_field);
    }
    return 1;
}

static void wl_show_selected_image(void)
{
    wildlife_record_t *rec;
    char msg[128];

    /* 没有有效选中项时不显示图片层，避免空图层打断主流程。 */
    if(g_ui.image_layer == NULL || g_ui.image_view == NULL || g_ui.label_image_tip == NULL) return;
    if(g_selected_idx < 0 || g_selected_idx >= WL_MAX_RECORDS || !g_records[g_selected_idx].valid) {
        return;
    }

    /* 图片预览依赖当前选中记录，所以这里先从 g_selected_idx 找到对应档案，再把浮层提到最前面。 */
    rec = &g_records[g_selected_idx];
    lv_obj_clear_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_ui.image_layer);

    wl_release_image_buffer();
    if(wl_build_sd_path(rec->image, g_image_path, sizeof(g_image_path))) {
        lv_img_set_src(g_ui.image_view, g_image_path);
        lv_obj_set_size(g_ui.image_view, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(g_ui.image_view);
        lv_obj_clear_flag(g_ui.image_view, LV_OBJ_FLAG_HIDDEN);
        snprintf(msg, sizeof(msg), "Image: %s (tap image to return)", rec->name);
        lv_label_set_text(g_ui.label_image_tip, msg);
        snprintf(msg, sizeof(msg), "Report: image loaded from %s", g_image_path);
        wl_set_report_text(msg);
    }
    else {
        lv_obj_add_flag(g_ui.image_view, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_ui.label_image_tip, "Image load failed: empty image path");
        wl_set_report_text("Image load failed: empty image path");
    }
}

static void wl_hide_image_layer(void)
{
    /* 图片层是覆盖层，隐藏时只需要收起它本身即可。 */
    if(g_ui.image_layer == NULL) return;
    lv_obj_add_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
}

/*
 * 把当前中的记录加载到编辑表单中
 *
 * idx 无效时，表单被清空，表示用户处于“新建记录模式
 * idx 有效时，表单会回填已有数据，方便修改后再提交
 */
static void wl_fill_edit_form(int16_t idx)
{
    /*
     * 编辑页是“新建 / 修改”共用表单：
     * - idx 无效 => 清空表单，进入新建模式；
     * - idx 有效 => 回填现有数据，进入修改模式。
     */
    if(idx < 0 || idx >= WL_MAX_RECORDS || !g_records[idx].valid) {
        /* 无选中项时把表单清空，表示编辑页处于“新建记录”状态。 */
        lv_textarea_set_text(g_ui.ta_name_edit, "");
        lv_textarea_set_text(g_ui.ta_cat_edit, "");
        lv_textarea_set_text(g_ui.ta_img_edit, "");
        lv_textarea_set_text(g_ui.ta_level_edit, "");
        lv_textarea_set_text(g_ui.ta_area_edit, "");
        lv_textarea_set_text(g_ui.ta_qty_edit, "0");
        if(g_ui.ta_updated_edit) lv_textarea_set_text(g_ui.ta_updated_edit, WL_DEFAULT_UPDATED_AT);
        lv_dropdown_set_selected(g_ui.dd_status_edit, 0);
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: New record");
        return;
    }

    {
        wildlife_record_t *rec = &g_records[idx];
        char qty[16];
        /* 有选中项时回填原始字段，这样用户修改时可以在现有数据基础上直接编辑。 */
        snprintf(qty, sizeof(qty), "%d", rec->quantity);
        lv_textarea_set_text(g_ui.ta_name_edit, rec->name);
        lv_textarea_set_text(g_ui.ta_cat_edit, rec->category);
        lv_textarea_set_text(g_ui.ta_img_edit, rec->image);
        lv_textarea_set_text(g_ui.ta_level_edit, rec->level);
        lv_textarea_set_text(g_ui.ta_area_edit, rec->area);
        lv_textarea_set_text(g_ui.ta_qty_edit, qty);
        if(g_ui.ta_updated_edit) {
            if(rec->updated_at[0] != '\0') lv_textarea_set_text(g_ui.ta_updated_edit, rec->updated_at);
            else lv_textarea_set_text(g_ui.ta_updated_edit, WL_DEFAULT_UPDATED_AT);
        }
        lv_dropdown_set_selected(g_ui.dd_status_edit, (uint16_t)wl_status_to_index(rec->status));
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: Existing record");
    }
}

/*
 * 更新右侧详情标签
 *
 * 详情区用于展示当前表格中项的摘要信息，属于中即预览的交互模式
 */
static void wl_update_detail_label(void)
{
    /* 详情区始终展示当前选中记录的摘要，未选中时使用占位符维持版面。 */
    if(g_selected_idx < 0 || g_selected_idx >= WL_MAX_RECORDS || !g_records[g_selected_idx].valid) {
        if(g_ui.label_detail) lv_label_set_text(g_ui.label_detail, "Detail: no selection");
        if(g_ui.label_detail_name) lv_label_set_text(g_ui.label_detail_name, "-");
        if(g_ui.label_detail_category) lv_label_set_text(g_ui.label_detail_category, "-");
        if(g_ui.label_detail_image) lv_label_set_text(g_ui.label_detail_image, "-");
        if(g_ui.label_detail_level) lv_label_set_text(g_ui.label_detail_level, "-");
        if(g_ui.label_detail_area) lv_label_set_text(g_ui.label_detail_area, "-");
        if(g_ui.label_detail_qty) lv_label_set_text(g_ui.label_detail_qty, "-");
        if(g_ui.label_detail_status) lv_label_set_text(g_ui.label_detail_status, "-");
        if(g_ui.label_detail_updated) lv_label_set_text(g_ui.label_detail_updated, WL_DEFAULT_UPDATED_AT);
        if(g_ui.label_detail_valid) lv_label_set_text(g_ui.label_detail_valid, "-");
        return;
    }

    {
        wildlife_record_t *rec = &g_records[g_selected_idx];
        /* 这里把同一条记录拆成多个字段显示，目的是让用户在右侧就能快速核对档案摘要。 */
        if(g_ui.label_detail) lv_label_set_text(g_ui.label_detail, rec->name);
        if(g_ui.label_detail_name) lv_label_set_text(g_ui.label_detail_name, rec->name);
        if(g_ui.label_detail_category) lv_label_set_text(g_ui.label_detail_category, rec->category);
        if(g_ui.label_detail_image) lv_label_set_text(g_ui.label_detail_image, rec->image);
        if(g_ui.label_detail_level) lv_label_set_text(g_ui.label_detail_level, rec->level);
        if(g_ui.label_detail_area) lv_label_set_text(g_ui.label_detail_area, rec->area);
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d", rec->quantity);
            if(g_ui.label_detail_qty) lv_label_set_text(g_ui.label_detail_qty, buf);
        }
        if(g_ui.label_detail_status) lv_label_set_text(g_ui.label_detail_status, wl_status_title(rec->status));
        if(g_ui.label_detail_updated) {
            if(rec->updated_at[0] != '\0') lv_label_set_text(g_ui.label_detail_updated, rec->updated_at);
            else lv_label_set_text(g_ui.label_detail_updated, WL_DEFAULT_UPDATED_AT);
        }
        if(g_ui.label_detail_valid) lv_label_set_text(g_ui.label_detail_valid, rec->valid ? "1" : "0");
    }
}

/*
 * 统一设置当前选中的记录
 *
 * 这里除了更新选中索引，还会联动刷新详情区和编辑表单，保证界面三处状同步
 */
static void wl_set_selected_record(int16_t idx)
{
    g_selected_idx = idx;
    if(idx < 0) wl_hide_image_layer();

    /* 选中状态变化后，详情、编辑表单和图片层都要同步刷新。 */
    wl_update_detail_label();
    wl_fill_edit_form(idx);
}

/*
 * 统计当前有效记录总数
 *
 * 逻辑上这是数据层”的基础计数，后续仪表盘、记录数和使用率都依赖它
 */
static uint16_t wl_count_valid_records(void)
{
    uint16_t i;
    uint16_t cnt = 0;

    /* 只统计 valid=1 的记录，软删除项不参与业务指标。 */
    for(i = 0; i < WL_MAX_RECORDS; i++) {
        if(g_records[i].valid) cnt++;
    }
    return cnt;
}

static uint8_t wl_status_bucket(const char *status)
{
    /* 把字符串状态映射到固定桶位，方便做统计和饼图绘制。 */
    if(strcmp(status, "critical") == 0) return 0U;
    if(strcmp(status, "recovering") == 0) return 1U;
    if(strcmp(status, "decreasing") == 0) return 2U;
    if(strcmp(status, "stable") == 0) return 3U;
    return 4U;
}

static void wl_canvas_fill_sector(lv_obj_t *canvas, int32_t start_deg, int32_t sweep_deg, lv_color_t color)
{
    lv_draw_arc_dsc_t arc;
    int32_t end_deg;
    int32_t seg_start;
    int32_t seg_end;
    lv_coord_t r;

    if(sweep_deg <= 0) return;

    while(start_deg < 0) start_deg += 360;
    while(start_deg >= 360) start_deg -= 360;

    end_deg = start_deg + sweep_deg - 1;
    lv_draw_arc_dsc_init(&arc);
    arc.color = color;
    arc.width = 1;

    /* 通过多层同心圆弧叠加，近似填满扇形区域。 */
    for(r = 1; r <= WL_PIE_RADIUS; r++) {
        if(end_deg < 360) {
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, start_deg, end_deg, &arc);
        }
        else {
            seg_start = start_deg;
            seg_end = 359;
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, seg_start, seg_end, &arc);
            seg_start = 0;
            seg_end = end_deg % 360;
            lv_canvas_draw_arc(canvas, WL_PIE_CANVAS_W / 2, WL_PIE_CANVAS_H / 2, r, seg_start, seg_end, &arc);
        }
    }
}

static void wl_refresh_status_pie(void)
{
    uint16_t count[WL_STATUS_BUCKETS] = {0};
    uint16_t percent[WL_STATUS_BUCKETS] = {0};
    uint16_t rem[WL_STATUS_BUCKETS] = {0};
    uint16_t i;
    uint16_t total = 0;
    uint16_t sum_percent = 0;
    uint16_t remain;
    int32_t start_deg = -90;
    int32_t used_deg = 0;
    int32_t active_last = -1;
    char line[40];

    /* 先统计每个状态出现次数，再换算成整数百分比。 */
    for(i = 0; i < g_filtered_count; i++) {
        wildlife_record_t *rec = &g_records[g_filtered_idx[i]];
        uint8_t b = wl_status_bucket(rec->status);
        count[b]++;
    }
    /*
     * 先把数量换算成整数百分比，再用“最大余数分配法”把总和补齐到 100%。
     * 这样既满足整数展示要求，也能避免四舍五入误差累积。
     */

    for(i = 0; i < WL_STATUS_BUCKETS; i++) {
        total = (uint16_t)(total + count[i]);
    }

    if(total > 0U) {
        /* 先取整，再把剩余百分比按最大余数补回去，确保最后总和一定是 100%。 */
        for(i = 0; i < WL_STATUS_BUCKETS; i++) {
            uint32_t mul = (uint32_t)count[i] * 100U;
            percent[i] = (uint16_t)(mul / total);
            rem[i] = (uint16_t)(mul % total);
            sum_percent = (uint16_t)(sum_percent + percent[i]);
        }

        remain = (uint16_t)(100U - sum_percent);
        while(remain > 0U) {
            uint8_t best = 0U;
            for(i = 1; i < WL_STATUS_BUCKETS; i++) {
                if(rem[i] > rem[best]) best = (uint8_t)i;
            }
            percent[best]++;
            rem[best] = 0U;
            remain--;
        }
    }

    if(g_ui.pie_canvas != NULL) {
        lv_draw_label_dsc_t text_dsc;
        /*
         * 画布绘制顺序：
         * 1) 清空背景；
         * 2) 逐个状态填充扇区；
         * 3) 在扇区中部写黑色文本（0% 不绘制）。
         */
        lv_canvas_fill_bg(g_ui.pie_canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        lv_draw_label_dsc_init(&text_dsc);
        text_dsc.color = lv_color_hex(0x000000);
        text_dsc.align = LV_TEXT_ALIGN_CENTER;

        for(i = 0; i < WL_STATUS_BUCKETS; i++) {
            if(percent[i] > 0U) active_last = i;
        }

        /* 最后一个扇区单独吃掉剩余角度，避免整数除法累积出一小段空白。 */
        for(i = 0; i < WL_STATUS_BUCKETS; i++) {
            int32_t sweep_deg;
            int32_t mid_deg;
            uint8_t dir_idx;
            int32_t tx;
            int32_t ty;
            if(percent[i] == 0U) continue;

            if((int32_t)i == active_last) sweep_deg = 360 - used_deg;
            else sweep_deg = ((int32_t)percent[i] * 360) / 100;

            if(sweep_deg <= 0) sweep_deg = 1;
            wl_canvas_fill_sector(g_ui.pie_canvas, start_deg, sweep_deg, lv_color_hex(g_status_colors_hex[i]));

            mid_deg = start_deg + (sweep_deg / 2);
            while(mid_deg < 0) mid_deg += 360;
            while(mid_deg >= 360) mid_deg -= 360;
            dir_idx = (uint8_t)(((uint32_t)mid_deg + 15U) / 30U);
            if(dir_idx >= 12U) dir_idx = 0U;

            /* 文本锚点落在半径约 62% 位置，尽量贴近扇区中部。 */
            snprintf(line, sizeof(line), "%s/%u%%", g_status_titles[i], (unsigned)percent[i]);
            tx = (WL_PIE_CANVAS_W / 2) + ((WL_PIE_RADIUS * 62 / 100) * g_cos30_x1000[dir_idx]) / 1000 - 44;
            ty = (WL_PIE_CANVAS_H / 2) + ((WL_PIE_RADIUS * 62 / 100) * g_sin30_x1000[dir_idx]) / 1000 - 10;
            lv_canvas_draw_text(g_ui.pie_canvas, (lv_coord_t)tx, (lv_coord_t)ty, 88, &text_dsc, line);

            start_deg += sweep_deg;
            used_deg += sweep_deg;
        }
    }

    /* 画布内已有文字，外部图例隐藏，避免重复信息。 */
    /* 外层图例不再显示，避免与扇区内文字重复。 */
    for(i = 0; i < WL_STATUS_BUCKETS; i++) {
        if(g_ui.pie_legend[i] == NULL) continue;
        lv_obj_add_flag(g_ui.pie_legend[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * 刷新右上角的仪表盘与统计卡片
 *
 * 这里把过滤结果转换成几个用户更容易理解的指标
 * - Power Usage：过滤结果占总有效记录的比例
 * - Records：当前命中数 / 总有效数
 * - Risk：处critical/extinct 状的数量
 * - Trend：decreasing 状数量以及数量之和
 */
static void wl_refresh_dashboard(void)
{
    uint16_t i;
    uint16_t valid_cnt = wl_count_valid_records();
    uint16_t critical_cnt = 0;
    uint16_t down_cnt = 0;
    long total_qty = 0;
    char buf[96];

    /* Dashboard 统计只依赖过滤结果，不重新扫描全量库，这样刷新代价小，也能和表格保持同一视图来源。 */
    for(i = 0; i < g_filtered_count; i++) {
        wildlife_record_t *rec = &g_records[g_filtered_idx[i]];
        total_qty += rec->quantity;
        if(strcmp(rec->status, "critical") == 0 || strcmp(rec->status, "extinct") == 0) critical_cnt++;
        if(strcmp(rec->status, "decreasing") == 0) down_cnt++;
    }

    /* 三个卡片分别对应“命中数”“高风险数”“下降趋势数”，让用户不用读完整表也能先看大方向。 */
    if(g_ui.label_card_records) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)g_filtered_count);
        lv_label_set_text(g_ui.label_card_records, buf);
    }

    if(g_ui.label_card_risk) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)critical_cnt);
        lv_label_set_text(g_ui.label_card_risk, buf);
    }

    if(g_ui.label_card_trend) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)down_cnt);
        lv_label_set_text(g_ui.label_card_trend, buf);
    }

    if(g_ui.label_report) {
        snprintf(buf, sizeof(buf), "Summary: %u records, qty=%ld, valid=%u", (unsigned)g_filtered_count, total_qty, (unsigned)valid_cnt);
        wl_set_report_text(buf);
    }

    /* 饼图依赖同一批过滤结果，所以统计卡片更新后要立刻同步刷新状态占比图。 */
    wl_refresh_status_pie();
}

/*
 * 刷新表格内容
 *
 * 表格0 行作为表头，后续行对应过滤后的结果集
 * 这里明确设置列宽crop 控制，是为了避免长文本把表格布局撑乱
 */
static void wl_refresh_table(void)
{
    uint16_t i;
    /* 表格第 0 行固定作为表头，正文从 1 开始。 */
    lv_table_set_col_cnt(g_ui.table, 7);
    lv_table_set_col_width(g_ui.table, 0, 34);
    lv_table_set_col_width(g_ui.table, 1, 120);
    lv_table_set_col_width(g_ui.table, 2, 90);
    lv_table_set_col_width(g_ui.table, 3, 70);
    lv_table_set_col_width(g_ui.table, 4, 90);
    lv_table_set_col_width(g_ui.table, 5, 52);
    lv_table_set_col_width(g_ui.table, 6, 84);
    lv_table_set_row_cnt(g_ui.table, g_filtered_count + 1U);

    lv_table_set_cell_value(g_ui.table, 0, 0, "ID");
    lv_table_set_cell_value(g_ui.table, 0, 1, "Name");
    lv_table_set_cell_value(g_ui.table, 0, 2, "Category");
    lv_table_set_cell_value(g_ui.table, 0, 3, "Level");
    lv_table_set_cell_value(g_ui.table, 0, 4, "Area");
    lv_table_set_cell_value(g_ui.table, 0, 5, "Qty");
    lv_table_set_cell_value(g_ui.table, 0, 6, "Status");
    lv_table_add_cell_ctrl(g_ui.table, 0, 0, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 1, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 2, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 3, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 4, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 5, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_table_add_cell_ctrl(g_ui.table, 0, 6, LV_TABLE_CELL_CTRL_TEXT_CROP);

    /* 表格每次都完全重建，避免旧结果残留在滚动区里造成“看起来像没刷新”的错觉。 */
    for(i = 0; i < g_filtered_count; i++) {
        char id_buf[8];
        char qty_buf[16];
        wildlife_record_t *rec = &g_records[g_filtered_idx[i]];

        /* 表格只是过滤结果的投影，不保存独立数据，所以每次刷新都要从 g_filtered_idx 重新取值。 */
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)(i + 1U));
        snprintf(qty_buf, sizeof(qty_buf), "%d", rec->quantity);

        lv_table_set_cell_value(g_ui.table, i + 1U, 0, id_buf);
        lv_table_set_cell_value(g_ui.table, i + 1U, 1, rec->name);
        lv_table_set_cell_value(g_ui.table, i + 1U, 2, rec->category);
        lv_table_set_cell_value(g_ui.table, i + 1U, 3, rec->level);
        lv_table_set_cell_value(g_ui.table, i + 1U, 4, rec->area);
        lv_table_set_cell_value(g_ui.table, i + 1U, 5, qty_buf);
        lv_table_set_cell_value(g_ui.table, i + 1U, 6, rec->status);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 0, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 1, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 2, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 3, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 4, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 5, LV_TABLE_CELL_CTRL_TEXT_CROP);
        lv_table_add_cell_ctrl(g_ui.table, i + 1U, 6, LV_TABLE_CELL_CTRL_TEXT_CROP);
    }

}

/*
 * 根据当前模式重建过滤候项
 *
 * 例如：按类别查询时，过滤下拉框会列出扢有出现过的类别，并附ALL 选项
 * 该函数会去重，并确保选项列表字符串不会溢出
 */
static void wl_update_filter_options(void)
{
    uint16_t mode = lv_dropdown_get_selected(g_ui.dd_mode);

    if(interaction_shell_set_mode((core_mode_t)mode, &g_core_snapshot) != CORE_OK) return;

    wl_strcpy(g_filter_opts, sizeof(g_filter_opts), g_core_snapshot.query.filter_options);

    lv_dropdown_set_options(g_ui.dd_filter, g_filter_opts);
    lv_dropdown_set_selected(g_ui.dd_filter, 0);
}

/*
 * 执行丢次完整过滤
 *
 * 过滤条件由三部分组成
 * - 查询模式决定比较字段
 * - 下拉框决定具体，ALL 表示不过滤；
 * - 名称关键字做额外模糊匹配
 *
 * 过滤完成后会同步刷新表格、详情和仪表盘，保证用户看到的始终是丢致状态
 */
static void wl_apply_filter(void)
{
    uint16_t mode = lv_dropdown_get_selected(g_ui.dd_mode);
    const char *name_key = lv_textarea_get_text(g_ui.ta_name);
    char filter_val[24];

    lv_dropdown_get_selected_str(g_ui.dd_filter, filter_val, sizeof(filter_val));

    if(interaction_shell_apply_query((core_mode_t)mode,
                                     filter_val,
                                     name_key,
                                     &g_core_snapshot) != CORE_OK) {
        return;
    }

    wl_core_render_from_snapshot();
}

/*
 * 生成统计报告文本
 *
 * 这个函数将当前过滤结果按保护等级做简单分组，输出到右侧报告标签中
 * 使统计按钮有明确的视觉反馈
 */
static void wl_show_statistics(void)
{
    uint16_t i;
    int class1 = 0;
    int class2 = 0;
    int other = 0;
    char buf[192];

    /* 统计报告只看当前过滤结果，所以这里按保护等级快速分组，不再额外去扫全量记录。 */
    for(i = 0; i < g_filtered_count; i++) {
        wildlife_record_t *rec = &g_records[g_filtered_idx[i]];
        if(strcmp(rec->level, "ClassI") == 0) class1++;
        else if(strcmp(rec->level, "ClassII") == 0) class2++;
        else other++;
    }

    snprintf(buf,
             sizeof(buf),
             "Report: result=%u, ClassI=%d, ClassII=%d, Other=%d",
             (unsigned)g_filtered_count,
             class1,
             class2,
             other);
    wl_set_report_text(buf);
}

/*
 * 查询模式变化回调
 *
 * 丢旦模式改变，过滤候项也要重新生成，因此这里同时刷新项和结果
 */
static void wl_mode_changed_cb(lv_event_t *e)
{
    (void)e;
    /* 模式变化会改变“可选筛选项”和“命中规则”，所以两个步骤必须一起刷新。 */
    wl_update_filter_options();
    wl_apply_filter();
}

/*
 * 过滤下拉框变化回调
 *
 * 当用户切换具体分类时，立即执行过滤，避免还需要额外点击按钮
 */
static void wl_filter_changed_cb(lv_event_t *e)
{
    (void)e;
    /* 用户切换具体过滤项时直接执行过滤，减少一次额外点击。 */
    wl_apply_filter();
}

/*
 * Query 按钮回调
 *
 * 这是显式触发查询的入口，逻辑上与过滤下拉框变更一致，只是由按钮驱动
 */
static void wl_query_btn_cb(lv_event_t *e)
{
    (void)e;
    /* Query 按钮只是把当前界面条件手动再执行一次，和下拉框变化走的是同一条过滤链路。 */
    wl_apply_filter();
}

/*
 * Reset 按钮回调
 *
 * 重置时只清空关键查询条件，并恢复默认模式与项，不是破坏基硢数据
 */
static void wl_reset_btn_cb(lv_event_t *e)
{
    (void)e;
    wl_hide_image_layer();

    if(interaction_shell_reset_query(&g_core_snapshot) != CORE_OK) return;

    lv_textarea_set_text(g_ui.ta_name, g_core_snapshot.query.name_key);
    lv_dropdown_set_selected(g_ui.dd_mode, (uint16_t)g_core_snapshot.query.mode);
    wl_strcpy(g_filter_opts, sizeof(g_filter_opts), g_core_snapshot.query.filter_options);
    lv_dropdown_set_options(g_ui.dd_filter, g_filter_opts);
    lv_dropdown_set_selected(g_ui.dd_filter, 0);

    wl_core_render_from_snapshot();
    wl_set_report_text("Report: ready");
}

/*
 * Statistics 按钮回调
 *
 * 统计逻辑基于当前过滤结果，因此按钮只负责刷新报告文本，不改变数据状
 */
static void wl_stat_btn_cb(lv_event_t *e)
{
    (void)e;
    /* 统计按钮只负责把当前结果集再总结成一段文字，不修改任何数据。 */
    wl_show_statistics();
}

static void wl_save_btn_cb(lv_event_t *e)
{
    (void)e;
    if(interaction_shell_save_db(&g_core_snapshot) != CORE_OK) {
        wl_set_report_text("Report: save failed");
        return;
    }
    wl_core_render_from_snapshot();
}

static void wl_export_btn_cb(lv_event_t *e)
{
    (void)e;
    if(interaction_shell_export_filtered(&g_core_snapshot) != CORE_OK) {
        wl_set_report_text("Report: export failed");
        return;
    }
    wl_core_render_from_snapshot();
}

static void wl_to_edit_btn_cb(lv_event_t *e)
{
    (void)e;
    if(g_ui.tabview == NULL) return;
    /* 这里不是跳转到新页面，而是把同一个 TabView 切到 Edit 页，让 Main 和 Edit 共用同一套窗口。 */
    lv_tabview_set_act(g_ui.tabview, 1, LV_ANIM_ON);
}

static void wl_image_back_event_cb(lv_event_t *e)
{
    (void)e;
    /* 点击图片浮层本身也会返回主页面，这样用户不用额外找关闭按钮。 */
    wl_hide_image_layer();
}

static void wl_keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED) {
        /* 键盘完成输入、取消或失焦时都要隐藏，这样不会让键盘一直压在页面底部。 */
        wl_keyboard_hide();
    }

    if(code == LV_EVENT_DELETE && g_keyboard == kb) {
        g_keyboard = NULL;
    }
}

static void wl_keyboard_hide(void)
{
    lv_obj_t *ta;
    lv_indev_t *indev;

    if(g_keyboard == NULL) return;

    /* 软键盘是全局复用对象，隐藏时要顺便解除它和当前文本框的绑定。 */
    ta = lv_keyboard_get_textarea(g_keyboard);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_keyboard, NULL);

    if(ta != NULL) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        indev = lv_indev_get_act();
        if(indev != NULL) {
            lv_indev_reset(indev, ta);
        }
    }
}

static void wl_keyboard_ensure_created(lv_obj_t *screen)
{
    if(screen == NULL || g_keyboard != NULL) return;

    /* 只创建一把软键盘，后续所有输入框都复用它，避免每个字段都生成独立键盘对象。 */
    g_keyboard = lv_keyboard_create(screen);
    if(g_keyboard == NULL) return;

    lv_obj_set_size(g_keyboard, LV_HOR_RES, LV_VER_RES / 3);
    lv_obj_align(g_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_keyboard, wl_keyboard_event_cb, LV_EVENT_ALL, NULL);
}

static void wl_keyboard_attach_to_textarea(lv_obj_t *ta)
{
    lv_obj_t *screen;

    if(ta == NULL) return;

    screen = lv_obj_get_screen(ta);
    wl_keyboard_ensure_created(screen);
    if(g_keyboard == NULL) return;

    /* 数字字段和普通文本字段使用不同键盘模式，这样更符合字段语义，也能减少误输入。 */
    if(ta == g_ui.ta_qty_edit) lv_keyboard_set_mode(g_keyboard, LV_KEYBOARD_MODE_NUMBER);
    else lv_keyboard_set_mode(g_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* 键盘和文本框是一对一临时绑定的，绑定后再把键盘提到最前面，避免被其他控件遮住。 */
    lv_keyboard_set_textarea(g_keyboard, ta);
    lv_obj_clear_flag(g_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_keyboard);
}

static void wl_textarea_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        /* 点击或获得焦点时显示软键盘，让输入动作和界面状态保持同步。 */
        wl_keyboard_attach_to_textarea(ta);
        return;
    }

    if(code == LV_EVENT_DEFOCUSED) {
        wl_keyboard_hide();
        return;
    }
}

static void wl_screen_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *kb = g_keyboard;
    lv_obj_t *ta = NULL;

    if(kb != NULL) {
        ta = lv_keyboard_get_textarea(kb);
    }

    /* 点到页面空白区域时隐藏键盘，但如果点的是键盘本体或当前文本框，就不要误关。 */
    if(kb == NULL) return;
    if(target == kb || target == ta || lv_obj_get_parent(target) == kb) return;

    wl_keyboard_hide();
}

/*
 * 表格交互回调
 *
 * 点击或中某一行后，程序会同步定位到对应记录，并将其内容回填到编辑区
 */
static void wl_table_event_cb(lv_event_t *e)
{
    uint16_t row;
    uint16_t col;
    lv_event_code_t code = lv_event_get_code(e);
    (void)e;

    lv_table_get_selected_cell(g_ui.table, &row, &col);
    /* 点击表格某行后，先映射到原始记录索引，再同步右侧详情和编辑表单。 */
    if(row == 0U || row > g_filtered_count) return;

    if(interaction_shell_select_row((uint16_t)(row - 1U), &g_core_snapshot) == CORE_OK) {
        wl_core_copy_snapshot_to_ui_cache();
    }

    wl_set_selected_record((int16_t)g_filtered_idx[row - 1U]);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_VALUE_CHANGED) {
        wl_show_selected_image();
    }
}

/*
 * New 按钮回调
 *
 * 清除当前选择后，编辑页进入新增记录状态，等待用户输入新档案
 */
static void wl_new_btn_cb(lv_event_t *e)
{
    (void)e;
    if(interaction_shell_new_record(&g_core_snapshot) != CORE_OK) {
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: new mode failed");
        return;
    }
    wl_core_render_from_snapshot();
    lv_label_set_text(g_ui.label_edit_tip, "Edit mode: New record");
}

/*
 * 编辑页的核心提交逻辑
 *
 * 处理流程
 * 1. 读取表单
 * 2. 校验必填字段
 * 3. 组装新记录；
 * 4. 若当前有选中项则覆盖更新，否则寻找空槽位新增
 * 5. 刷新详情、表格和统计结果
 */
static void wl_apply_edit_btn_cb(lv_event_t *e)
{
    core_record_t core_rec;
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
    updated = (g_ui.ta_updated_edit != NULL) ? lv_textarea_get_text(g_ui.ta_updated_edit) : WL_DEFAULT_UPDATED_AT;
    lv_dropdown_get_selected_str(g_ui.dd_status_edit, status, sizeof(status));

    if(strlen(name) == 0U || strlen(cat) == 0U || strlen(level) == 0U || strlen(area) == 0U) {
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: required fields are empty");
        return;
    }

    memset(&rec, 0, sizeof(rec));
    wl_strcpy(rec.name, sizeof(rec.name), name);
    wl_strcpy(rec.category, sizeof(rec.category), cat);
    wl_strcpy(rec.image, sizeof(rec.image), img);
    wl_strcpy(rec.level, sizeof(rec.level), level);
    wl_strcpy(rec.area, sizeof(rec.area), area);
    rec.quantity = atoi(qty);
    wl_strcpy(rec.status, sizeof(rec.status), status);
    if(updated == NULL || updated[0] == '\0') {
        wl_strcpy(rec.updated_at, sizeof(rec.updated_at), WL_DEFAULT_UPDATED_AT);
    }
    else {
        if(!wl_is_updated_at_valid(updated)) {
            lv_label_set_text(g_ui.label_edit_tip, "Edit mode: Updated format invalid, use 26/04/04/16/51");
            return;
        }
        wl_strcpy(rec.updated_at, sizeof(rec.updated_at), updated);
    }
    rec.valid = 1;

    memset(&core_rec, 0, sizeof(core_rec));
    wl_strcpy(core_rec.name, sizeof(core_rec.name), rec.name);
    wl_strcpy(core_rec.category, sizeof(core_rec.category), rec.category);
    wl_strcpy(core_rec.image, sizeof(core_rec.image), rec.image);
    wl_strcpy(core_rec.level, sizeof(core_rec.level), rec.level);
    wl_strcpy(core_rec.area, sizeof(core_rec.area), rec.area);
    core_rec.quantity = rec.quantity;
    wl_strcpy(core_rec.status, sizeof(core_rec.status), rec.status);
    wl_strcpy(core_rec.updated_at, sizeof(core_rec.updated_at), rec.updated_at);
    core_rec.valid = 1U;

    if(interaction_shell_apply_edit(&core_rec, &g_core_snapshot) != CORE_OK) {
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: apply failed");
        return;
    }

    wl_core_render_from_snapshot();
    lv_label_set_text(g_ui.label_edit_tip, "Edit mode: apply success");
}

/*
 * 删除当前选中记录
 *
 * 这里采用软删除：仅将 valid 标记清零，不是立即清空所有字段，
 * 这样既能保留当前对象的内存结构，也方便后续复用同丢槽位
 */
static void wl_delete_btn_cb(lv_event_t *e)
{
    (void)e;
    if(interaction_shell_delete_selected(&g_core_snapshot) != CORE_OK) {
        lv_label_set_text(g_ui.label_edit_tip, "Edit mode: no record selected");
        return;
    }
    wl_core_render_from_snapshot();
    lv_label_set_text(g_ui.label_edit_tip, "Edit mode: record deleted");
}

/*
 * 创建带标签的单行输入框
 *
 * 这个小工具函数的目的，是统一编辑页字段的布局和风格，减少重复代码
 */
static lv_obj_t *wl_labeled_ta(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7F8C8D), 0);
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_width(ta, lv_pct(98));
    lv_obj_add_style(ta, &g_style_input, 0);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(ta, wl_textarea_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ta, wl_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, wl_textarea_focus_cb, LV_EVENT_DEFOCUSED, NULL);
    return ta;
}

/*
 * 创建左侧菜单按钮
 *
 * 该函数封装了统一尺寸、样式和文本居中逻辑，保证菜单项视觉丢致
 */
static lv_obj_t *wl_create_menu_button(lv_obj_t *parent, const char *icon, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *icon_label = lv_label_create(btn);
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 0);
    lv_obj_set_flex_grow(btn, 1);
    /*
     * 为了避免按钮在状态切换时短暂回落到主题默认色（蓝色），
     * 默认态、焦点态都绑定同一基础样式，并关闭动画插值。
     */
    lv_obj_add_style(btn, &g_style_sidebar_btn, 0);
    lv_obj_add_style(btn, &g_style_sidebar_btn, LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &g_style_sidebar_btn, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn, &g_style_sidebar_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &g_style_sidebar_btn_checked, LV_STATE_CHECKED);
    lv_obj_add_style(btn, &g_style_sidebar_btn_checked, LV_STATE_CHECKED | LV_STATE_FOCUSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_min_height(btn, 52, 0);
    lv_obj_set_style_pad_left(btn, 14, 0);
    lv_obj_set_style_pad_right(btn, 14, 0);
    lv_obj_set_style_pad_column(btn, 10, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_anim_time(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_anim_time(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_anim_time(btn, 0, LV_PART_MAIN | LV_STATE_CHECKED);
    /* 这里先创建图标和文字，再由后续回调统一切换按钮状态，避免每个按钮都写重复布局代码。 */
    lv_label_set_text(icon_label, icon);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0x8BA88E), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x2C3E50), 0);
    return btn;
}

static void wl_set_sidebar_active(uint8_t index)
{
    uint8_t i;

    if(index >= 4) return;

    /* 四个侧边按钮互斥选中，确保任何时刻只有一个高亮；这个状态会被侧栏回调和页签切换共同使用。 */
    for(i = 0; i < 4; i++) {
        if(g_ui.sidebar_buttons[i] == NULL) continue;
        if(i == index) lv_obj_add_state(g_ui.sidebar_buttons[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(g_ui.sidebar_buttons[i], LV_STATE_CHECKED);
    }

    g_ui.sidebar_active = index;
}

static void wl_clear_sidebar_active(void)
{
    uint8_t i;
    /* 顶部 Main 被点击时，左侧业务按钮全部取消高亮，这样用户会明确知道自己已经回到总览页。 */
    for(i = 0; i < 4; i++) {
        if(g_ui.sidebar_buttons[i] == NULL) continue;
        lv_obj_clear_state(g_ui.sidebar_buttons[i], LV_STATE_CHECKED);
    }
    g_ui.sidebar_active = 0xFFU;
}

static void wl_sidebar_btn_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    /* 左侧按钮本身不存业务逻辑，只负责把索引交给统一的视图同步函数。 */
    wl_sync_sidebar_view((uint8_t)index);
}

static void wl_sync_sidebar_view(uint8_t index)
{
    if(index >= 4U) return;

    /* 左侧四个按钮对应四种互斥视图，切换时要同时隐藏不相关区域，避免多个面板叠在一起。 */
    wl_set_sidebar_active(index);

    if(g_ui.center_col == NULL || g_ui.right_col == NULL) return;

    /* File 视图会复用中间区域，但只显示文件面板，因此要把查询表格、统计卡片和日志先统一隐藏。 */
    if(index == 0U) {
        lv_obj_clear_flag(g_ui.center_col, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.ctrl_wrap) lv_obj_add_flag(g_ui.ctrl_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.table_wrap) lv_obj_add_flag(g_ui.table_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.file_panel) lv_obj_clear_flag(g_ui.file_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.pie_panel) lv_obj_add_flag(g_ui.pie_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.kpi_wrap) lv_obj_add_flag(g_ui.kpi_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.detail_panel) lv_obj_add_flag(g_ui.detail_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.footer_panel) lv_obj_add_flag(g_ui.footer_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.log_panel) lv_obj_add_flag(g_ui.log_panel, LV_OBJ_FLAG_HIDDEN);
        wl_file_refresh();
        return;
    }

    /* Analytics 视图只需要卡片和饼图，表格和文件浏览面板反而会干扰用户观察统计结果。 */
    if(index == 1U) {
        lv_obj_clear_flag(g_ui.center_col, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.ctrl_wrap) lv_obj_add_flag(g_ui.ctrl_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.table_wrap) lv_obj_add_flag(g_ui.table_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.file_panel) lv_obj_add_flag(g_ui.file_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.kpi_wrap) lv_obj_clear_flag(g_ui.kpi_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.pie_panel) lv_obj_clear_flag(g_ui.pie_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.detail_panel) lv_obj_add_flag(g_ui.detail_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.footer_panel) lv_obj_add_flag(g_ui.footer_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.log_panel) lv_obj_add_flag(g_ui.log_panel, LV_OBJ_FLAG_HIDDEN);

        wl_refresh_dashboard();
        wl_show_statistics();
        return;
    }

    /* Classify 视图在当前版本里仍是占位入口，所以这里只保留与后续识别链路相关的表面布局。 */
    if(index == 2U) {
        lv_obj_clear_flag(g_ui.center_col, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.ctrl_wrap) lv_obj_clear_flag(g_ui.ctrl_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.table_wrap) lv_obj_clear_flag(g_ui.table_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.file_panel) lv_obj_add_flag(g_ui.file_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.kpi_wrap) lv_obj_clear_flag(g_ui.kpi_wrap, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.detail_panel) lv_obj_clear_flag(g_ui.detail_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.log_panel) lv_obj_add_flag(g_ui.log_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.pie_panel) lv_obj_add_flag(g_ui.pie_panel, LV_OBJ_FLAG_HIDDEN);
        if(g_ui.footer_panel) lv_obj_add_flag(g_ui.footer_panel, LV_OBJ_FLAG_HIDDEN);
        wl_set_report_text("Classify: reserved for future camera/CNN workflow");
        return;
    }

    /* Logs 视图把右侧日志区单独露出来，方便查看保存、导出和其他操作历史。 */
    lv_obj_add_flag(g_ui.center_col, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.file_panel) lv_obj_add_flag(g_ui.file_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.pie_panel) lv_obj_add_flag(g_ui.pie_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.kpi_wrap) lv_obj_add_flag(g_ui.kpi_wrap, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.detail_panel) lv_obj_add_flag(g_ui.detail_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.footer_panel) lv_obj_add_flag(g_ui.footer_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.log_panel) lv_obj_clear_flag(g_ui.log_panel, LV_OBJ_FLAG_HIDDEN);

    if(g_ui.label_log_history) {
        lv_label_set_text(g_ui.label_log_history, g_log_history);
    }
    wl_set_report_text("Logs: showing operation history and status messages");
}

/*
 * 给某个下拉框的弹出列表统丢套用主题样式
 *
 * LVGL dropdown 弹层是独list 对象，所以要在控件创建后单独找出来上样式
 */
static void wl_style_dropdown_list(lv_obj_t * dropdown)
{
    lv_obj_t * list = lv_dropdown_get_list(dropdown);

    if(list == NULL) return;

    /* 下拉框的弹出列表是运行时才创建的独立对象，不是主控件本体，所以这里必须等它生成后再单独补样式。 */
    lv_obj_add_style(list, &g_style_dropdown_list, LV_PART_MAIN);
    lv_obj_add_style(list, &g_style_dropdown_list_selected, LV_PART_SELECTED);
    lv_obj_add_style(list, &g_style_dropdown_list_selected, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_style(list, &g_style_dropdown_list_selected, LV_PART_SELECTED | LV_STATE_PRESSED);
}

/*
 * 下拉框主按钮采用 RTL 布局。
 *
 * 这样图标和文字在视觉上会更贴近当前页面的排列习惯，
 * 初学者可以把它理解成“让下拉箭头和文字交换左右位置”。
 */
static void wl_style_dropdown_button(lv_obj_t * dropdown)
{
    /* 主按钮采用 RTL 只是为了让图标和文字的位置更符合当前页面的视觉顺序。 */
    lv_obj_add_style(dropdown, &g_style_dropdown_button_rtl, LV_PART_MAIN);
}

static lv_obj_t *wl_create_kpi_card(lv_obj_t *parent, const char *caption, lv_obj_t **out_value)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_t *value = lv_label_create(card);
    lv_obj_t *label = lv_label_create(card);

    lv_obj_add_style(card, &g_style_kpi_card, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 6, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    /* KPI 卡片做成横向长条，并让三条纵向堆叠。 */
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, 34);

    /* 数值和说明文字分开创建，是为了后续只更新数值标签，不必重建整张卡片。 */
    lv_label_set_text(value, "0");
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x22313F), 0);
    lv_obj_set_style_text_line_space(value, 0, 0);
    lv_obj_set_style_transform_zoom(value, 170, 0);

    lv_label_set_text(label, caption);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7F8C8D), 0);

    *out_value = value;
    return card;
}

static lv_obj_t *wl_create_detail_row(lv_obj_t *parent, const char *key, lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *key_label = lv_label_create(row);
    lv_obj_t *value_label = lv_label_create(row);

    lv_obj_set_size(row, lv_pct(100), 30);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 左侧是固定字段名，右侧是后续由 wl_update_detail_label 填充的动态值。 */
    lv_label_set_text(key_label, key);
    lv_obj_set_width(key_label, 86);
    lv_obj_set_style_text_color(key_label, lv_color_hex(0x666666), 0);

    lv_label_set_text(value_label, "-");
    lv_obj_set_flex_grow(value_label, 1);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_LEFT, 0);

    *out_value = value_label;
    return row;
}

static const char *wl_status_title(const char *status)
{
    /* 详情区显示的是更友好的首字母大写文本，和内部存储状态字符串分开处理。 */
    if(strcmp(status, "recovering") == 0) return "Recovering";
    if(strcmp(status, "stable") == 0) return "Stable";
    if(strcmp(status, "decreasing") == 0) return "Decreasing";
    if(strcmp(status, "critical") == 0) return "Critical";
    if(strcmp(status, "extinct") == 0) return "Extinct";
    return status;
}

static void wl_modify_btn_cb(lv_event_t *e)
{
    (void)e;

    if(g_ui.tabview == NULL) return;
    /* Modify 按钮本质上是 Edit 页的快捷入口，避免用户先手动切页。 */
    lv_tabview_set_act(g_ui.tabview, 1, LV_ANIM_OFF);
}

static void wl_tab_changed_cb(lv_event_t *e)
{
    (void)e;

    wl_keyboard_hide();
    /* 切换页签时先处理键盘，避免文本框焦点在跨页后继续悬挂。 */

    if(g_ui.tabview == NULL) return;
    if(lv_tabview_get_tab_act(g_ui.tabview) != 0U) return;

    /* 只要回到 Main 页，就把侧边栏高亮、文件/日志/统计这些子视图都恢复到总览状态。 */
    wl_clear_sidebar_active();

    if(g_ui.center_col) lv_obj_clear_flag(g_ui.center_col, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.ctrl_wrap) lv_obj_clear_flag(g_ui.ctrl_wrap, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.table_wrap) lv_obj_clear_flag(g_ui.table_wrap, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.file_panel) lv_obj_add_flag(g_ui.file_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.pie_panel) lv_obj_add_flag(g_ui.pie_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.kpi_wrap) lv_obj_clear_flag(g_ui.kpi_wrap, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.detail_panel) lv_obj_clear_flag(g_ui.detail_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.footer_panel) lv_obj_clear_flag(g_ui.footer_panel, LV_OBJ_FLAG_HIDDEN);
    if(g_ui.log_panel) lv_obj_add_flag(g_ui.log_panel, LV_OBJ_FLAG_HIDDEN);
    wl_set_report_text("Main: overview restored");
}

/*
 * 构建主页面（Main Tab）
 *
 * 这个页面采用固定三栏布局：
 * - 左侧：业务入口导航；
 * - 中间：当前业务的主要操作区；
 * - 右侧：选中记录的摘要和辅助信息。
 *
 * 这种布局的目标是把“筛选 / 查看 / 反馈”放在同一个屏幕里，
 * 减少来回切页的成本。
 */
static void wl_create_main_tab(lv_obj_t *tab)
{
    lv_obj_t *root;
    lv_obj_t *sidebar;
    lv_obj_t *center_col;
    lv_obj_t *right_col;
    lv_obj_t *kpi_wrap;
    lv_obj_t *pie_panel;
    lv_obj_t *detail_panel;
    lv_obj_t *log_panel;
    lv_obj_t *detail_grid;
    lv_obj_t *footer_panel;
    lv_obj_t *scale;
    lv_obj_t *ctrl_wrap;
    lv_obj_t *action_row;
    lv_obj_t *file_panel;
    lv_obj_t *file_actions;
    lv_obj_t *btn;
    lv_obj_t *table_wrap;

    /* 根容器负责把三栏组织成一整页布局。 */
    root = lv_obj_create(tab);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(root, &g_style_screen, 0);
    lv_obj_set_style_pad_all(root, 20, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(root, 20, 0);

    /* 左侧菜单先创建出来，是因为它决定了后面中间区到底显示文件、统计还是日志。 */
    sidebar = lv_obj_create(root);
    lv_obj_set_size(sidebar, 130, lv_pct(100));
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sidebar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(sidebar, &g_style_sidebar, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(sidebar, 8, 0);
    g_ui.sidebar_buttons[0] = wl_create_menu_button(sidebar, LV_SYMBOL_DIRECTORY, "File");
    g_ui.sidebar_buttons[1] = wl_create_menu_button(sidebar, LV_SYMBOL_BARS, "Analytics");
    g_ui.sidebar_buttons[2] = wl_create_menu_button(sidebar, LV_SYMBOL_SETTINGS, "Classify");
    g_ui.sidebar_buttons[3] = wl_create_menu_button(sidebar, LV_SYMBOL_FILE, "Logs");
    lv_obj_add_event_cb(g_ui.sidebar_buttons[0], wl_sidebar_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    lv_obj_add_event_cb(g_ui.sidebar_buttons[1], wl_sidebar_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1);
    lv_obj_add_event_cb(g_ui.sidebar_buttons[2], wl_sidebar_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)2);
    lv_obj_add_event_cb(g_ui.sidebar_buttons[3], wl_sidebar_btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)3);
    wl_set_sidebar_active(0);

    /* center_col 和 right_col 在后续多个回调里都会继续使用，所以这里必须先把句柄保存到全局 UI 上下文。 */
    center_col = lv_obj_create(root);
    g_ui.center_col = center_col;
    lv_obj_set_size(center_col, 0, lv_pct(100));
    lv_obj_set_flex_grow(center_col, 1);
    lv_obj_clear_flag(center_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(center_col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(center_col, &g_style_panel, 0);
    lv_obj_set_flex_flow(center_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(center_col, 15, 0);

    /* 中间上半区：模式选择、筛选条件、名称关键字和操作按钮。 */
    ctrl_wrap = lv_obj_create(center_col);
    g_ui.ctrl_wrap = ctrl_wrap;
    lv_obj_set_size(ctrl_wrap, lv_pct(100), 154);
    lv_obj_clear_flag(ctrl_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ctrl_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(ctrl_wrap, &g_style_panel, 0);
    lv_obj_set_flex_flow(ctrl_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctrl_wrap, 10, 0);

    scale = lv_obj_create(ctrl_wrap);
    lv_obj_set_size(scale, lv_pct(100), 48);
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scale, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(scale, &g_style_control, 0);
    lv_obj_set_style_pad_all(scale, 0, 0);
    lv_obj_set_flex_flow(scale, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(scale, 6, 0);
    lv_obj_set_style_translate_y(scale, 0, 0);

    g_ui.dd_mode = lv_dropdown_create(scale);
    lv_dropdown_set_options(g_ui.dd_mode, g_mode_options);
    lv_obj_set_size(g_ui.dd_mode, 143, 36);
    lv_obj_add_style(g_ui.dd_mode, &g_style_input, 0);
    wl_style_dropdown_button(g_ui.dd_mode);
    wl_style_dropdown_list(g_ui.dd_mode);
    lv_obj_add_event_cb(g_ui.dd_mode, wl_mode_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    g_ui.dd_filter = lv_dropdown_create(scale);
    lv_obj_set_size(g_ui.dd_filter, 140, 36);
    lv_obj_add_style(g_ui.dd_filter, &g_style_input, 0);
    wl_style_dropdown_button(g_ui.dd_filter);
    wl_style_dropdown_list(g_ui.dd_filter);
    lv_obj_add_event_cb(g_ui.dd_filter, wl_filter_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    g_ui.ta_name = lv_textarea_create(scale);
    lv_textarea_set_one_line(g_ui.ta_name, 1);
    lv_textarea_set_placeholder_text(g_ui.ta_name, "Name keyword");
    lv_obj_set_size(g_ui.ta_name, 0, 36);
    lv_obj_set_flex_grow(g_ui.ta_name, 1);
    lv_obj_add_style(g_ui.ta_name, &g_style_input, 0);
    lv_obj_add_flag(g_ui.ta_name, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(g_ui.ta_name, wl_textarea_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_ui.ta_name, wl_textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(g_ui.ta_name, wl_textarea_focus_cb, LV_EVENT_DEFOCUSED, NULL);

    action_row = lv_obj_create(ctrl_wrap);
    lv_obj_set_size(action_row, lv_pct(100), 76);
    lv_obj_clear_flag(action_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(action_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(action_row, &g_style_control, 0);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(action_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(action_row, 6, 0);
    lv_obj_set_style_pad_row(action_row, 6, 0);

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_query_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Query");

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_stat_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Statistics");

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Save DB");

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_export_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Export");

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_reset_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Reset");

    btn = lv_btn_create(action_row);
    lv_obj_set_size(btn, 104, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_to_edit_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Go Edit");

    /* 中间下半区：显示过滤后的记录表格。 */
    table_wrap = lv_obj_create(center_col);
    g_ui.table_wrap = table_wrap;
    /*
     * 在纵flex 布局里用 grow 占据剩余空间，不是固100% 高度
     * 否则容器会被父级裁切，导致表格可视高度与滚动范围不一致，长列表翻不到底
     */
    lv_obj_set_size(table_wrap, lv_pct(100), 0);
    lv_obj_set_flex_grow(table_wrap, 1);
    lv_obj_clear_flag(table_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(table_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(table_wrap, &g_style_panel, 0);

    g_ui.table = lv_table_create(table_wrap);
    lv_obj_set_size(g_ui.table, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(g_ui.table, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_ui.table, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_style(g_ui.table, &g_style_table, 0);
    lv_obj_add_event_cb(g_ui.table, wl_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ui.table, wl_table_event_cb, LV_EVENT_CLICKED, NULL);

    /* File 面板复用中间区域，但与查询表格互斥显示。 */
    file_panel = lv_obj_create(center_col);
    g_ui.file_panel = file_panel;
    lv_obj_set_size(file_panel, lv_pct(100), 0);
    lv_obj_set_flex_grow(file_panel, 1);
    lv_obj_clear_flag(file_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(file_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(file_panel, &g_style_panel, 0);
    lv_obj_set_flex_flow(file_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(file_panel, 8, 0);
    lv_obj_add_flag(file_panel, LV_OBJ_FLAG_HIDDEN);

    g_ui.file_path = lv_label_create(file_panel);
    lv_obj_set_width(g_ui.file_path, lv_pct(100));
    lv_obj_set_style_text_color(g_ui.file_path, lv_color_hex(0x2C3E50), 0);
    lv_label_set_long_mode(g_ui.file_path, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(g_ui.file_path, "0:/");

    file_actions = lv_obj_create(file_panel);
    lv_obj_set_size(file_actions, lv_pct(100), 44);
    lv_obj_clear_flag(file_actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(file_actions, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(file_actions, &g_style_control, 0);
    lv_obj_set_flex_flow(file_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(file_actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(file_actions, 6, 0);

    btn = lv_btn_create(file_actions);
    lv_obj_set_size(btn, 84, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_file_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    lv_label_set_text(lv_label_create(btn), "Up");

    btn = lv_btn_create(file_actions);
    lv_obj_set_size(btn, 84, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_file_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1);
    lv_label_set_text(lv_label_create(btn), "Refresh");

    btn = lv_btn_create(file_actions);
    lv_obj_set_size(btn, 84, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_file_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)2);
    lv_label_set_text(lv_label_create(btn), "Open");

    btn = lv_btn_create(file_actions);
    lv_obj_set_size(btn, 84, 30);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_file_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)3);
    lv_label_set_text(lv_label_create(btn), "Delete");

    g_ui.file_table = lv_table_create(file_panel);
    lv_obj_set_size(g_ui.file_table, lv_pct(100), 0);
    lv_obj_set_flex_grow(g_ui.file_table, 1);
    lv_obj_set_scroll_dir(g_ui.file_table, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_ui.file_table, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_style(g_ui.file_table, &g_style_table, 0);
    lv_obj_add_event_cb(g_ui.file_table, wl_file_table_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ui.file_table, wl_file_table_event_cb, LV_EVENT_CLICKED, NULL);

    g_ui.file_tip = lv_label_create(file_panel);
    lv_obj_set_width(g_ui.file_tip, lv_pct(100));
    lv_obj_set_style_text_color(g_ui.file_tip, lv_color_hex(0x7F8C8D), 0);
    lv_label_set_long_mode(g_ui.file_tip, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(g_ui.file_tip, "Selected: -");

    right_col = lv_obj_create(root);
    g_ui.right_col = right_col;
    lv_obj_set_size(right_col, 260, lv_pct(100));
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(right_col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(right_col, &g_style_panel, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right_col, 8, 0);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    kpi_wrap = lv_obj_create(right_col);
    g_ui.kpi_wrap = kpi_wrap;
    /* KPI 区改为纵向堆叠三条横向长条。 */
    lv_obj_set_height(kpi_wrap, 140);
    lv_obj_set_width(kpi_wrap, lv_pct(100));
    lv_obj_clear_flag(kpi_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(kpi_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(kpi_wrap, &g_style_detail_panel, 0);
    lv_obj_set_flex_flow(kpi_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_column(kpi_wrap, 0, 0);
    lv_obj_set_style_pad_row(kpi_wrap, 6, 0);
    lv_obj_set_flex_align(kpi_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(kpi_wrap, 0);

    wl_create_kpi_card(kpi_wrap, "Matched", &g_ui.label_card_records);
    wl_create_kpi_card(kpi_wrap, "Critical", &g_ui.label_card_risk);
    wl_create_kpi_card(kpi_wrap, "Declining", &g_ui.label_card_trend);

    /* Analytics 面板占用中间区域的空白位置，用来显示状态占比饼图。 */
    pie_panel = lv_obj_create(center_col);
    g_ui.pie_panel = pie_panel;
    lv_obj_set_width(pie_panel, lv_pct(100));
    lv_obj_set_height(pie_panel, 0);
    lv_obj_set_flex_grow(pie_panel, 1);
    lv_obj_clear_flag(pie_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(pie_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(pie_panel, &g_style_detail_panel, 0);
    lv_obj_set_flex_flow(pie_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pie_panel, 6, 0);
    lv_obj_set_style_pad_all(pie_panel, 12, 0);
    lv_obj_add_flag(pie_panel, LV_OBJ_FLAG_HIDDEN);
    {
        uint8_t i;
        lv_obj_t *title = lv_label_create(pie_panel);
        lv_label_set_text(title, "Status Ratio");
        lv_obj_add_style(title, &g_style_detail_title, 0);

        /* 画布和图例都在这里一次性创建，后续刷新时只更新画布内容，不再反复 new 对象。 */
        g_ui.pie_canvas = lv_canvas_create(pie_panel);
        lv_canvas_set_buffer(g_ui.pie_canvas, g_pie_canvas_buf, WL_PIE_CANVAS_W, WL_PIE_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(g_ui.pie_canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        lv_obj_set_size(g_ui.pie_canvas, WL_PIE_CANVAS_W, WL_PIE_CANVAS_H);
        lv_obj_align(g_ui.pie_canvas, LV_ALIGN_TOP_MID, 0, 0);

        for(i = 0; i < WL_STATUS_BUCKETS; i++) {
            g_ui.pie_legend[i] = lv_label_create(pie_panel);
            lv_label_set_text(g_ui.pie_legend[i], "-");
            lv_obj_set_width(g_ui.pie_legend[i], lv_pct(100));
            lv_obj_add_flag(g_ui.pie_legend[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 右侧详情区：展示当前选中记录的摘要字段。 */
    detail_panel = lv_obj_create(right_col);
    g_ui.detail_panel = detail_panel;
    lv_obj_set_width(detail_panel, lv_pct(100));
    lv_obj_add_flag(detail_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(detail_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_style(detail_panel, &g_style_detail_panel, 0);
    lv_obj_set_flex_flow(detail_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail_panel, 10, 0);
    lv_obj_set_style_pad_all(detail_panel, 16, 0);
    lv_obj_set_flex_grow(detail_panel, 1);
    /* 这里通过 pad_row 调整每一行之间的垂直间距，让右侧详情区更容易阅读。 */
    g_ui.label_detail = lv_label_create(detail_panel);
    lv_label_set_text(g_ui.label_detail, "Detail: no selection");
    lv_obj_add_style(g_ui.label_detail, &g_style_detail_title, 0);

    detail_grid = lv_obj_create(detail_panel);
    lv_obj_set_width(detail_grid, lv_pct(100));
    lv_obj_set_flex_grow(detail_grid, 1);
    lv_obj_add_flag(detail_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(detail_grid, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_style(detail_grid, &g_style_detail_panel, 0);
    lv_obj_set_style_shadow_width(detail_grid, 0, 0);
    lv_obj_set_style_border_width(detail_grid, 0, 0);
    lv_obj_set_style_bg_opa(detail_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(detail_grid, 0, 0);
    lv_obj_set_flex_flow(detail_grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail_grid, 2, 0);

    wl_create_detail_row(detail_grid, "Name", &g_ui.label_detail_name);
    wl_create_detail_row(detail_grid, "Category", &g_ui.label_detail_category);
    wl_create_detail_row(detail_grid, "Image", &g_ui.label_detail_image);
    wl_create_detail_row(detail_grid, "Level", &g_ui.label_detail_level);
    wl_create_detail_row(detail_grid, "Area", &g_ui.label_detail_area);
    wl_create_detail_row(detail_grid, "Quantity", &g_ui.label_detail_qty);
    wl_create_detail_row(detail_grid, "Status", &g_ui.label_detail_status);
    wl_create_detail_row(detail_grid, "Updated", &g_ui.label_detail_updated);
    wl_create_detail_row(detail_grid, "Valid", &g_ui.label_detail_valid);
    lv_obj_set_style_pad_row(detail_grid, 10, 0);

    /* 日志区：集中显示保存、导出和异常消息。 */
    log_panel = lv_obj_create(right_col);
    g_ui.log_panel = log_panel;
    lv_obj_set_width(log_panel, lv_pct(100));
    lv_obj_add_flag(log_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(log_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_style(log_panel, &g_style_detail_panel, 0);
    lv_obj_set_flex_flow(log_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(log_panel, 6, 0);
    lv_obj_set_style_pad_all(log_panel, 14, 0);
    lv_obj_set_flex_grow(log_panel, 1);
    lv_obj_add_flag(log_panel, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *log_title = lv_label_create(log_panel);
        lv_label_set_text(log_title, "Logs");
        lv_obj_add_style(log_title, &g_style_detail_title, 0);
    }

    g_ui.label_log_history = lv_label_create(log_panel);
    lv_obj_set_width(g_ui.label_log_history, lv_pct(100));
    lv_obj_set_style_text_color(g_ui.label_log_history, lv_color_hex(0x4C5D6A), 0);
    lv_obj_set_style_text_align(g_ui.label_log_history, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_ui.label_log_history, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_ui.label_log_history, "Logs: waiting for events");

    /* 底部操作区：保留 Modify 按钮与统一反馈文本。 */
    footer_panel = lv_obj_create(right_col);
    g_ui.footer_panel = footer_panel;
    lv_obj_set_height(footer_panel, 98);
    lv_obj_set_width(footer_panel, lv_pct(100));
    lv_obj_clear_flag(footer_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(footer_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(footer_panel, &g_style_detail_panel, 0);
    lv_obj_set_flex_flow(footer_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(footer_panel, 6, 0);
    lv_obj_set_style_pad_all(footer_panel, 14, 0);
    lv_obj_set_flex_grow(footer_panel, 0);

    g_ui.btn_modify = lv_btn_create(footer_panel);
    lv_obj_set_width(g_ui.btn_modify, lv_pct(100));
    lv_obj_set_height(g_ui.btn_modify, 36);
    lv_obj_add_style(g_ui.btn_modify, &g_style_action_btn, 0);
    lv_obj_add_style(g_ui.btn_modify, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_ui.btn_modify, wl_modify_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(g_ui.btn_modify), "Modify");

    g_ui.label_report = lv_label_create(footer_panel);
    lv_obj_set_width(g_ui.label_report, lv_pct(100));
    lv_obj_set_style_text_color(g_ui.label_report, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_align(g_ui.label_report, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(g_ui.label_report, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_ui.label_report, "Report: ready");

    g_ui.image_layer = lv_obj_create(tab);
    /* 图片层是覆盖在主页面上的全屏浮层，平时隐藏，只有点选记录后才临时显示。 */
    lv_obj_set_size(g_ui.image_layer, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(g_ui.image_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_ui.image_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(g_ui.image_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_ui.image_layer, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_ui.image_layer, 0, 0);
    lv_obj_set_style_pad_all(g_ui.image_layer, 0, 0);
    lv_obj_add_flag(g_ui.image_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_ui.image_layer, wl_image_back_event_cb, LV_EVENT_CLICKED, NULL);

    g_ui.image_view = lv_img_create(g_ui.image_layer);
    lv_obj_center(g_ui.image_view);
    lv_obj_add_event_cb(g_ui.image_view, wl_image_back_event_cb, LV_EVENT_CLICKED, NULL);

    g_ui.label_image_tip = lv_label_create(g_ui.image_layer);
    lv_obj_set_width(g_ui.label_image_tip, lv_pct(100));
    lv_obj_set_style_text_color(g_ui.label_image_tip, lv_color_white(), 0);
    lv_obj_set_style_text_align(g_ui.label_image_tip, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_ui.label_image_tip, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_ui.label_image_tip, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(g_ui.label_image_tip, "Tap image to return");
}

/*
 * 构建编辑页面（Edit Tab）。
 *
 * 这个页面用于维护档案数据，支持新建、修改和删除。
 * 布局按照“标题 -> 表单 -> 操作按钮 -> 提示信息”的顺序组织，
 * 这样用户在编辑时可以很自然地理解先看字段、再点按钮、最后看结果。
 * 如果初始化时已经有选中记录，调用链会在后续刷新阶段把它回填到表单里。
 */
static void wl_create_edit_tab(lv_obj_t *tab)
{
    /* Edit 页采用单栏表单布局，减少字段编辑时的视觉切换成本。 */
    lv_obj_t *root = lv_obj_create(tab);
    lv_obj_t *title = lv_label_create(root);
    lv_obj_t *cont = lv_obj_create(root);
    lv_obj_t *row_btn = lv_obj_create(root);
    lv_obj_t *btn;

    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(root, &g_style_screen, 0);

    lv_label_set_text(title, "Wildlife Archive - Edit Page");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2C3E50), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_set_size(cont, lv_pct(98), lv_pct(72));
    lv_obj_align_to(cont, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_add_style(cont, &g_style_panel, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);

    /* 表单字段顺序与数据模型顺序一致，方便后续回填、保存和排查字段对应关系。 */
    g_ui.ta_name_edit = wl_labeled_ta(cont, "Name");
    g_ui.ta_cat_edit = wl_labeled_ta(cont, "Category");
    g_ui.ta_img_edit = wl_labeled_ta(cont, "Image Path");
    g_ui.ta_level_edit = wl_labeled_ta(cont, "Protection Level");
    g_ui.ta_area_edit = wl_labeled_ta(cont, "Distribution Area");
    g_ui.ta_qty_edit = wl_labeled_ta(cont, "Quantity");
    g_ui.ta_updated_edit = wl_labeled_ta(cont, "Updated (YY/MM/DD/HH/MM)");
    lv_textarea_set_text(g_ui.ta_updated_edit, WL_DEFAULT_UPDATED_AT);

    {
        lv_obj_t *label = lv_label_create(cont);
        lv_label_set_text(label, "Status");
        lv_obj_set_style_text_color(label, lv_color_hex(0x7F8C8D), 0);
        g_ui.dd_status_edit = lv_dropdown_create(cont);
        lv_dropdown_set_options(g_ui.dd_status_edit, g_status_options);
        lv_obj_set_width(g_ui.dd_status_edit, lv_pct(98));
        lv_obj_add_style(g_ui.dd_status_edit, &g_style_input, 0);
        wl_style_dropdown_list(g_ui.dd_status_edit);
    }

    /* 按钮区放在表单底部，形成“编辑 -> 提交 -> 删除”的自然操作流。 */
    lv_obj_set_size(row_btn, lv_pct(98), 52);
    lv_obj_align_to(row_btn, cont, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_add_style(row_btn, &g_style_control, 0);
    lv_obj_set_flex_flow(row_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_btn, 8, 0);

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 170, 34);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_new_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "New");

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 170, 34);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_apply_edit_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Apply");

    btn = lv_btn_create(row_btn);
    lv_obj_set_size(btn, 170, 34);
    lv_obj_add_style(btn, &g_style_action_btn, 0);
    lv_obj_add_style(btn, &g_style_action_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, wl_delete_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(btn), "Delete");

    g_ui.label_edit_tip = lv_label_create(root);
    lv_obj_set_width(g_ui.label_edit_tip, lv_pct(98));
    lv_obj_align_to(g_ui.label_edit_tip, row_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_text_color(g_ui.label_edit_tip, lv_color_hex(0x7F8C8D), 0);
    lv_label_set_text(g_ui.label_edit_tip, "Edit mode: ready");
}

/*
 * 页面初始化入口
 *
 * 这里完成以下步骤
 * 1. 初始化样式；
 * 2. 清理屏幕并恢复默认数据；
 * 3. 创建 TabView 和两个页面；
 * 4. 载入初始过滤条件并刷新首屏内容
 */
void ui_init(void)
{
    lv_obj_t *tab_main;
    lv_obj_t *tab_edit;
    lv_obj_t *tab_btns;

    /* 启动顺序：样式 -> 清屏 -> 数据初始化 -> UI 构建 -> 首轮刷新。 */
    wl_init_styles();
    lv_obj_clean(lv_scr_act());
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(lv_scr_act(), &g_style_screen, 0);

    /* 每次初始化都清掉旧 UI 残留，保证页面从干净状态开始。 */
    wl_release_image_buffer();
    memset(&g_ui, 0, sizeof(g_ui));
    g_log_history[0] = '\0';
    g_keyboard = NULL;
    wl_reset_defaults();

    if(interaction_shell_init(wl_core_event_cb, NULL, &g_core_snapshot) != CORE_OK) {
        wl_append_log("Core init failed");
    }

    /* 顶部 TabView 负责 Main / Edit 两个主页面切换。 */
    g_ui.tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 46);
    lv_obj_set_size(g_ui.tabview, lv_pct(100), lv_pct(100));
    lv_obj_add_style(g_ui.tabview, &g_style_screen, 0);
    lv_obj_add_event_cb(g_ui.tabview, wl_tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    tab_btns = lv_tabview_get_tab_btns(g_ui.tabview);
    lv_obj_add_style(tab_btns, &g_style_tab_btn, LV_PART_ITEMS);
    lv_obj_add_style(tab_btns, &g_style_tab_btn_checked, LV_PART_ITEMS | LV_STATE_CHECKED);

    tab_main = lv_tabview_add_tab(g_ui.tabview, "Main");
    tab_edit = lv_tabview_add_tab(g_ui.tabview, "Edit");

    /* 先构建主页面，再构建编辑页，确保首屏可直接操作。 */
    wl_create_main_tab(tab_main);
    wl_create_edit_tab(tab_edit);
    wl_set_report_text("Boot: tabs created");

    /* 软键盘和空白点击回收逻辑都要在页面对象创建后绑定，所以放在 UI 生成之后再注册。 */
    wl_keyboard_ensure_created(lv_scr_act());
    lv_obj_add_event_cb(lv_scr_act(), wl_screen_click_cb, LV_EVENT_CLICKED, NULL);

    /* 先跑一次空条件过滤，是为了让表格、详情和统计区在 boot 阶段就处于一致状态。 */
    wl_update_filter_options();
    wl_apply_filter();
    wl_sync_sidebar_view(0);
    wl_set_report_text("Boot: initial filter ready");

#if WL_ENABLE_BOOT_LOAD
    if(interaction_shell_load_db(&g_core_snapshot) == CORE_OK) {
        wl_core_render_from_snapshot();
        wl_set_report_text("Report: boot auto-load success -> 0:/wildlife_db.csv");
    }
    else {
        wl_set_report_text("Report: boot auto-load failed, using core default data");
    }
#else
    wl_set_report_text("Report: using core default data");
#endif
}

void wildlife_app_start(void)
{
    /* 这是对外暴露的应用入口之一，和 Hang2Hang 保持同一套初始化流程，方便不同工程集成方式复用。 */
    ui_init();
}

void Hang2Hang(void)
{
    /* 这是另一个兼容入口，外部如果按旧符号调用，也会走同样的 UI 初始化流程。 */
    ui_init();
}
