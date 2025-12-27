## Symptom\u00e8

- CMD0 en ext-CS retourne uniquement `0xFF`, class\u00e9 `sd=ABSENT`, avec diagnostic `MISO stuck high or CS inactive`. Logs : `CMD0 failed after 12 tries @100 kHz class=ABSENT... raw= FF ...` et `MISO stuck high or CS inactive`.  
- Erreurs I\u00b2C concomitantes sur le touch : `TOUCH_EVT: i2c_errors=2 ...` puis `I2C recovered after 2 errors`.

## Causes possibles
- Contention I\u00b2C entre GT911 et l'expander CH32V003 pendant les basculements CS, menant \u00e0 des \u00e9checs d'\u00e9criture sur l'expander (CS non effectivement assert\u00e9).
- Absence de validation/lecture du latch expander apr\u00e8s les \u00e9critures CS, rendant la d\u00e9tection d'un CS non appliqu\u00e9 tardive (voir `shadow=0xFF` constant).
- Transactions CS non instrument\u00e9es finement (pas de trace du r\u00e9sultat de write/readback ni du streak d'erreurs I\u00b2C) pour corr\u00e9ler avec les erreurs `TOUCH_EVT`.

## Root cause retenue
- Pas de preuve unique, mais le pattern `CMD0 all-FF` + `i2c_errors` sur un bus partag\u00e9 indique un CS possiblement non assert\u00e9 ou mal lib\u00e9r\u00e9 \u00e0 cause d'\u00e9checs I\u00b2C concurrents. Le manque de validation CS et l'absence de mode d'isolation touch pendant l'init SD limitent le diagnostic.

## Correctifs appliqu\u00e9s
- **Atomicit\u00e9 CS ext-CS + readback optionnel** : `sd_extcs_apply_cs_level_locked()` r\u00e9alise d\u00e9sormais les \u00e9critures CS sous mutex I\u00b2C, avec retries born\u00e9s, journalisation du shadow avant/apr\u00e8s, et readback du latch expander activable via `CONFIG_ARS_SD_EXTCS_CS_READBACK`. Les erreurs I\u00b2C d\u00e9clenchent une tentative de r\u00e9cup\u00e9ration sous lock. \u3010F:components/sd/sd_host_extcs.c\u2020L45-L82\u3011\u3010F:components/sd/sd_host_extcs.c\u2020L296-L356\u3011
- **S\u00e9rialisation CS simplifi\u00e9e** : les helpers `sd_extcs_assert_cs()/deassert_cs()` s'appuient sur la nouvelle fonction centralis\u00e9e (plus de double boucle), conservant le lock I\u00b2C sur toute la transaction SPI. \u3010F:components/sd/sd_host_extcs.c\u2020L414-L454\u3011
- **Mode test d'isolation I\u00b2C** : ajout de l'option `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT` (Kconfig SD) pour mettre en pause la t\u00e2che GT911 pendant l'init SD; exposition d'une API `touch_pause_for_sd_init()`/`gt911_set_paused()` qui stoppe IRQ/polling et r\u00e9active l'IRQ ensuite. Utilis\u00e9e dans `main.c` autour de `sd_card_init()`. \u3010F:components/sd/Kconfig\u2020L47-L80\u3011\u3010F:components/touch/gt911.c\u2020L188-L219\u3011\u3010F:components/touch/gt911.c\u2020L1694-L1701\u3011\u3010F:components/touch/gt911.h\u2020L93-L101\u3011\u3010F:components/touch/touch.c\u2020L142-L147\u3011\u3010F:components/touch/touch.h\u2020L67-L75\u3011\u3010F:main/main.c\u2020L62-L73\u3011
- **Instrumentation CS** : logs `CS ... shadow ... latched ... streak` pour chaque bascule afin de corr\u00e9ler l'\u00e9tat CS avec `i2c_error_streak`.
- **Activation par d\u00e9faut (sdkconfig.defaults)** : `CONFIG_ARS_SD_EXTCS_CS_READBACK=y` et `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT=y` sont positionn\u00e9s par d\u00e9faut pour fiabiliser les diagnostics (d\u00e9sactiver au besoin pour mesurer la perf brute).

## Proc\u00e9dure de test
1. **Build**  
   - `idf.py fullclean`  
   - `idf.py build`

2. **Flash & monitor**  
   - `idf.py -p COM3 flash monitor`

3. **Validation attendue en logs**  
   - Pendant l'init SD : lignes `CS LOW/HIGH OK ... shadow ... latched=...` sans \u00e9chec ni recover r\u00e9p\u00e9t\u00e9.  
   - `Diag-CMD0` puis `CMD0: Card entered idle state (R1=0x01)` si carte pr\u00e9sente.  
   - Absence de nouveaux `TOUCH_EVT ... i2c_errors` pendant l'init SD si `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT=y`.  
   - En absence de carte : message contr\u00f4l\u00e9 `SD init: NO_CARD detected` sans panic.

4. **Rollback**  
   - D\u00e9sactiver `CONFIG_ARS_SD_EXTCS_CS_READBACK` et `CONFIG_ARS_SD_PAUSE_TOUCH_DURING_SD_INIT`, puis rebuild.
