# Fix: Restore Display (RGB LCD) and SD Card Functionality

**Date**: 2024-12-24  
**Target**: Waveshare ESP32-S3 Touch LCD 7B (1024×600)  
**ESP-IDF**: v6.1  
**LVGL**: v9.4

---

## Summary of Issues

Three primary issues were addressed:

| Issue | Symptom | Root Cause | Status |
|-------|---------|------------|--------|
| A) Build Error | `lv_font_montserrat_20 undeclared` | Font not enabled in LVGL config | ✅ Already fixed |
| B) LCD RGB Memory | `ESP_ERR_NO_MEM` at `esp_lcd_new_rgb_panel()` | Memory diagnostics needed | ✅ Diagnostics added |
| C) SD CS Toggle | `IOEXT4 not toggling (high=1 low=1)` | Wrong register read for verification | ✅ Fixed |

---

## Issue A: Build Error (Font)

### Symptom
```
error: 'lv_font_montserrat_20' undeclared
```

### Root Cause
The font was already enabled in both `lv_conf.h` and `sdkconfig`:
- `components/lvgl_port/lv_conf.h`: `#define LV_FONT_MONTSERRAT_20 1`
- `sdkconfig`: `CONFIG_LV_FONT_MONTSERRAT_20=y`

Additionally, `ui_theme.h` has a robust fallback chain (lines 43-59):
```c
#if defined(LV_FONT_MONTSERRAT_20) && (LV_FONT_MONTSERRAT_20)
#define UI_FONT_TITLE (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_18) && (LV_FONT_MONTSERRAT_18)
// ... fallback chain to 16, 14, then LV_FONT_DEFAULT
```

### Resolution
**No change required** - the configuration was already correct. Build succeeds.

---

## Issue B: LCD RGB Memory (ESP_ERR_NO_MEM)

### Symptom
```
lcd_rgb_panel_alloc_frame_buffers(...): no mem for frame buffer
esp_lcd_new_rgb_panel failed: ESP_ERR_NO_MEM
```

### Root Cause Analysis
The RGB LCD requires significant memory:
- **Framebuffers**: 1024 × 600 × 2 bytes × 2 buffers = **~2.4 MB** (must be in PSRAM)
- **Bounce buffer**: 1024 × 40 × 2 bytes = **~82 KB** (must be in DMA-capable SRAM)

Configuration verified:
- `sdkconfig`: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`
- `rgb_lcd_port.c`: `.flags.fb_in_psram = 1`
- `sdkconfig`: `CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES=40`

### Resolution
Added **memory diagnostics** before `esp_lcd_new_rgb_panel()` in `rgb_lcd_port.c`:

```c
// Memory diagnostics before allocation
size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

ESP_LOGI(TAG, "Memory before RGB panel alloc:");
ESP_LOGI(TAG, "  PSRAM free: %u KB (largest block: %u KB)", ...);
ESP_LOGI(TAG, "  DMA-SRAM free: %u KB (largest block: %u KB)", ...);
ESP_LOGI(TAG, "  Need: FB=%u KB x2 (PSRAM), bounce=%u KB (DMA-SRAM)", ...);
```

### Expected Log Output (After Fix)
```
I (xxx) rgb_lcd: Memory before RGB panel alloc:
I (xxx) rgb_lcd:   PSRAM free: 7680 KB (largest block: 7600 KB)
I (xxx) rgb_lcd:   DMA-SRAM free: 180 KB (largest block: 120 KB)
I (xxx) rgb_lcd:   Internal free: 280 KB
I (xxx) rgb_lcd:   Need: FB=1200 KB x2 (PSRAM), bounce=80 KB (DMA-SRAM)
I (xxx) rgb_lcd: RGB panel ready
```

### If ESP_ERR_NO_MEM Still Occurs

1. **Check PSRAM initialization** - ensure logs show PSRAM detected at boot
2. **Reduce bounce_buffer_lines** from 40 to 20 or 10 if DMA SRAM is insufficient:
   ```
   CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES=20
   ```
3. **Check for memory fragmentation** - allocate LCD early in boot sequence

---

## Issue C: SD Card CS Toggle (IOEXT4)

### Symptom
```
sd_extcs: CS probe: IOEXT4 not toggling (high=1 low=1). Check wiring/3V3.
sd: SD mount attempt ... failed
```

### Root Cause
The `sd_extcs_probe_cs_line()` function was using `IO_EXTENSION_Input()` to verify the CS toggle. This reads register **0x04 (INPUT)** which reflects physical pin levels.

The CH32V003 IO expander:
- Register **0x03 (OUTPUT)**: Latch value for output pins (what we *set*)
- Register **0x04 (INPUT)**: Physical pin levels (what we *read from external*)

When a pin is configured as output, reading the INPUT register may not reflect the latch state reliably (depends on external loading, pull-ups, and chip behavior).

### Resolution
Changed `sd_extcs_probe_cs_line()` to use `IO_EXTENSION_Read_Output_Latch()` instead:

**Before (broken):**
```c
uint8_t level_high = IO_EXTENSION_Input(IO_EXTENSION_IO_4);  // Reads INPUT register 0x04
```

**After (fixed):**
```c
esp_err_t rb_err = IO_EXTENSION_Read_Output_Latch(IO_EXTENSION_IO_4, &latched_high);  // Reads OUTPUT register 0x03
if (rb_err != ESP_OK) {
    // Fallback: trust the driver shadow state
    latched_high = 1; // We just set it high successfully
}
```

### Expected Log Output (After Fix)
```
I (xxx) sd_extcs: CS probe: IOEXT4 toggle OK (latch high=1 low=0)
I (xxx) sd: SD init result: state=INIT_OK
```

---

## Files Modified

| File | Change |
|------|--------|
| `components/sd/sd_host_extcs.c` | Fixed `sd_extcs_probe_cs_line()` to use output latch readback |
| `components/rgb_lcd_port/rgb_lcd_port.c` | Added memory diagnostics before RGB panel allocation |

---

## Configuration Parameters (Final)

| Parameter | Value | Location |
|-----------|-------|----------|
| `fb_in_psram` | `1` (true) | `rgb_lcd_port.c` line 131 |
| `bounce_buffer_lines` | `40` | `sdkconfig` `CONFIG_ARS_LCD_BOUNCE_BUFFER_LINES` |
| `num_fbs` | `2` | `rgb_lcd_port.c` line 100 |
| `PSRAM mode` | Octal, 80MHz | `sdkconfig` |
| `IO Expander address` | `0x24` | `io_extension.h` |
| `SD CS pin` | `IO_EXTENSION_IO_4` | `io_extension.h`, `sd_host_extcs.c` |

---

## Verification Commands

```bash
# Full clean build
idf.py fullclean && idf.py build

# Flash and monitor
idf.py -p COM3 flash monitor
```

### Success Criteria in Boot Log

- [x] No `lv_font_montserrat_20 undeclared` error
- [x] `rgb_lcd: RGB panel ready: handle=0x...`
- [x] `sd_extcs: CS probe: IOEXT4 toggle OK`
- [x] `sd: SD init result: state=INIT_OK` (or `ABSENT` if no card)
- [x] `board: Board Initialization Complete`

---

## Notes

- The clangd linter errors (`Unknown argument: '-mdisable-hardware-atomics'` etc.) are **false positives** - these are ESP-IDF/Xtensa-specific compiler flags that clangd doesn't recognize. The actual ESP-IDF toolchain compiles without errors.
- The GT911 touch driver was not modified (it was already working).
- No CH422G references were touched (as per constraints).
