# components/lv_port/AGENTS — LVGL display/input port (Strict)

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
