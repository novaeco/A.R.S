#include "web_server.h"
#include "core_service.h"

#include "board.h" // For board_sd_is_mounted
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;
static char s_auth_token[CONFIG_ARS_WEB_AUTH_TOKEN_BYTES * 2 + 1] = {0};
static bool s_token_ready = false;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");

// =============================================================================
// Embedded content replaces inline HTML

// =============================================================================
// Helper
// =============================================================================

static esp_err_t load_or_create_token(void) {
  nvs_handle_t nvs = 0;
  esp_err_t err = nvs_open("web", NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for web token: %s", esp_err_to_name(err));
    return err;
  }

  size_t required = sizeof(s_auth_token);
  err = nvs_get_str(nvs, "auth_token", s_auth_token, &required);
  if (err == ESP_OK && s_auth_token[0] != '\0') {
    s_token_ready = true;
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    uint8_t raw[CONFIG_ARS_WEB_AUTH_TOKEN_BYTES] = {0};
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); ++i) {
      snprintf(&s_auth_token[i * 2], 3, "%02x", raw[i]);
    }
    err = nvs_set_str(nvs, "auth_token", s_auth_token);
    if (err == ESP_OK) {
      err = nvs_commit(nvs);
    }
    if (err == ESP_OK) {
      s_token_ready = true;
      ESP_LOGW(TAG, "Auth token generated and stored in NVS (namespace 'web').");
    }
  }

  nvs_close(nvs);
  return err;
}

static bool is_authenticated(httpd_req_t *req) {
  if (!CONFIG_ARS_WEB_ENABLE) {
    return false;
  }
  if (!s_token_ready) {
    return false;
  }
  char buf[128];
  if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) ==
      ESP_OK) {
    const char *prefix = "Bearer ";
    size_t prefix_len = strlen(prefix);
    if (strncmp(buf, prefix, prefix_len) == 0) {
      const char *token = buf + prefix_len;
      if (strcmp(token, s_auth_token) == 0) {
        return true;
      }
    }
  }
  return false;
}

static esp_err_t httpd_resp_send_401(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer realm=\"A.R.S\"");
  return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED,
                             "Authentication required (Bearer token)");
}

static bool is_safe_path(const char *path) {
  // Prevent directory traversal
  if (strstr(path, "..")) {
    return false;
  }
  // Must start with base path
  if (strncmp(path, "/sdcard/reports/", 16) != 0) {
    return false;
  }
  return true;
}

static esp_err_t httpd_resp_send_503(httpd_req_t *req, const char *msg) {
#if defined(HTTPD_503_SERVICE_UNAVAILABLE)
  return httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, msg);
#else
  httpd_resp_set_status(req, "503 Service Unavailable");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
#endif
}

static esp_err_t httpd_resp_set_cors(httpd_req_t *req) {
  if (strlen(CONFIG_ARS_WEB_CORS_ORIGIN) == 0) {
    return ESP_OK; // CORS disabled by default
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",
                     CONFIG_ARS_WEB_CORS_ORIGIN);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods",
                     "GET, POST, PUT, DELETE, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                     "Content-Type, Authorization");
  return ESP_OK;
}

// =============================================================================
// Handlers
// =============================================================================

/* GET / handler - Serve embedded index.html */
static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (const char *)index_html_start,
                  index_html_end - index_html_start);
  return ESP_OK;
}

/* GET /app.css */
static esp_err_t css_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/css");
  httpd_resp_send(req, (const char *)app_css_start,
                  app_css_end - app_css_start);
  return ESP_OK;
}

/* GET /app.js */
static esp_err_t js_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
  return ESP_OK;
}

/* GET /health handler */
#include "esp_heap_caps.h"
#include "net_manager.h"
#include "sd.h" // for capacity

