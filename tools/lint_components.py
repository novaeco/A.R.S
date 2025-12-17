#!/usr/bin/env python3
"""Simple lint to ensure component manifests and configs adhere to repo rules.

Checks applied per subdirectory of `components/`:
- CMakeLists.txt exists and contains `idf_component_register`.
- Kconfig, when present, is non-empty (no placeholder stubs).
The script returns non-zero on failure and prints human-readable diagnostics.
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
COMPONENTS_DIR = REPO_ROOT / "components"


def lint_component(component_dir: Path) -> list[str]:
    errors: list[str] = []
    cmake = component_dir / "CMakeLists.txt"
    if not cmake.is_file():
        errors.append("missing CMakeLists.txt")
    else:
        content = cmake.read_text(encoding="utf-8", errors="ignore")
        if "idf_component_register" not in content:
            errors.append("CMakeLists.txt does not call idf_component_register()")

    kconfig = component_dir / "Kconfig"
    if kconfig.exists() and not kconfig.read_text(encoding="utf-8", errors="ignore").strip():
        errors.append("Kconfig is present but empty")

    return errors


def main() -> int:
    if not COMPONENTS_DIR.is_dir():
        print(f"::error ::components directory not found at {COMPONENTS_DIR}")
        return 1

    failed = False
    for entry in sorted(COMPONENTS_DIR.iterdir()):
        if not entry.is_dir() or entry.name.startswith("."):
            continue
        if entry.name == "__pycache__":
            continue

        errors = lint_component(entry)
        if errors:
            failed = True
            prefix = f"[components/{entry.name}]"
            for err in errors:
                print(f"::error ::{prefix} {err}")

    if failed:
        print("Lint failed: fix the errors above.")
        return 1

    print("Component lint passed: CMakeLists.txt/Kconfig look consistent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
