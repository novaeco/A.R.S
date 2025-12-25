# Rapport d’Audit : Compatibilité ESP-IDF 6.1 & LVGL 9.4

**Date** : 23/12/2025
**Projet** : A.R.S (Assistant Reptiles)
**Cible** : ESP32-S3 (Waveshare 7" Touch LCD - IO Extender CH32V003)

---

## 1. Résumé Exécutif

**Statut Global : FONCTIONNEL MAIS INCOMPLET**

Le socle technique (BSP, Drivers, Affichage, Tactile, I²C) est **solide, moderne et conforme** aux exigences ESP-IDF 6.1 et LVGL 9.4. L'architecture matérielle complexe (I²C partagé, IO Extender) est correctement gérée.

Cependant, la couche **Logicielle Applicative (Métier)** est à l'état de **prototype/squelette**. Les moteurs de règles, la gestion documentaire et les modèles de données avancés sont absents ou stubbés.

| Domaine | Statut | Commentaire |
| :--- | :--- | :--- |
| **BSP / Hardware** | 🟢 **EXCELLENT** | Initialisation robuste, isolation propre (Board/IOExt/I2C). |
| **LVGL / Port** | 🟢 **CONFORME** | LVGL 9.x natif, Multicore, VSync anti-tearing, Thread-safe. |
| **Stockage Core** | 🔴 **CRITIQUE** | Squelette vide (`storage_core`). Pas de versioning/intégrité. |
| **Compliance / Règles** | 🔴 **MANQUANT** | Aucun moteur de règles trouvé. Stubs uniquement. |
| **Modèles Données** | 🟠 **PARTIEL** | Seuls `Reptile` et `Event` sont gérés (JSON simple). |

---

## 2. Inventaire Technique

### Arborescence Clé (Vérifiée)
```text
/main
  ├── main.c           (Orchestrateur boot : NVS -> I2C -> Board -> LVGL -> SD -> Net)
  └── lv_conf.h        (Redirection vers components/lvgl_port)
/components
  ├── board/           (BSP: LCD, Power seq, Init centralisé)
  ├── i2c/             (Bus partagé + Mutex + Recovery)
  ├── io_extension/    (Driver CH32V003 @0x24)
  ├── touch/           (Driver GT911 @0x5D/0x14 + IRQ Task)
  ├── lvgl_port/       (Adaptation LVGL 9, Flush, Mulit-buffer)
  ├── sd/              (Gestion SD via IOExtender CS)
  ├── data_manager/    (Stockage JSON LittleFS - Implémentation de base)
  ├── core_service/    (Services métier - Stubs majoritaires)
  ├── storage_core/    (Squelette vide - NON FONCTIONNEL)
  ├── reptile_storage/ (Wrapper NVS simple)
  └── ui/              (Interface Graphique)
```

**Outillage Détecté** :
*   **Cible** : `esp32s3` (Validé via code drivers)
*   **Build System** : CMake (Standard IDF)

---

## 3. Matrice de Conformité "Composants Attendus"

### 3.1 Hardware & Drivers (BSP)

| Composant | Statut | Preuve (Fichier) | Détails |
| :--- | :--- | :--- | :--- |
| **Board Bring-up** | 🟢 **OK** | `board.c:50` | Init séquentielle : I2C -> IOExt -> LCD Power -> LCD -> Backlight. |
| **Bus I²C Partagé** | 🟢 **OK** | `i2c_bus_shared.c` | Mutex `g_i2c_bus_mutex` global. Recovery activé (toggle SCL). |
| **IO Extension** | 🟢 **OK** | `io_extension.h` | Adresse `0x24`. Utilise `i2c_bus_shared`. Pas de traces CH422G. |
| **Touch GT911** | 🟢 **OK** | `gt911.c` | Task dédiée `gt911_irq`. Utilisation correcte du Mutex I2C. |
| **LVGL Port** | 🟢 **OK** | `lvgl_port.c` | Tâche dédiée. Sync VSYNC. Callbacks flush/read corrects. |
| **Stockage SD** | 🟢 **OK** | `sd.c` | Init robuste avec états. Gestion CS via IO Extender. |

### 3.2 Logiciel Métier (Application)

| Composant | Statut | Preuve | Détails |
| :--- | :--- | :--- | :--- |
| **Storage Core** | 🔴 **NON** | `storage_core.c` | Fichier quasi-vide. Aucune logique de migration/SHA/Backup. |
| **Domain Models** | 🟠 **PARTIEL** | `data_manager.c` | Seuls `Reptile`/`Event`/`Weight` existent. Manque `Document`, `Taxon`, etc. |
| **Compliance Rules** | 🔴 **NON** | N/A | Aucun fichier source trouvé pour le moteur de règles. |
| **Documents/Export** | 🔴 **NON** | `core_service.c` | Fonctions `core_export_csv` sont des "Stubs" (vide). |
| **UI Isolation** | 🟢 **OK** | `main.c` | UI déléguée à la tâche LVGL via `lvgl_port_set_ui_init_cb`. |

---

## 4. Compatibilité ESP-IDF v6.1

*   🟢 **Drivers I2C** : Utilisation du nouveau driver `driver/i2c_master.h` (`i2c_new_master_bus`). Pas de driver "legacy".
*   🟢 **Build System** : CMakeLists standard. Pas de hacks de chemins absolus détectés.
*   🟢 **FreeRTOS** : Utilisation correcte des priorités et du pinning (`xTaskCreatePinnedToCore` pour LVGL et Touch). Yields (`vTaskDelay`) présents dans les boucles critiques (main init, recovery).

## 5. Compatibilité LVGL 9.4

*   🟢 **API v9** : Utilisation de `lv_display_t`, `lv_display_create` (remplace `lv_disp_drv_t` de v8).
*   🟢 **Configuration** : `lv_conf.h` définit `LV_COLOR_DEPTH 16` et active les assertions/logs.
*   🟢 **Thread Safety** : Utilisation d'un Mutex Récursif `lvgl_mux` autour des appels LVGL timer handler.
*   🟢 **Flush** : Callback de flush implémente l'attente active (VSYNC) pour éviter le tearing.

---

## 6. Anomalies & Artefacts Détectés

1.  **Macros "EXAMPLE"** (Cosmétique) :
    *   `components/touch/gt911.h` : Lignes 47, 51. `EXAMPLE_PIN_NUM_TOUCH_RST`. À renommer en `ARS_` ou `GT911_`.
    *   `components/lvgl_port/lvgl_port.h` : `CONFIG_EXAMPLE_LVGL_PORT_...`. À nettoyer.

2.  **Stubs Métier** :
    *   `core_service.c` contient de nombreuses fonctions qui ne font que logger "Stub: ...".
    *   `storage_core` est inopérant.

---

## 7. Liste des Actions Prioritaires

### P0 - Critique (Bloquant)
*   *Aucun point bloquant le démarrage ou l'affichage n'a été trouvé.*

### P1 - Risque Maintenance / Fonctionnalité Manquante
1.  **Implémenter `storage_core`** : Le système de fichier manque de protection. En cas de corruption JSON ou de mise à jour de structure, l'app plantera. Il faut implémenter le versioning et les checksums.
2.  **Créer le Moteur de Règles** : La fonctionnalité "Compliance" est inexistante. Créer le composant `compliance_engine`.
3.  **Finaliser les Modèles** : Ajouter les structures manquantes (`Document`, `Contact`) dans `data_manager`.

### P2 - Qualité Code
1.  **Nettoyage Macros** : Remplacer toutes les occurrences de `EXAMPLE_` par `ARS_` dans `gt911.h` et `lvgl_port.h`.
2.  **Implémenter l'Export** : Coder la logique CSV dans `core_service.c`.

---

## Annexes : Commandes de Vérification

**Compiler et Flasher :**
```bash
idf.py build flash monitor
```

**Vérifier les logs de démarrage (Séquence attendue) :**
1.  `[board] IO expander init failed: ESP_OK` (ou logs détaillés IO)
2.  `[board] LCD VCOM/VDD Enabled`
3.  `[lv_port] LVGL DIRECT mode ready`
4.  `[sd] SD state -> INIT_OK` (si carte présente)

**Confirmer un "Stub" :**
Appeler une fonction d'export dans l'UI et vérifier que le log affiche `Stub: Export CSV...`.
