#include "ui_app.h"
#include "lvgl_port.h"
#include "domain_models.h"
#include "compliance_rules.h"
#include "documents_service.h"
#include "export_share.h"
#include "board.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";
static storage_context_t *s_ctx;

static void build_topbar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), 40);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x123432), 0);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, "Reptile Admin Dashboard");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
}

static void show_animals_tab(lv_obj_t *parent)
{
    lv_obj_clean(parent);
    build_topbar(parent);
    lv_obj_t *list = lv_list_create(parent);
    lv_obj_set_size(list, lv_pct(100), lv_pct(60));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    for (size_t i = 0; i < s_ctx->animal_count; ++i) {
        const animal_record *a = &s_ctx->animals[i];
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_OK, a->id);
        lv_obj_set_user_data(btn, (void *)a);
    }

    lv_obj_t *timeline = lv_list_create(parent);
    lv_obj_set_size(timeline, lv_pct(100), lv_pct(30));
    lv_obj_align(timeline, LV_ALIGN_BOTTOM_MID, 0, -10);
    size_t ev_count = 0;
    const event_record *events = domain_models_get_timeline(&ev_count);
    for (size_t i = 0; i < ev_count; ++i) {
        const event_record *e = &events[i];
        char line[128];
        snprintf(line, sizeof(line), "%s - %s (%s)", e->timestamp, e->type, e->actor);
        lv_list_add_text(timeline, line);
    }
}

static void show_compliance_tab(lv_obj_t *parent)
{
    lv_obj_clean(parent);
    build_topbar(parent);
    size_t count = 0;
    const rule_check_result *results = compliance_rules_evaluate(s_ctx, &count);
    lv_obj_t *list = lv_list_create(parent);
    lv_obj_set_size(list, lv_pct(100), lv_pct(80));
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);
    for (size_t i = 0; i < count; ++i) {
        const rule_check_result *r = &results[i];
        char line[160];
        snprintf(line, sizeof(line), "%s [%s] - evidence: %s", r->title, r->satisfied ? "OK" : "MANQUANT", r->expected_evidence);
        lv_list_add_text(list, line);
    }
}

static void nav_btn_handler(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    lv_obj_t *screen = lv_screen_active();
    if (strcmp(txt, "Animaux") == 0) {
        show_animals_tab(screen);
    } else if (strcmp(txt, "Conformité") == 0) {
        show_compliance_tab(screen);
    }
}

static void build_navigation(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), 60);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x0C1C1C), 0);

    const char *labels[] = {"Dashboard", "Animaux", "Reproduction", "Documents", "Conformité", "Paramètres"};
    for (size_t i = 0; i < sizeof(labels)/sizeof(labels[0]); ++i) {
        lv_obj_t *btn = lv_btn_create(cont);
        lv_obj_set_width(btn, lv_pct(16));
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, (i * (BOARD_DISPLAY_H_RES / 6)), 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, nav_btn_handler, LV_EVENT_CLICKED, NULL);
    }
}

void ui_app_start(storage_context_t *ctx)
{
    s_ctx = ctx;
    lv_display_t *disp = lvgl_port_get_display();
    if (!disp) {
        ESP_LOGE(TAG, "LVGL display not ready");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1a1a), 0);
    show_animals_tab(scr);
    build_navigation(scr);
    lv_refr_now(disp);
    ESP_LOGI(TAG, "UI started");
}
