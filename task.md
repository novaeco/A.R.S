# Development Environment Setup

## A) Fix WDT (CPU0 saturation)
- [x] Identify blocking code in `board.c` test pattern
- [/] Make test pattern always run in dedicated task (remove inline fallback)
- [x] Add Kconfig option `CONFIG_ARS_LCD_TEST_PATTERN` (disabled by default) - already exists
- [/] Ensure test pattern task has proper vTaskDelay() and low priority
