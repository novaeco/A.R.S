#include "ui_app.h"
#include "lvgl_port.h"
#include "domain_models.h"
#include "compliance_rules.h"
#include "documents_service.h"
#include "export_share.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sd_service.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";
static storage_context_t *s_ctx;
static lv_obj_t *s_content_area;
static lv_obj_t *s_nav_bar;

typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *sd;
    lv_obj_t *clock;
} topbar_widgets_t;

static topbar_widgets_t s_topbar = {0};
static lv_timer_t *s_status_timer;

static void update_topbar_status(lv_timer_t *timer)
{
    (void)timer;
    if (!s_topbar.wifi || !s_topbar.sd || !s_topbar.clock) {
        return;
    }

    lv_label_set_text(s_topbar.wifi, "Wi-Fi: --");

    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    lv_label_set_text(s_topbar.sd, mount ? "SD: montée" : "SD: absente");

    char clock_txt[16];
    int64_t seconds = esp_timer_get_time() / 1000000LL;
    int64_t hours = seconds / 3600;
    int64_t minutes = (seconds % 3600) / 60;
    int64_t secs = seconds % 60;
    snprintf(clock_txt, sizeof(clock_txt), "%02lld:%02lld:%02lld", hours % 24, minutes, secs);
    lv_label_set_text(s_topbar.clock, clock_txt);
}

static void build_topbar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, lv_pct(100), 48);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x123432), 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Reptile Admin Dashboard");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    lv_obj_t *status_row = lv_obj_create(bar);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(status_row, 1);

    s_topbar.wifi = lv_label_create(status_row);
    lv_label_set_text(s_topbar.wifi, "Wi-Fi: --");
    s_topbar.sd = lv_label_create(status_row);
    lv_label_set_text(s_topbar.sd, "SD: --");
    s_topbar.clock = lv_label_create(status_row);
    lv_label_set_text(s_topbar.clock, "--:--:--");

    lv_obj_add_flag(status_row, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);

    if (!s_status_timer) {
        s_status_timer = lv_timer_create(update_topbar_status, 1000, NULL);
    }
    update_topbar_status(NULL);
}

static void place_content(lv_obj_t *obj, lv_coord_t y_offset)
{
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y_offset);
}

static void show_animals_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 0);
    lv_obj_set_height(list, lv_pct(60));
    for (size_t i = 0; i < s_ctx->animal_count; ++i) {
        const animal_record *a = &s_ctx->animals[i];
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_OK, a->id);
        lv_obj_set_user_data(btn, (void *)a);
    }

    lv_obj_t *timeline = lv_list_create(s_content_area);
    place_content(timeline, lv_pct(60));
    lv_obj_set_height(timeline, lv_pct(40));
    size_t ev_count = 0;
    const event_record *events = domain_models_get_timeline(s_ctx, &ev_count);
    for (size_t i = 0; i < ev_count; ++i) {
        const event_record *e = &events[i];
        char line[128];
        snprintf(line, sizeof(line), "%s - %s (%s)", e->timestamp, e->type, e->actor);
        lv_list_add_text(timeline, line);
    }
}

static void show_compliance_tab(void)
{
    lv_obj_clean(s_content_area);
    size_t count = 0;
    const rule_check_result *results = compliance_rules_evaluate(s_ctx, &count);
    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 0);
    lv_obj_set_height(list, lv_pct(80));
    for (size_t i = 0; i < count; ++i) {
        const rule_check_result *r = &results[i];
        char line[160];
        snprintf(line, sizeof(line), "%s [%s] - evidence: %s", r->title, r->satisfied ? "OK" : "MANQUANT", r->expected_evidence);
        lv_list_add_text(list, line);
    }
}

static void show_documents_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *title = lv_label_create(s_content_area);
    lv_label_set_text(title, "Documents associés");
    place_content(title, 0);

    size_t count = 0;
    const document_record *docs = documents_service_list(s_ctx, &count);
    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 30);
    lv_obj_set_height(list, lv_pct(90));
    if (!docs || count == 0) {
        lv_list_add_text(list, "Aucun document");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const document_record *d = &docs[i];
        char line[192];
        snprintf(line, sizeof(line), "%s — %s (%s) [%s]", d->id, d->type, d->reference, d->valid ? "valide" : "à vérifier");
        lv_list_add_text(list, line);
    }
}

