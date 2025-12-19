# components/data_manager/AGENTS — Persistent storage rules (Strict)

## 1) Integrity first
- Any partition/FS operation must:
  - validate offsets and sizes
  - handle errors without corrupting existing data
- Never assume partition exists: check by label.

## 2) Versioning
- If schemas change, add a migration path or bump version and reset safely (explicit).

## 3) Logging
- On init, log:
  - partition size
  - used bytes
  - migration action (if any)

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence ou corruption de partition doit rester non-bloquante.
- Logs: TAG data_manager publie taille/usage et migrations avec `esp_err_to_name` en cas d’échec.
- Threading: accès stockage aligné avec backend choisi sans bloquer LVGL ni boot.
