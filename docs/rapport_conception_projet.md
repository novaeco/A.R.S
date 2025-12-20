# Rapport de conception — Reptile Admin & Conformité

## Objectifs
- Tablette ESP32-S3 (Waveshare 7B) orientée gestion administrative d'élevage de reptiles.
- Pas de contrôle climatique : uniquement collecte de données, documents, échéances, règles.
- Architecture modulaire séparant drivers, services métier, et UI LVGL.

## Périmètre matériel
- ESP-IDF 6.1 (S3 + PSRAM), écran 1024×600, tactile GT911 via I²C partagé (mutex unique).
- IO extender CH32V003 @0x24 pour reset tactile et CS SD.
- SD en mode SDSPI, non bloquante au boot.

## Architecture retenue
- Drivers bas niveau : `board`, `i2c_bus_shared`, `io_extension`, `touch`, `touch_transform`, `display`, `sd`.
- Services : `storage_core`, `domain_models`, `compliance_rules`, `documents`, `export_share`.
- UI : `lvgl_port`, `ui` avec une seule boucle LVGL (task dédiée + tick séparé).
- Données de démonstration chargées en mémoire ; persistance stub (prévue sur SD/flash).

## Stratégie LVGL
- LVGL 9.x via composant géré, buffers en PSRAM double (partiel 40 lignes).
- Tâche `lv_tick` (core 0) + tâche `lv_loop` (core 1) pour respecter le mono-contexte UI.

## Conformité et données
- Modèles : Animaux, Taxons, Documents, Événements, Échéances.
- Règles data-driven simplifiées (3 règles) évaluées sur les données en mémoire.
- Exports CSV disponibles pour la liste des animaux ; bundle extensible.

## Points ouverts / TODO
- Mapper précisément le registre du CH32V003 (aujourd'hui shadow + best-effort I²C).
- Implémenter la persistance (FATFS sur SD ou SPIFFS/LittleFS) + migrations versionnées.
- Ajouter le calcul de fingerprint réel (SHA256) et le stockage binaire des documents.
- Étendre le moteur de règles (chargement JSON/CBOR, sévérités paramétrables).
- Adapter le driver LCD réel (esp_lcd rgb) et la transformation tactile exacte.
