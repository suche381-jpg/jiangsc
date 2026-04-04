#ifndef WILDLIFE_CORE_H
#define WILDLIFE_CORE_H

#include "Wildlife_Def.h"

void wildlife_core_init(void);
wildlife_result_t wildlife_core_boot(uint8_t try_load_db);

wildlife_result_t core_apply_filter(wildlife_mode_t mode,
                                    const char *val,
                                    const char *key);

uint16_t core_get_view_count(void);
const wildlife_record_t *core_get_record(uint16_t view_idx);
wildlife_stats_t core_get_stats(void);

wildlife_result_t core_get_filter_options(char *out, uint32_t out_size);
const char *core_get_last_message(void);

wildlife_result_t core_update_record(int16_t view_idx, const wildlife_record_t *rec);
wildlife_result_t core_delete_record(int16_t view_idx);

wildlife_result_t core_save_db(void);
wildlife_result_t core_export_csv(void);
wildlife_result_t core_load_db(void);

#endif
