# components/reptile_storage/AGENTS — Persistent storage rules (Strict)

## Scope
Applies to NVS-backed storage helpers under `components/reptile_storage`.

## Expectations
- Use NVS with explicit namespaces and commit on each mutation; handle `nvs_open`/`nvs_get`/`nvs_set` errors explicitly.
- Avoid blocking boot: if storage is unavailable, return clear errors and let callers degrade gracefully.
- Validate key/value sizes before writes; guard against buffer overruns when reading strings.
- Never persist secrets in plaintext unless encryption/NVS security is explicitly configured elsewhere.
- Keep API surface minimal and documented in headers; avoid hidden globals or implicit init.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert même si NVS indisponible; init doit se dégrader proprement.
- Logs: TAG storage affiche init, occupation et erreurs avec `esp_err_to_name`.
- Threading: accès NVS dans les chemins autorisés, sans blocage du boot.
