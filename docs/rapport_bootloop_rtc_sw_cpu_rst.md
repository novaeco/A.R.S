# Rapport bootloop RTC_SW_CPU_RST (esp_restart_noos)

## Symptôme observé
- Redémarrage en boucle avec `rst:0xc (RTC_SW_CPU_RST)` et `Saved PC` pointant vers `esp_restart_noos()` dès le boot (avant UI).

## Localisation des déclencheurs de reboot
- `components/iot/src/iot_manager.c`: `iot_init()` enchaînait des `ESP_ERROR_CHECK` sur `nvs_flash_init()`, `esp_netif_init()`, `esp_event_loop_create_default()` et la configuration Wi‑Fi. Toute erreur standard (`ESP_ERR_NVS_NO_FREE_PAGES`, `ESP_ERR_INVALID_STATE`, init Wi‑Fi) provoquait un `abort()` → panic → `esp_restart_noos`, avant même l’initialisation UI/SD.
- `components/gpio/gpio.c`: les helpers PWM (`DEV_GPIO_PWM`, `DEV_SET_PWM`) utilisaient `ESP_ERROR_CHECK` sur LEDC. Un échec LEDC (timer ou duty) entraînait le même `abort()` donc un reboot logiciel dès qu’un appel PWM était émis (ex. backlight fallback).

## Correctif appliqué (mode dégradé sans reboot)
- `iot_manager.c`: remplacement de tous les `ESP_ERROR_CHECK` par une gestion explicite des retours (`esp_err_to_name`) avec abandon gracieux de l’étape fautive. La pile NVS/esp_netif/loop s’arrête proprement si invalide et le démarrage Wi‑Fi retourne un `esp_err_t` au lieu de forcer un redémarrage.
- `gpio.c`: les helpers PWM journalisent désormais les erreurs LEDC et quittent la fonction sans panic, évitant tout `abort()` inattendu.

## Avant / Après
- **Avant**: échec NVS/esp_netif/LEDC ⇒ `abort()` ⇒ panic handler (`esp_restart_noos`) ⇒ boucle RTC_SW_CPU_RST.
- **Après**: échec ⇒ log contextualisé + retour d’erreur, le boot continue en mode dégradé (Wi‑Fi/PWM facultatifs), plus de boucle de redémarrage automatique.

## Commandes de validation
1. `idf.py fullclean`
2. `idf.py build`
3. `idf.py -p COM3 flash`
4. `idf.py -p COM3 monitor`

### Critères de réussite
- Absence de répétition `rst:0xc (RTC_SW_CPU_RST)` / `esp_restart_noos` dans le monitor.
- Logs visibles sur la console USB/JTAG (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y) montrant `Starting Reptiles Assistant`, puis init NVS/Netif/UI/SD (ou leurs erreurs non bloquantes). 
- En cas d’erreur NVS/Wi‑Fi/LEDC, présence d’un log `ESP_LOGE` explicite sans reboot.
