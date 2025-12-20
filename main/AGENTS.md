# main/AGENTS — Strict rules for app_main(), tasks, and boot sequencing

## 1) Boot sequencing invariants
- Boot must reach:
  - BSP/board init (display + touch)
  - LVGL port init and LVGL task start
  - UI init
  - Network init (can be “not provisioned”)
- SD init/mount is optional: any error -> log + continue.

## 2) Tasking / LVGL constraints
- LVGL object creation must occur in LVGL task context (or via the repo’s dispatcher).
- Do not call LVGL APIs directly from:
  - SD thread
  - network callbacks
  - IRQ context
- If data must be pushed to UI: use queue/event mechanism already present.

## 3) Watchdog / stability
- Do not add long blocking delays in app_main() without yields.
- Prefer timeouts and non-blocking state machines during peripheral init.

## 4) Strict output logging
- On boot, log a single-line summary:
  - display ok
  - touch ok
  - sd status (mounted / not present / failed)
  - wifi status (provisioned / not provisioned)
