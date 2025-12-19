#include "touch_transform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gt911.h"
#include "touch_orient.h"
#include "sdkconfig.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static const char *TAG = "touch_transform";
static touch_transform_t s_active_transform = {.a11 = 1.0f, .a22 = 1.0f};

void touch_transform_identity(touch_transform_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  out->a11 = 1.0f;
  out->a22 = 1.0f;
}

const touch_transform_t *touch_transform_get_active(void) {
  return &s_active_transform;
}

void touch_transform_set_active(const touch_transform_t *tf) {
  if (!tf) {
    ESP_LOGW(TAG, "Active transform reset to identity (null input)");
    touch_transform_identity(&s_active_transform);
    return;
  }

  if (touch_transform_validate(tf) != ESP_OK) {
    ESP_LOGW(TAG,
             "Invalid active transform (det or NaN), reverting to identity");
    touch_transform_identity(&s_active_transform);
    return;
  }

  s_active_transform = *tf;
}

static void apply_orientation(const touch_transform_t *tf, int32_t *x,
                              int32_t *y) {
  if (tf->swap_xy) {
    int32_t tmp = *x;
    *x = *y;
    *y = tmp;
  }
  if (tf->mirror_x) {
    *x = -*x;
  }
  if (tf->mirror_y) {
    *y = -*y;
  }
}

esp_err_t touch_transform_validate(const touch_transform_t *tf) {
  if (!tf)
    return ESP_ERR_INVALID_ARG;

  if (!isfinite(tf->a11) || !isfinite(tf->a12) || !isfinite(tf->a13) ||
      !isfinite(tf->a21) || !isfinite(tf->a22) || !isfinite(tf->a23)) {
    return ESP_ERR_INVALID_STATE;
  }

  if (fabsf(tf->a11) > 100.0f || fabsf(tf->a22) > 100.0f ||
      fabsf(tf->a12) > 100.0f || fabsf(tf->a21) > 100.0f ||
      fabsf(tf->a13) > 10000.0f || fabsf(tf->a23) > 10000.0f) {
    return ESP_ERR_INVALID_STATE;
  }

  float det = tf->a11 * tf->a22 - tf->a12 * tf->a21;
  if (fabsf(det) < 1e-6f) {
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

esp_err_t touch_transform_apply_ex(const touch_transform_t *tf, int32_t raw_x,
                                   int32_t raw_y, int32_t max_x,
                                   int32_t max_y, bool apply_orientation_flag,
                                   lv_point_t *out) {
  if (!tf || !out)
    return ESP_ERR_INVALID_ARG;

  int32_t x = raw_x;
  int32_t y = raw_y;
  if (apply_orientation_flag) {
    apply_orientation(tf, &x, &y);
  }

  float fx = (float)x;
  float fy = (float)y;
  float rx = tf->a11 * fx + tf->a12 * fy + tf->a13;
  float ry = tf->a21 * fx + tf->a22 * fy + tf->a23;

  if (!isfinite(rx) || !isfinite(ry)) {
    return ESP_ERR_INVALID_STATE;
  }

  if (max_x > 0) {
    if (rx < 0)
      rx = 0;
    if (rx > (float)(max_x - 1))
      rx = (float)(max_x - 1);
  }
  if (max_y > 0) {
    if (ry < 0)
      ry = 0;
    if (ry > (float)(max_y - 1))
      ry = (float)(max_y - 1);
  }

  out->x = (lv_coord_t)lrintf(rx);
  out->y = (lv_coord_t)lrintf(ry);
  return ESP_OK;
}

esp_err_t touch_transform_apply(const touch_transform_t *tf, int32_t raw_x,
                                int32_t raw_y, int32_t max_x, int32_t max_y,
                                lv_point_t *out) {
  return touch_transform_apply_ex(tf, raw_x, raw_y, max_x, max_y, true, out);
}

static float mat3_det(float a11, float a12, float a13, float a21, float a22,
                      float a23, float a31, float a32, float a33) {
  return a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) +
         a13 * (a21 * a32 - a22 * a31);
}

static bool solve_normal_equations(const float m[3][3], const float b[3],
                                   float *x0, float *x1, float *x2,
                                   float *cond_out) {
  float det = mat3_det(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2],
                       m[2][0], m[2][1], m[2][2]);
  if (fabsf(det) < 1e-6f)
    return false;

  // Compute adjugate matrix for inverse
  float inv_det = 1.0f / det;
  float adj[3][3];
  adj[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
  adj[0][1] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
  adj[0][2] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
  adj[1][0] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
  adj[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
  adj[1][2] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
  adj[2][0] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
  adj[2][1] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
  adj[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];

  float inv[3][3];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      inv[r][c] = adj[r][c] * inv_det;
    }
  }

  *x0 = inv[0][0] * b[0] + inv[0][1] * b[1] + inv[0][2] * b[2];
  *x1 = inv[1][0] * b[0] + inv[1][1] * b[1] + inv[1][2] * b[2];
  *x2 = inv[2][0] * b[0] + inv[2][1] * b[1] + inv[2][2] * b[2];

  if (cond_out) {
    float norm_m = 0.0f;
    float norm_inv = 0.0f;
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        norm_m = fmaxf(norm_m, fabsf(m[r][c]));
        norm_inv = fmaxf(norm_inv, fabsf(inv[r][c]));
      }
    }
    *cond_out = norm_m * norm_inv;
  }

  return true;
}

