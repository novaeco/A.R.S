# tools/AGENTS — Tooling rules (Strict)

## Scope
Covers helper scripts and utilities under `tools/` used for builds, flashing, and maintenance.

## Expectations
- Scripts must be idempotent and avoid altering firmware configuration unless explicitly passed flags.
- Default behavior must be non-destructive: never erase NVS, partitions, or user data by default.
- Prefer ESP-IDF tooling (`idf.py`, `esptool.py`) and standard Python without new third-party deps unless justified.
- Provide clear usage/parameters and example commands; fail with actionable errors.
- Do not embed credentials or network endpoints; require them as inputs when needed.
