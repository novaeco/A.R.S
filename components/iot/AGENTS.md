# components/iot/AGENTS — IoT connectivity rules (Strict)

## Scope
Covers Wi-Fi control and OTA helpers under `components/iot`.

## Expectations
- Use ESP-IDF Wi-Fi/OTA APIs; handle errors with clear `esp_err_t` returns without reboot loops.
- Never log SSID/passwords or OTA URLs with tokens; require credentials as inputs and scrub buffers after use when feasible.
- Ensure OTA uses TLS/HTTPS when available and validates image signatures/checksums per ESP-IDF defaults.
- Keep connection/retry logic bounded to avoid blocking other tasks; provide hooks for graceful stop/deinit.
- Document task names, stack sizes, and core affinity if background tasks are added; maintain compatibility with provisioning flow.
