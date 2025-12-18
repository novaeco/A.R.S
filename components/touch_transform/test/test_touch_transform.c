#include "touch_transform.h"
#include "unity.h"

static void apply_and_check(const touch_transform_t *tf, int32_t x, int32_t y,
                            int32_t max_x, int32_t max_y, int exp_x,
                            int exp_y) {
  lv_point_t out;
  TEST_ASSERT_EQUAL(ESP_OK,
                    touch_transform_apply(tf, x, y, max_x, max_y, &out));
  TEST_ASSERT_EQUAL_INT(exp_x, out.x);
  TEST_ASSERT_EQUAL_INT(exp_y, out.y);
}

TEST_CASE("identity mapping", "[touch_transform]") {
  touch_transform_t tf;
  touch_transform_identity(&tf);
  apply_and_check(&tf, 10, 20, 200, 200, 10, 20);
}

TEST_CASE("scale_and_offset", "[touch_transform]") {
  touch_transform_t tf;
  touch_transform_identity(&tf);
  tf.a11 = 2.0f;
  tf.a22 = 0.5f;
  tf.a13 = 5.0f;
  tf.a23 = -3.0f;
  apply_and_check(&tf, 10, 20, 500, 500, 25, 7);
}

TEST_CASE("orientation_swap_mirror", "[touch_transform]") {
  touch_transform_t tf;
  touch_transform_identity(&tf);
  tf.swap_xy = true;
  tf.mirror_x = true;
  apply_and_check(&tf, 10, 5, 200, 200, -5, 10);
}

TEST_CASE("affine_solver_noise", "[touch_transform]") {
  lv_point_t raw[5] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {50, 50}};
  lv_point_t ref[5];
  // apply known transform: x'=2x+3y+10, y'= -1x+4y+5
  for (int i = 0; i < 5; ++i) {
    ref[i].x = 2 * raw[i].x + 3 * raw[i].y + 10;
    ref[i].y = -raw[i].x + 4 * raw[i].y + 5;
    if (i == 4) {
      ref[i].x += 2; // small noise
    }
  }
  touch_transform_t tf;
  touch_transform_metrics_t m;
  TEST_ASSERT_EQUAL(ESP_OK,
                    touch_transform_solve_affine(raw, ref, 5, &tf, &m));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.0f, tf.a11);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.0f, tf.a12);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, tf.a13);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -1.0f, tf.a21);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.0f, tf.a22);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 5.0f, tf.a23);
  TEST_ASSERT_TRUE(m.rms_error < 3.0f);
}

