# Rapport bootloop — esp_restart_noos au boot

## Contexte matériel / cible
- ESP32-S3, écran RGB 1024×600 (ST7262), LVGL 9.x
- Bus I2C partagé (SDA=8, SCL=9) avec IO expander CH32V003 (adresse 0x24)
- Touch GT911 via IO expander, SD ext-CS via IO expander

## Symptôme observé
- Boucle de reboot avec log `rst:0xc (RTC_SW_CPU_RST)` et `Saved PC = esp_restart_noos()`.

## Cause racine
- Dans `app_main` (main/main.c), les appels critiques utilisaient `ESP_ERROR_CHECK()` sur `nvs_flash_init`, `esp_netif_init`, `esp_event_loop_create_default` et la création du netif STA. Toute erreur de NVS ou de pile réseau déclenchait un abort() -> panic -> `esp_restart_noos()`, d’où le reboot infini avant toute UI/SD. 【F:main/main.c†L21-L38】【F:main/main.c†L43-L81】

## Correctifs apportés
1) **Gestion d’erreur non bloquante au boot** : remplacement des `ESP_ERROR_CHECK` par une gestion explicite avec logs, poursuite en mode dégradé, et saut de `net_init` si la pile réseau de base est indisponible. Plus de restart en cas d’échec NVS/esp_netif/loop/netif. 【F:main/main.c†L43-L81】【F:main/main.c†L99-L125】
2) **Visibilité des logs console** : reconfiguration par défaut de la console sur UART0 (115200) au lieu de l’USB Serial/JTAG, pour voir les logs/panics sur la prise série standard. 【F:sdkconfig.defaults†L16-L18】

## Validation recommandée
1. Build propre :  
   ```bash
   idf.py fullclean
   idf.py build
   ```
2. Flash + monitor (adapter le port) :  
   ```bash
   idf.py -p /dev/ttyACM0 flash monitor
   ```
3. Succès attendu en monitor :  
   - Logs visibles sur UART0 (115200).  
   - Pas de reboot infini ; en cas d’échec NVS/esp_netif, log d’erreur puis boot continue (UI/SD init).  
   - Ligne `BOOT-SUMMARY ... wifi=net_stack_unavailable` si la pile réseau de base est indispo ; sinon état Wi-Fi habituel.  

## Pourquoi le reboot infini est supprimé
- Les init critiques ne passent plus par `ESP_ERROR_CHECK`; aucune voie ne provoque `abort()`/`esp_restart_noos()` en cas d’erreur NVS ou stack réseau. Le flux continue avec logs et mode dégradé, empêchant la boucle de redémarrage. 【F:main/main.c†L43-L81】【F:main/main.c†L99-L125】
