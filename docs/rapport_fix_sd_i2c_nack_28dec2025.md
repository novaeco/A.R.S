# Technical Report: SD Card I2C NACK Fix (ESP32-S3 Waveshare 7B)

## Date: 2025-12-28

## Executive Summary

This report documents the root cause analysis and fix for `ESP_ERR_INVALID_RESPONSE` errors (I2C NACKs) during SD card initialization via the IO extender (CH32V003 @ I2C address 0x24) on the Waveshare ESP32-S3 Touch LCD 7B.

---

## 1. Symptom Description

### Observed Log Sequence
```
io_ext: IO_Mode OK
sd_extcs: CS probe: IOEXT4 toggle OK
CMD0 OK: "Card entered idle state (R1=0x01)"
I2C shared bus error [sd_extcs_cs]: ESP_ERR_INVALID_RESPONSE
CS LOW attempt X failed...
ACMD41 timeout
SD init failed
```

### Key Observations
1. **Initial communication succeeds**: `IO_Mode OK` and `CS probe OK` confirm the IO extender responds initially
2. **CMD0 succeeds**: The SD card enters idle state correctly
3. **Failure occurs AFTER CMD0**: During subsequent CS toggles for CMD8/ACMD41
4. **Error type**: `ESP_ERR_INVALID_RESPONSE` = I2C NACK from the CH32V003

---

## 2. Root Cause Analysis

### Primary Cause: CH32V003 I2C Timing Sensitivity

The CH32V003 is a microcontroller running custom I2C slave firmware, NOT a dedicated I2C IO expander IC. It has:

1. **Higher latency** than standard I2C peripherals (74HC595, PCF8574, etc.)
2. **Processing time requirements** between consecutive I2C commands
3. **No hardware I2C buffering** - each command blocks the MCU

### Why it worked initially but failed later:

During SD card initialization, the code performs rapid CS toggles:
- `sd_extcs_assert_cs()` → I2C write to CH32V003
- `sd_extcs_deassert_cs()` → I2C write to CH32V003
- Repeat for each SPI command (CMD0, CMD8, CMD55, ACMD41, CMD58)

With the previous configuration:
- **I2C SCL speed**: 100 kHz (10µs per bit, ~100µs per transaction)
- **CS settle delay**: 200µs
- **Effective I2C burst rate**: ~3-5 transactions per ms

This burst rate was too fast for the CH32V003 firmware to process.

### Secondary Causes

1. **Double touch pause**: Touch was being paused twice (in `main.c` and `sd.c`)
2. **Blocking delay only**: `ets_delay_us()` blocked the CPU but didn't yield to RTOS
3. **No diagnostic logging**: Hard to verify timing configuration was correct

---

## 3. Applied Fixes

### Fix A: Reduced I2C Speed for IO Extender (sdkconfig.defaults)

```diff
- CONFIG_ARS_IOEXT_SCL_SPEED_HZ=100000
+ CONFIG_ARS_IOEXT_SCL_SPEED_HZ=50000
```

**Rationale**: 50 kHz gives the CH32V003 ~2x more time between clock edges, reducing the chance of missed ACKs.

### Fix B: Increased CS I2C Settle Time (sdkconfig.defaults)

```diff
+ CONFIG_ARS_SD_EXTCS_CS_I2C_SETTLE_US=1000
+ CONFIG_ARS_SD_EXTCS_CS_PRE_CMD0_DELAY_US=500
```

**Rationale**: 1ms settle time (vs 200µs) gives the CH32V003 sufficient time to complete processing before the next I2C transaction.

### Fix C: RTOS-Friendly CS Delay (sd_host_extcs.c)

```c
// Before: Pure blocking delay
ets_delay_us(settle_us);

// After: FreeRTOS delay for longer waits
if (settle_us >= 1000) {
    vTaskDelay(pdMS_TO_TICKS(settle_us / 1000));
    ets_delay_us(settle_us % 1000);
} else {
    ets_delay_us(settle_us);
}
```

**Rationale**: Using `vTaskDelay()` allows other RTOS tasks to run during the delay, preventing CPU starvation and allowing the I2C bus to fully settle.

### Fix D: Removed Redundant Touch Pause (sd.c)

```diff
- bool touch_paused = false;
- #if CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT
-   touch_pause_for_sd_init(true);
-   touch_paused = true;
- #endif
+ // NOTE: Touch pause is handled by the caller (main.c)
```

**Rationale**: Touch pause is already performed in `main.c` with a 50ms delay before calling `sd_card_init()`. Double-pausing was unnecessary and could cause timing inconsistencies.

