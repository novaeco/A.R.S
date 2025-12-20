# components/ui/AGENTS — UI/UX Implementation Rules (Strict)

## 1) Concurrency & Thread Safety (CRITICAL)
- LVGL is **not** thread-safe.
- **Forbidden**: Calling `lv_...` functions freely from:
  - Hardware ISRs
  - Timer callbacks (esp_timer)
  - Non-display FreeRTOS tasks (Net, Sensor, Logic)
- **Required**:
  - All UI updates must occur within the **LVGL task context**.
  - OR be protected by the `lvgl_port_lock()` / `lvgl_port_unlock()` mechanism (if provided by `lvgl_port`).

## 2) Event Handling & Performance
- **No Blocking in Callbacks**:
  - `lv_event_cb_t` functions run in the UI task.
  - **Forbidden**: `vTaskDelay`, blocking sockets, long file I/O, or heavy loops inside callbacks.
  - **Required**: Offload work to worker tasks via Queues/Semaphores. Update UI only when work is done (via synchronization).

## 3) Architecture & Structure
- **Respect the Existing Manager**:
  - This project uses a custom screen manager (`ui_screen_manager.c`) and theme engine (`ui_theme.c`).
  - **Do NOT** bypass them by creating ad-hoc screens in `main.c` or strictly following SquareLine/EEZ monolith patterns unless explicit.
- **Theme Consistency**:
  - Use `ui_theme` functions/constants for colors and styles.
  - Avoid hardcoded hex colors (e.g., `0xFF0000`) in logic files; keep styling centralized.

## 4) Aesthetics (User Mandate)
- **Visual Excellence**:
  - The UI must look **premium** and **modern**.
  - Use animations, smooth transitions, and "glassmorphism" where applicable.
  - **No placeholders**: If an asset is missing, fail gracefully or use a generated geometric fallback, but do not leave broken UI.

## 5) Error Handling
- **Fail-Safe**:
  - Missing fonts or images should log `ESP_LOGE` but **MUST NOT** crash/panic the controller.
  - If a screen fails to load, fallback to a "Safe Mode" or Dashboard.

## 6) Correctifs vs améliorations
- En correctif (bug/build/driver), priorité à la stabilité et au diff minimal.
- Les améliorations esthétiques/animations ne sont autorisées que lorsqu’elles sont demandées explicitement.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence d’assets ne bloque pas l’UI (fallback Safe Mode autorisé).
- Logs: TAG ui signale init/erreurs en quelques messages sans spam.
- Threading: toutes les interactions LVGL passent par la tâche LVGL ou un lock explicite.
