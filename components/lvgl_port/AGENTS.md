# components/lvgl_port/AGENTS — LVGL display/input port (Strict)

## 1) Single-writer rule
- LVGL APIs must be called from LVGL task context unless an existing lock/bridge is used.

## 2) Display flush guarantees
- `flush_cb` must:
  - never block indefinitely
  - always call `lv_disp_flush_ready()` exactly once
- Prefer DMA-capable buffers already used by the repo.

## 3) Performance constraints
- Avoid full-frame flush every tick unless necessary.
- Do not increase buffer sizes blindly; justify with memory math.

## 4) Diagnostics
- Allowed: first flush log, frame timing stats at low rate.
- Forbidden: noisy per-pixel logs.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; LVGL task démarre même si display/touch optionnels manquent.
- Logs: TAG lvgl_port publie init + premier flush et erreurs avec `esp_err_to_name` en peu de lignes.
- Threading: toutes les APIs LVGL sont appelées depuis la tâche LVGL ou sous lock, `lv_disp_flush_ready()` appelé une seule fois.