static esp_err_t health_get_handler(httpd_req_t *req) {
  httpd_resp_set_cors(req);
  if (!is_authenticated(req))
    return httpd_resp_send_401(req);
  cJSON *root = cJSON_CreateObject();

  // Uptime
  cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);

  // Memory
  cJSON *heap = cJSON_AddObjectToObject(root, "heap");
  cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
  cJSON_AddNumberToObject(heap, "min_free", esp_get_minimum_free_heap_size());

  // Wifi
  net_status_t net = net_get_status();
  cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
  cJSON_AddBoolToObject(wifi, "connected", net.is_connected);
  cJSON_AddStringToObject(wifi, "ip", net.ip_addr);
  cJSON_AddNumberToObject(wifi, "rssi", net.rssi);

  // Storage
  size_t total = 0, free_kb = 0;
  esp_err_t sd_err = read_sd_capacity(&total, &free_kb);
  cJSON *store = cJSON_AddObjectToObject(root, "storage");
  cJSON_AddBoolToObject(store, "mounted", (sd_err == ESP_OK));
  if (sd_err == ESP_OK) {
    cJSON_AddNumberToObject(store, "total_kb", total);
    cJSON_AddNumberToObject(store, "free_kb", free_kb);
  }

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, strlen(json_str));
  free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

/* GET /api/animals handler */
static esp_err_t api_animals_get_handler(httpd_req_t *req) {
  httpd_resp_set_cors(req);
  if (!is_authenticated(req))
    return httpd_resp_send_401(req);
  animal_summary_t *list = NULL;
  size_t count = 0;

  // Use core_list_animals
  if (core_list_animals(&list, &count) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateArray();
  for (size_t i = 0; i < count; i++) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", list[i].id);
    cJSON_AddStringToObject(item, "name", list[i].name);
    cJSON_AddStringToObject(item, "species", list[i].species);
    cJSON_AddItemToArray(root, item);
  }
  core_free_animal_list(list);

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, strlen(json_str));

  free((void *)json_str);
  cJSON_Delete(root);
  return ESP_OK;
}

/* POST /api/animals handler */
static esp_err_t api_animals_post_handler(httpd_req_t *req) {
  httpd_resp_set_cors(req);
  if (!is_authenticated(req))
    return httpd_resp_send_401(req);
  // SECURITY: Limit content length
  if (req->content_len > CONFIG_ARS_WEB_MAX_CONTENT_LENGTH) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
  }

  char *buf = malloc(req->content_len + 1);
  if (!buf)
    return httpd_resp_send_500(req);

  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  free(buf);

  if (!root) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }

  cJSON *name = cJSON_GetObjectItem(root, "name");
  cJSON *species = cJSON_GetObjectItem(root, "species");

  if (cJSON_IsString(name) && cJSON_IsString(species) &&
      strlen(name->valuestring) < 64 && strlen(species->valuestring) < 64) {

    animal_t new_animal = {0};

    // SECURITY: Secure ID generation
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    snprintf(new_animal.id, sizeof(new_animal.id), "%08lx-%08lx",
             (unsigned long)r1, (unsigned long)r2);

    strncpy(new_animal.name, name->valuestring, sizeof(new_animal.name) - 1);
    strncpy(new_animal.species, species->valuestring,
            sizeof(new_animal.species) - 1);
    new_animal.sex = SEX_UNKNOWN;

    if (core_save_animal(&new_animal) == ESP_OK) {
      httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    } else {
      httpd_resp_send_500(req);
    }
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid input");
  }

  cJSON_Delete(root);
  return ESP_OK;
}