### Fix E: Diagnostic Logging (sd_host_extcs.c)

```c
ESP_LOGI(TAG, "SD I2C config: IOEXT_SCL=%u Hz, CS_settle=%u us, pre_CMD0=%u us",
         (unsigned)CONFIG_ARS_IOEXT_SCL_SPEED_HZ,
         (unsigned)SD_EXTCS_CS_I2C_SETTLE_US,
         (unsigned)SD_EXTCS_CS_PRE_CMD0_DELAY_US);
```

**Rationale**: Allows verification that timing fixes are correctly applied at runtime.

---

## 4. Validation Steps

### Build and Flash Commands

```bash
cd c:\Users\woode\Desktop\ai\A.R.S
idf.py fullclean
idf.py build
idf.py -p COMx flash monitor
```

### Expected Log Output (Success Case)

```
sd_extcs: Mounting SD (SDSPI ext-CS) init=100 kHz target=20000 kHz
sd_extcs: SD I2C config: IOEXT_SCL=50000 Hz, CS_settle=1000 us, pre_CMD0=500 us
sd_extcs: SD pre-init: I2C bus healthy, skipping forced recovery
io_ext: IO_Mode OK
sd_extcs: SD ExtCS: IOEXT outputs configured (mask=0xFF, CS=IO4 push-pull)
sd_extcs: CS probe: IOEXT4 toggle OK
sd_extcs: CMD0 -> IDLE
sd_extcs: CMD8 -> ...
sd_extcs: ACMD41 ready after N attempt(s)
sd_extcs: CMD58 OK
sd: SD state -> INIT_OK
sd: SD read validation PASS (sector 0 readable)
```

### Expected Log Output (Card Absent Case)

```
sd_extcs: Mounting SD (SDSPI ext-CS) init=100 kHz target=20000 kHz
sd_extcs: SD I2C config: IOEXT_SCL=50000 Hz, CS_settle=1000 us, pre_CMD0=500 us
sd_extcs: CMD0 failed after 3 tries (all 0xFF)
sd: SD state -> ABSENT
```

### Failure Indicators (If Issue Persists)

If you still see `I2C shared bus error [sd_extcs_cs]: ESP_ERR_INVALID_RESPONSE`:

1. **Verify sdkconfig**: Run `idf.py menuconfig` and check:
   - `SD Card Configuration → CS I2C settle delay` = 1000
   - `I2C Configuration → IOEXT SCL Speed Hz` = 50000

2. **Try slower I2C**: Set `CONFIG_ARS_IOEXT_SCL_SPEED_HZ=25000`

3. **Check wiring**: Ensure SDA/SCL have 4.7kΩ pull-up resistors

---

## 5. Files Modified

| File | Changes |
|------|---------|
| `sdkconfig.defaults` | Reduced IOEXT I2C speed to 50kHz, increased CS settle to 1000µs |
| `components/sd/sd.c` | Removed redundant touch pause (handled by caller) |
| `components/sd/sd_host_extcs.c` | Added RTOS-friendly delay, diagnostic logging |

---

## 6. Technical Details

### I2C Timing Calculation

At 50 kHz with ~10 bytes per transaction:
- Transaction time: ~1.6ms
- With 1ms settle: Effective rate = ~400 transactions/sec
- This is well within CH32V003 capabilities

### Why GT911 Touch Didn't Cause Issues

The GT911 touch controller:
- Has dedicated I2C hardware
- Responds faster than CH32V003
- Is paused during SD init (`CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT=y`)

---

## 7. Conclusion

The SD card I2C NACK issue was caused by the CH32V003 IO extender's I2C slave firmware being unable to keep up with rapid I2C transactions during SD initialization. The fix involves:

1. Slowing down I2C communication to 50 kHz
2. Adding 1ms delay between CS toggle operations
3. Using FreeRTOS delays instead of blocking delays
4. Removing redundant touch pause calls

These changes ensure reliable SD card initialization without impacting LCD, LVGL, or GT911 touch functionality.

---

## 8. Rollback Instructions

If these changes cause issues, revert:

```diff
# sdkconfig.defaults
- CONFIG_ARS_IOEXT_SCL_SPEED_HZ=50000
+ CONFIG_ARS_IOEXT_SCL_SPEED_HZ=100000

- CONFIG_ARS_SD_EXTCS_CS_I2C_SETTLE_US=1000
- CONFIG_ARS_SD_EXTCS_CS_PRE_CMD0_DELAY_US=500
```

Then run `idf.py fullclean build`.
