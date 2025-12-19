# CHANGELOG

## Unreleased
- Corrige l’initialisation I2C partagée (suppression de la récursion, timeouts réduits, verrouillage sûr) et la gestion IO expander.
- Sécurise l’init BSP : macros batterie alignées Kconfig, test pattern désactivable, backlight/touch/SD en mode dégradé clair.
- LVGL : allocations DMA internes avec fallback, VSYNC optionnel après timeout, gestion des erreurs d’init/timer.
- GT911 : lecture I2C déplacée hors IRQ, compteurs d’erreurs avec logs différés, backoff intégré.
- SD ext-CS : retries bornés avec état explicite, logs séquencés.
- Réseau : retries Wi-Fi plafonnés, pas de log de mots de passe.
