# components/iot/AGENTS — IoT connectivity rules (Strict)

## Scope
Covers Wi-Fi control and OTA helpers under `components/iot`.

## Expectations
- Use ESP-IDF Wi-Fi/OTA APIs; handle errors with clear `esp_err_t` returns without reboot loops.
- Never log SSID/passwords or OTA URLs with tokens; require credentials as inputs and scrub buffers after use when feasible.
- Ensure OTA uses TLS/HTTPS when available and validates image signatures/checksums per ESP-IDF defaults.
- Keep connection/retry logic bounded to avoid blocking other tasks; provide hooks for graceful stop/deinit.
- Document task names, stack sizes, and core affinity if background tasks are added; maintain compatibility with provisioning flow.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence de Wi-Fi ou OTA ne bloque pas l’UI.
- Logs: TAG iot signale init/connexion/erreur sans divulguer SSID/mdp.
- Threading: retries bornés et tâches documentées restent compatibles provisioning.
