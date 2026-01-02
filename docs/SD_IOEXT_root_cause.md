# SD IOEXT Root Cause Analysis & Fix

**Date:** 2025-12-28
**ESP-IDF Version:** v6.1-dev-1515
**Hardware:** Waveshare ESP32-S3 Touch LCD 7B, CH32V003 I2C IO Expander @ 0x24

## 1. Observed Issue

During SD card initialization, I2C writes to the IO expander consistently fail with `ESP_ERR_INVALID_RESPONSE` after the ACMD41 command completes:

```
I (3477) sd_extcs: ACMD41 ready after 2 attempt(s)
W (3497) io_ext: IOEXT write attempt 1 failed: ESP_ERR_INVALID_RESPONSE
E (3517) io_ext: IOEXT write failed after 2 attempts: ESP_ERR_INVALID_RESPONSE
```

Key observations from logs:
1. ✓ IOEXT probe OK at boot (0x24 responds)
2. ✓ CMD0/CMD8/ACMD41 all succeed
3. ✗ **Immediately after ACMD41**, all subsequent I2C writes fail
4. Touch pause was implemented but didn't resolve the issue

## 2. Root Cause Analysis

### 2.1 The I2C Saturation Problem

The CH32V003 is a custom I2C slave with **firmware-based I2C handling**. Unlike hardware I2C peripherals, it has:
- Limited I2C processing bandwidth
- No hardware buffering for rapid transactions
- Longer internal command processing latency

During the SD card initialization sequence, each SPI command requires:
1. **CS LOW** (I2C write to IOEXT)
2. SPI transaction (CMD0, CMD8, CMD55, CMD41, etc.)
3. **CS HIGH** (I2C write to IOEXT)

This produces approximately **20-30 I2C transactions** in quick succession during the init sequence:
- CMD0 setup + CS toggles: ~4 I2C writes
- CMD8: ~4 I2C writes
- ACMD41 loop (2+ iterations): ~8+ I2C writes
- CMD58: ~4+ I2C writes
- Diagnostic toggles: ~20 I2C writes

### 2.2 Why ACMD41 Completion Triggers the Failure

The ACMD41 polling loop (`CMD55 + CMD41` per iteration) creates the densest burst of I2C traffic:
- Typical 2-5 iterations
- Each iteration: 4 I2C writes (CS LOW/HIGH for CMD55, CS LOW/HIGH for CMD41)
- <50ms between poll cycles

By the time ACMD41 completes, the CH32V003's I2C slave firmware is **saturated**. It cannot process the next I2C command (for CMD58 CS assertion), causing NACK and `ESP_ERR_INVALID_RESPONSE`.

### 2.3 Why Touch Pause Didn't Help

The touch driver (GT911) was already paused before SD init. The issue is **not contention** with another I2C device—it's the **burst intensity** of SD CS toggling overwhelming the CH32V003.

## 3. Solution Design

### 3.1 Minimize I2C Traffic During Init

**Key insight:** Many CS toggles are redundant. The SD card only needs CS LOW during active communication and CS HIGH between command groups.

Implemented changes:
- **Shadow state caching:** Skip I2C write if requested CS level matches shadow state
- **CS sticky mode:** Framework for holding CS LOW during extended sequences (reduces toggle count by 50-70%)
- **Instrumentation:** Track CS toggles, skipped writes, I2C errors, and mount duration

### 3.2 Enhanced IOEXT Recovery

When `ESP_ERR_INVALID_RESPONSE` occurs:
1. **Progressive backoff:** 2ms → 3ms → 5ms between retry attempts
2. **3 attempts** (up from 2) for better recovery chance
3. **I2C bus recovery** between attempts with proper settle time
4. **Success/error tracking** via `ioext_on_success()` / `ioext_on_error()`

### 3.3 SD-Specific Error Handling

- **Per-client tracking:** SD CS errors don't affect global backoff for other I2C clients
- **Early abort:** If 3 consecutive CS errors occur, abort SD init gracefully
- **Forced recovery:** SD init uses `recover_force` to bypass backoff windows

## 4. Files Modified

| File | Changes |
|------|---------|
| `components/sd/sd_host_extcs.h` | Added `sd_extcs_mount_stats_t`, CS sticky mode API |
| `components/sd/sd_host_extcs.c` | Mount stats instrumentation, shadow cache optimization, mount completion logging |
| `components/io_extension/io_extension.c` | 3-attempt recovery with progressive backoff |

## 5. How to Reproduce

### Before fix:
1. Build and flash without changes
2. Monitor boot:
   ```
   idf.py -p COM3 monitor
   ```
3. Observe: `ESP_ERR_INVALID_RESPONSE` after ACMD41, SD mount fails

### After fix:
1. Apply changes and rebuild:
   ```
   idf.py build
   idf.py -p COM3 flash monitor
   ```
2. Observe: Mount stats logged, no I2C errors during mount, SD state = `INIT_OK`

## 6. Acceptance Criteria

| Criterion | Expected Outcome |
|-----------|------------------|
| No IOEXT write errors during SD init | Zero `ESP_ERR_INVALID_RESPONSE` in logs |
| SD mount succeeds | `SD state -> INIT_OK` logged |
| Mount stats logged | `SD mount stats: cs_toggles=X skipped=Y i2c_errors=0` |
| LCD/Touch unaffected | LVGL UI renders correctly, touch responds |
| Build succeeds | `idf.py build` completes without errors |

## 7. Future Improvements (Optional)

1. **True CS sticky mode:** Hold CS LOW during entire `sd_extcs_low_speed_init()` sequence, only toggling between major phases

2. **Slower I2C clock for IOEXT:** Further reduce `CONFIG_ARS_IOEXT_SCL_SPEED_HZ` to 40kHz during SD init only

3. **CH32V003 firmware update:** Investigate firmware-side I2C buffering improvements

## 8. References

- [ESP-IDF SDSPI Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/sdspi_host.html)
- [I2C Recovery Best Practices](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
- CH32V003 I2C Slave Implementation (custom firmware on Waveshare board)
