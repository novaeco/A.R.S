# components/joltwallet__littlefs/AGENTS — LittleFS port rules (Strict)

## Scope
Applies to the vendored LittleFS component and its integration with ESP-IDF storage.

## Expectations
- Treat upstream structure as vendored: minimize diff, keep LICENSE/metadata intact, and document any fork-specific change.
- Do not alter partition tables or Kconfig defaults without coordinating with `components/board` storage layout.
- Prefer using existing ESP-IDF flash abstractions; avoid custom SPI flash tweaks that risk wear or boot stability.
- Add tests/examples only if they build with the repo toolchain; remove stale upstream artifacts carefully with justification.
- Ensure logging and error handling remain consistent with root fail-safe boot policy (no panics on mount failure).

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; échec de montage LittleFS doit se logguer et laisser l’app continuer.
- Logs: TAG littlefs indique init/mount et erreurs avec `esp_err_to_name`.
- Threading: accès flash via APIs ESP-IDF existantes sans tâches cachées.
