# Rapport de correction: I2C Timeouts & Task Watchdog

## Objectif
Corriger les déclenchements du Task Watchdog (TWDT) et les timeouts I2C affectant le tactile GT911 et l'IO expander sur le bus I2C partagé (GPIO8/9).

## Analyse des causes racines (Root Cause)

1.  **Architecture I2C Multiple**: L'ancien code créait potentiellement plusieurs instances du driver I2C ou des wrappers (`DEV_I2C`) concurrents, causant des conflits d'interruption et de mutex.
2.  **Surcharge CPU dans gt911_irq**:
    -   La tâche IRQ utilisait parfois des boucles d'attente actives ou des timeouts trop longs (200ms) bloquant le scheduler.
    -   L'absence de stratégie de dégagement (yield) explicite en cas de boucle I2C.
3.  **Blocage Physique du Bus**: Les esclaves I2C (GT911 ou IO Expander) peuvent parfois maintenir SDA bas (stuck bus) au boot, empêchant toute initialisation.
4.  **IO Extension Probe Fragile**: L'initialisation échouait immédiatement au premier NACK sans réessayer, bloquant le boot de l'écran.

## Correctifs Appliqués

### P0-A: Architecture Bus Unique
-   **Fichier**: `components/i2c/i2c_bus_shared.c`
-   **Modification**:
    -   Refonte de `i2c_bus_shared_init()` pour garantir une instance unique (`s_shared_bus`).
    -   Ajout d'une **séquence de récupération physique** (9 cycles d'horloge sur SCL) exécutée avant l'initialisation du driver si SDA/SCL sont détectés bas.
-   **Fichier**: `components/io_extension/io_extension.c`
    -   Suppression de l'utilisation du wrapper `DEV_I2C_*` (legacy).
    -   Utilisation directe de `i2c_bus_shared_get_handle()` et `i2c_master_transmit`.
    -   Ceci élimine les contentions cachées et assure que tout le trafic passe par le même mutex et driver.

### P0-B: Optimisation Driver GT911 (Tâche IRQ)
-   **Fichier**: `components/touch/gt911.c`
-   **Modification**:
    -   Réduction du timeout d'acquisition du mutex I2C de 200ms à **50ms** pour éviter de bloquer la tâche IDLE trop longtemps.
    -   Ajout d'une boucle de **retry (2 tentatives)** sur les lectures/écritures I2C pour tolérer les erreurs transitoires.
    -   Confirmation que la tâche utilise `ulTaskNotifyTake` (bloquant) et non du polling actif.

### P1: Stabilité Init IO Expander
-   **Fichier**: `components/io_extension/io_extension.c`
-   **Modification**:
    -   Ajout d'une boucle de **3 tentatives** pour la détection (Probe) de l'IO Expander.
    -   En cas d'échec intermédiaire, appel automatique à `i2c_bus_shared_recover()` pour tenter de débloquer le bus.

## Procédure de validation

1.  **Compilation Propre**: exécutez `idf.py fullclean && idf.py build`.
2.  **Test de Boot (Cold)**: Éteindre/Rallumer la carte. Vérifier dans les logs:
    -   `I: [i2c_bus_shared] Shared I2C bus initialized successfully`
    -   `I: [io_ext] IOEXT PROBE OK addr=0x24`
3.  **Test de Stabilité Tactile**:
    -   Toucher l'écran rapidement pendant 30s.
    -   Vérifier l'absence de `Task watchdog got triggered`.
    -   Vérifier que le tactile reste réactif.
4.  **Test de Récupération (Simulation)**:
    -   Si possible, court-circuiter brièvement SDA/GND (avec précaution) pour voir si le driver détecte l'erreur et retry sans planter le CPU.

## Diff des fichiers modifiés

Les modifications ont été appliquées directement. Voir les fichiers sources pour les détails exacts:
-   `components/i2c/i2c_bus_shared.c`
-   `components/io_extension/io_extension.c`
-   `components/touch/gt911.c`

Ce correctif est compatible ESP-IDF v6.1 et LVGL 9.4.
