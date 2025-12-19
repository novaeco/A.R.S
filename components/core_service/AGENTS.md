# components/core_service/AGENTS — Core service rules (Strict)

## Scope
Covers shared service primitives in `components/core_service` consumed by other modules.

## Expectations
- Keep APIs small, documented, and stable; return `esp_err_t` for operations and define ownership of returned buffers.
- Validate pointers and lengths on entry; tolerate absent data by returning empty/default structures instead of crashing.
- Avoid heavy allocations or long-running work on the main task; offload to dedicated tasks only when documented.
- Logging must be minimal and context-rich (function name, counts); avoid noisy loops in production paths.
- Add unit tests or stubs in `test/` when introducing new service helpers to prevent regressions.
