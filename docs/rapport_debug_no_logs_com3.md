# Rapport de diagnostic — absence de logs sur COM3

## Symptôme observé
- Monitor @115200 n’affiche que les lignes ROM: `ESP-ROM...`, `load...`, `entry...`, `call_start_cpu0 (bootloader_start.c:27)`, puis silence (pas de `boot:` ni `app_main`).

## Analyse configuration console et logs
| Source | Clé | Valeur actuelle | Impact pour COM3 |
| --- | --- | --- | --- |
| `sdkconfig` | `CONFIG_ESP_CONSOLE_UART_DEFAULT` | `y` | Console primaire sur UART0 (COM3 si câblé au port série). |
| `sdkconfig` | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `n` | Pas de redirection vers USB-JTAG, évite la perte de logs sur COM3. |
| `sdkconfig` | `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM` | `0` | Les prints ROM sortent sur UART0. |
| `sdkconfig.defaults` | `CONFIG_LOG_DEFAULT_LEVEL` | `3 (INFO)` | Niveau de log par défaut garantit l’affichage des `ESP_LOGI/W/E`. |
| `sdkconfig.defaults` | `CONFIG_BOOTLOADER_LOG_LEVEL` | `3 (INFO)` | Logs bootloader visibles avant `app_main`. |

## Instrumentation ajoutée (preuves d’atteinte d’app_main)
Trois checkpoints minimalistes utilisant `esp_rom_printf` (ROM UART0) + `ESP_LOGI`:
1. **Début d`app_main`** : `"ARS: app_main reached (build=... idf=...)"`
2. **Après init NVS** : `"ARS: checkpoint 2 after NVS (idf=...)"`
3. **Avant dispatch LVGL/UI** : `"ARS: checkpoint 3 before LVGL/UI dispatch"`

## Correctifs de configuration
- `sdkconfig.defaults`: valeurs booléennes Kconfig corrigées `0/1 -> n/y` pour `CONFIG_ARS_TOUCH_SWAP_XY`, `CONFIG_ARS_TOUCH_MIRROR_X`, `CONFIG_ARS_TOUCH_MIRROR_Y` (suppression des warnings Kconfig).
- `sdkconfig.defaults` + `sdkconfig`: console forçée sur UART0, USB_SERIAL_JTAG désactivé, niveaux de log bootloader/app fixés à INFO.

## Procédure de validation
1. `idf.py fullclean`
2. `idf.py build`
3. `idf.py -p COM3 flash`
4. `idf.py -p COM3 monitor`

### Résultat attendu au monitor
Au reset, au moins les lignes suivantes doivent apparaître sur COM3 (UART0):
```
ARS: app_main reached (build=... idf=...)
ARS: checkpoint 2 after NVS (idf=...)
ARS: checkpoint 3 before LVGL/UI dispatch
```
Puis les logs habituels (`boot:` puis `main: BOOT-SUMMARY ...`).

### Rollback
Revenir aux réglages précédents: restaurer les versions antérieures de `sdkconfig`, `sdkconfig.defaults`, et retirer les `esp_rom_printf`/`ESP_LOGI` ajoutés dans `main/main.c`, puis `idf.py fullclean build`.