// GET /reports handler - Lists files on SD
static esp_err_t reports_list_handler(httpd_req_t *req) {
  if (!is_authenticated(req))
    return httpd_resp_send_401(req);

  if (!board_sd_is_mounted()) {
    return httpd_resp_send_503(req, "SD card unavailable");
  }

  char **reports = NULL;
  size_t count = 0;

  // Assuming core_list_reports exists as hinted or we just list directory
  // manually
  if (core_list_reports(&reports, &count) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_sendstr_chunk(req, "<html><head><title>Rapports</title></"
                                "head><body><h1>Rapports Disponibles</h1><ul>");

  for (size_t i = 0; i < count; i++) {
    char buf[256];
    snprintf(buf, sizeof(buf), "<li><a href=\"/reports/%s\">%s</a></li>",
             reports[i], reports[i]);
    httpd_resp_sendstr_chunk(req, buf);
  }

  httpd_resp_sendstr_chunk(req,
                           "</ul><br><a href='/'>Retour</a></body></html>");
  httpd_resp_sendstr_chunk(req, NULL); // Finish

  core_free_report_list(reports, count);
  return ESP_OK;
}

// GET /reports/* handler - Download file
static esp_err_t report_download_handler(httpd_req_t *req) {
  if (!is_authenticated(req))
    return httpd_resp_send_401(req);

  if (!board_sd_is_mounted()) {
    return httpd_resp_send_503(req, "SD card unavailable");
  }

  char filepath[256];
  // Skip "/reports/" prefix (length 9)
  snprintf(filepath, sizeof(filepath), "/sdcard/reports/%s", req->uri + 9);

  if (!is_safe_path(filepath)) {
    ESP_LOGW(TAG, "Access denied: Unsafe path %s", filepath);
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access Denied");
  }

  FILE *f = fopen(filepath, "r");
  if (!f) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  char *chunk = malloc(1024);
  if (!chunk) {
    fclose(f);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  size_t chunksize;
  while ((chunksize = fread(chunk, 1, 1024, f)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
      fclose(f);
      free(chunk);
      return ESP_FAIL;
    }
  }
  free(chunk);
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// =============================================================================
// Init
// =============================================================================

esp_err_t web_server_init(void) {
  if (!CONFIG_ARS_WEB_ENABLE) {
    ESP_LOGW(TAG, "Web server disabled via CONFIG_ARS_WEB_ENABLE");
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (!server) {
    esp_err_t err = load_or_create_token();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Cannot start web server without auth token");
      return err;
    }
    if (CONFIG_ARS_WEB_REQUIRE_TLS_PROXY) {
      ESP_LOGW(TAG,
               "HTTP is exposed without TLS; deploy behind a TLS reverse proxy.");
    }
  }

  if (server)
    return ESP_OK;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192; // Increase stack for JSON processing
  config.uri_match_fn =
      httpd_uri_match_wildcard; // Enable wildcard for /reports/*

  ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {

    // URI: /
    httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_register_uri_handler(server, &root_uri);

    // URI: /app.css
    httpd_uri_t css_uri = {
        .uri = "/app.css", .method = HTTP_GET, .handler = css_get_handler};
    httpd_register_uri_handler(server, &css_uri);

    // URI: /app.js
    httpd_uri_t js_uri = {
        .uri = "/app.js", .method = HTTP_GET, .handler = js_get_handler};
    httpd_register_uri_handler(server, &js_uri);

    // URI: /health
    httpd_uri_t health_uri = {
        .uri = "/health", .method = HTTP_GET, .handler = health_get_handler};
    httpd_register_uri_handler(server, &health_uri);

    // URI: /api/animals (GET)
    httpd_uri_t animals_get_uri = {.uri = "/api/animals",
                                   .method = HTTP_GET,
                                   .handler = api_animals_get_handler};
    httpd_register_uri_handler(server, &animals_get_uri);

    // URI: /api/animals (POST)
    httpd_uri_t animals_post_uri = {.uri = "/api/animals",
                                    .method = HTTP_POST,
                                    .handler = api_animals_post_handler};
    httpd_register_uri_handler(server, &animals_post_uri);

    // URI: /reports (GET)
    httpd_uri_t reports_list = {
        .uri = "/reports", .method = HTTP_GET, .handler = reports_list_handler};
    httpd_register_uri_handler(server, &reports_list);

    // URI: /reports/* (GET)
    httpd_uri_t report_download = {.uri = "/reports/*",
                                   .method = HTTP_GET,
                                   .handler = report_download_handler};
    httpd_register_uri_handler(server, &report_download);

    return ESP_OK;
  }

  ESP_LOGE(TAG, "Error starting server!");
  return ESP_FAIL;
}

void web_server_stop(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
}