esp_err_t touch_transform_solve_affine(const lv_point_t *raw,
                                       const lv_point_t *ref, size_t count,
                                       touch_transform_t *out,
                                       touch_transform_metrics_t *metrics) {
  if (!raw || !ref || !out || count < 3)
    return ESP_ERR_INVALID_ARG;

  float ata[3][3] = {{0}};
  float atbx[3] = {0};
  float atby[3] = {0};

  size_t used = 0;
  for (size_t i = 0; i < count; ++i) {
    float x = (float)raw[i].x;
    float y = (float)raw[i].y;
    float bx = (float)ref[i].x;
    float by = (float)ref[i].y;
    float row[3] = {x, y, 1.0f};

    ata[0][0] += row[0] * row[0];
    ata[0][1] += row[0] * row[1];
    ata[0][2] += row[0] * row[2];
    ata[1][0] += row[1] * row[0];
    ata[1][1] += row[1] * row[1];
    ata[1][2] += row[1] * row[2];
    ata[2][0] += row[2] * row[0];
    ata[2][1] += row[2] * row[1];
    ata[2][2] += row[2] * row[2];

    atbx[0] += row[0] * bx;
    atbx[1] += row[1] * bx;
    atbx[2] += row[2] * bx;

    atby[0] += row[0] * by;
    atby[1] += row[1] * by;
    atby[2] += row[2] * by;
    used++;
  }

  float cond_x = 0.0f, cond_y = 0.0f;
  float a11, a12, a13;
  float a21, a22, a23;
  if (!solve_normal_equations(ata, atbx, &a11, &a12, &a13, &cond_x) ||
      !solve_normal_equations(ata, atby, &a21, &a22, &a23, &cond_y)) {
    return ESP_ERR_INVALID_STATE;
  }

  touch_transform_t tf = {
      .a11 = a11, .a12 = a12, .a13 = a13,
      .a21 = a21, .a22 = a22, .a23 = a23,
  };
  tf.swap_xy = false;
  tf.mirror_x = false;
  tf.mirror_y = false;

  if (touch_transform_validate(&tf) != ESP_OK) {
    return ESP_ERR_INVALID_STATE;
  }

  if (metrics) {
    float sum_sq = 0.0f;
    float max_err = 0.0f;
    for (size_t i = 0; i < count; ++i) {
      lv_point_t mapped;
      touch_transform_apply(&tf, raw[i].x, raw[i].y, -1, -1, &mapped);
      float dx = (float)mapped.x - (float)ref[i].x;
      float dy = (float)mapped.y - (float)ref[i].y;
      float err = sqrtf(dx * dx + dy * dy);
      sum_sq += err * err;
      if (err > max_err)
        max_err = err;
    }
    metrics->rms_error = sqrtf(sum_sq / (float)count);
    metrics->max_error = max_err;
    metrics->condition_number = fmaxf(cond_x, cond_y);
    metrics->points_used = used;
    metrics->points_rejected = 0;
  }

  *out = tf;
  return ESP_OK;
}

esp_err_t touch_transform_solve_fallback(const lv_point_t *raw,
                                         const lv_point_t *ref, size_t count,
                                         touch_transform_t *out) {
  if (!raw || !ref || !out || count < 2)
    return ESP_ERR_INVALID_ARG;

  int32_t min_rx = INT32_MAX, min_ry = INT32_MAX;
  int32_t max_rx = INT32_MIN, max_ry = INT32_MIN;
  int32_t min_tx = INT32_MAX, min_ty = INT32_MAX;
  int32_t max_tx = INT32_MIN, max_ty = INT32_MIN;

  for (size_t i = 0; i < count; ++i) {
    if (raw[i].x < min_rx)
      min_rx = raw[i].x;
    if (raw[i].y < min_ry)
      min_ry = raw[i].y;
    if (raw[i].x > max_rx)
      max_rx = raw[i].x;
    if (raw[i].y > max_ry)
      max_ry = raw[i].y;

    if (ref[i].x < min_tx)
      min_tx = ref[i].x;
    if (ref[i].y < min_ty)
      min_ty = ref[i].y;
    if (ref[i].x > max_tx)
      max_tx = ref[i].x;
    if (ref[i].y > max_ty)
      max_ty = ref[i].y;
  }

  float scale_x = (float)(max_tx - min_tx) / (float)(max_rx - min_rx);
  float scale_y = (float)(max_ty - min_ty) / (float)(max_ry - min_ry);
  touch_transform_identity(out);
  out->a11 = scale_x;
  out->a22 = scale_y;
  out->a13 = (float)min_tx - scale_x * (float)min_rx;
  out->a23 = (float)min_ty - scale_y * (float)min_ry;
  return ESP_OK;
}

touch_sample_raw_t touch_transform_sample_raw_oriented(
    esp_lcd_touch_handle_t tp, bool apply_orientation_hint) {
  touch_sample_raw_t sample = {
      .pressed = false, .raw_x = 0, .raw_y = 0, .timestamp_us = 0};

  if (!tp)
    return sample;

  gt911_stats_t stats = {0};
  gt911_get_stats(&stats);
  sample.raw_x = stats.last_raw_x;
  sample.raw_y = stats.last_raw_y;
  sample.timestamp_us = esp_timer_get_time();

  // Interpret "pressed" based on irq counter delta / last read heuristic
  sample.pressed = stats.last_raw_x || stats.last_raw_y;

  if (apply_orientation_hint) {
    lv_point_t oriented = {.x = sample.raw_x, .y = sample.raw_y};
    touch_orient_map_point(touch_orient_get_active(), sample.raw_x,
                           sample.raw_y, CONFIG_ARS_TOUCH_X_MAX,
                           CONFIG_ARS_TOUCH_Y_MAX, &oriented);
    sample.raw_x = (uint16_t)(oriented.x >= 0 ? oriented.x : 0);
    sample.raw_y = (uint16_t)(oriented.y >= 0 ? oriented.y : 0);
  }

  return sample;
}

