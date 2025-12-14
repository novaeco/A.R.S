# components/i2c_bus_shared/AGENTS — Shared I2C bus (Strict)

## 1) Single source of truth
- Only one module owns i2c driver install/uninstall.
- Other components must not call `i2c_driver_install()` directly.

## 2) Serialization
- Provide a mutex around I2C transactions.
- Make timeouts explicit and bounded.

## 3) Bus speed changes
- Avoid changing I2C clock at runtime unless the repo has a policy.
