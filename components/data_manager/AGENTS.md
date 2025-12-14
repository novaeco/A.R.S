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