static void export_btn_handler(lv_event_t *e)
{
    lv_obj_t *status = (lv_obj_t *)lv_event_get_user_data(e);
    if (!status) {
        return;
    }
    const char *mount = NULL;
    sd_service_get_mount_point(&mount);
    if (!mount) {
        lv_label_set_text(status, "Export impossible: SD absente");
        return;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/animals.csv", mount);
    esp_err_t err = export_share_animals_csv(s_ctx, path);
    if (err == ESP_OK) {
        lv_label_set_text(status, "Dossier exporté sur SD (animals.csv)");
    } else {
        lv_label_set_text(status, "Echec export (voir logs)");
    }
}

static void show_export_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *info = lv_label_create(s_content_area);
    lv_label_set_text(info, "Export du dossier animal (CSV)");
    place_content(info, 0);

    lv_obj_t *status = lv_label_create(s_content_area);
    lv_label_set_text(status, "En attente d'action");
    place_content(status, 30);

    lv_obj_t *btn = lv_btn_create(s_content_area);
    lv_obj_set_width(btn, lv_pct(60));
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Exporter animals.csv");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, export_btn_handler, LV_EVENT_CLICKED, status);
}

static void show_deadlines_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *title = lv_label_create(s_content_area);
    lv_label_set_text(title, "Echéances (planificateur)");
    place_content(title, 0);

    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 30);
    lv_obj_set_height(list, lv_pct(90));

    size_t ev_count = 0;
    const event_record *events = domain_models_get_timeline(s_ctx, &ev_count);
    if (!events || ev_count == 0) {
        lv_list_add_text(list, "Aucune échéance disponible (persistance future)");
        return;
    }
    for (size_t i = 0; i < ev_count; ++i) {
        const event_record *e = &events[i];
        char line[192];
        snprintf(line, sizeof(line), "%s : %s -> %s", e->timestamp, e->type, e->related_animal);
        lv_list_add_text(list, line);
    }
    lv_list_add_text(list, "Synchroniser avec service échéances dès disponibilité.");
}

static void show_settings_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 0);
    lv_obj_set_height(list, lv_pct(90));
    lv_list_add_text(list, "Paramètres (profil élevage, juridiction) à connecter aux services persistants");
    lv_list_add_text(list, "Matériel: ESP32-S3 + écran RGB 1024x600");
    lv_list_add_text(list, "LVGL 9.x, buffer partiel en PSRAM");
    lv_list_add_text(list, "Touch GT911 via bus I2C partagé");
}

static void show_dashboard_tab(void)
{
    lv_obj_clean(s_content_area);
    lv_obj_t *list = lv_list_create(s_content_area);
    place_content(list, 0);
    lv_obj_set_height(list, lv_pct(90));
    char buf[64];
    snprintf(buf, sizeof(buf), "Animaux: %u", (unsigned int)s_ctx->animal_count);
    lv_list_add_text(list, buf);
    snprintf(buf, sizeof(buf), "Documents: %u", (unsigned int)s_ctx->document_count);
    lv_list_add_text(list, buf);
    snprintf(buf, sizeof(buf), "Evénements: %u", (unsigned int)s_ctx->event_count);
    lv_list_add_text(list, buf);
    lv_list_add_text(list, "Widgets synthèse à compléter");
}

static void nav_btn_handler(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (strcmp(txt, "Dashboard") == 0) {
        show_dashboard_tab();
    } else if (strcmp(txt, "Animaux") == 0) {
        show_animals_tab();
    } else if (strcmp(txt, "Export") == 0) {
        show_export_tab();
    } else if (strcmp(txt, "Echéances") == 0) {
        show_deadlines_tab();
    } else if (strcmp(txt, "Documents") == 0) {
        show_documents_tab();
    } else if (strcmp(txt, "Conformité") == 0) {
        show_compliance_tab();
    } else if (strcmp(txt, "Paramètres") == 0) {
        show_settings_tab();
    }
}

static void build_navigation(lv_obj_t *parent)
{
    s_nav_bar = lv_obj_create(parent);
    lv_obj_set_size(s_nav_bar, lv_pct(100), 60);
    lv_obj_set_style_bg_color(s_nav_bar, lv_color_hex(0x0C1C1C), 0);
    lv_obj_set_flex_flow(s_nav_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_nav_bar, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_nav_bar, LV_OBJ_FLAG_SCROLLABLE);

    const char *labels[] = {"Dashboard", "Animaux", "Export", "Echéances", "Documents", "Conformité", "Paramètres"};
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        lv_obj_t *btn = lv_btn_create(s_nav_bar);
        lv_obj_set_width(btn, lv_pct(100 / (sizeof(labels) / sizeof(labels[0]))));
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
    if (!lvgl_port_lock(500)) {
        ESP_LOGE(TAG, "LVGL lock timeout");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1a1a), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    build_topbar(scr);

    s_content_area = lv_obj_create(scr);
    lv_obj_set_width(s_content_area, lv_pct(100));
    lv_obj_set_flex_flow(s_content_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_content_area, 8, 0);
    lv_obj_clear_flag(s_content_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(s_content_area, 1);

    build_navigation(scr);

    show_animals_tab();
    lv_obj_move_foreground(s_nav_bar);
    lv_refr_now(disp);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "UI started");
}
