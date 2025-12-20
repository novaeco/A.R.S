# Directives de Correction et Bonnes Pratiques ESP-IDF v6.1

Ce document détaille les corrections appliquées au projet pour garantir une compilation sans erreur avec ESP-IDF v6.1 et suivre les recommandations d'Espressif.

## 1. Gestion des Dépendances Pilotes (Split Drivers)

Dans les versions récentes d'ESP-IDF (v5.x/v6.x), les pilotes monolithiques ont été scindés. Il est nécessaire de déclarer les dépendances spécifiques.

### Composant `gpio` et `esp_driver_ledc`
- **Erreur corrigée :** `driver/ledc.h` introuvable.
- **Cause :** `driver/ledc.h` appartient désormais au composant `esp_driver_ledc`.
- **Solution appliquée :**
  - L'inclusion `#include "driver/ledc.h"` est bien située dans `gpio.c` (et non `gpio.h`), ce qui est une bonne pratique pour réduire le couplage.
  - Dans `components/gpio/CMakeLists.txt`, nous avons déclaré la dépendance en **privé** :
    ```cmake
    PRIV_REQUIRES esp_driver_ledc
    ```
    Cela évite d'exposer les symboles LEDC aux utilisateurs du composant `gpio`.

### Composant `i2c` et `esp_driver_i2c`
- **Erreur corrigée :** `driver/i2c_master.h` introuvable.
- **Cause :** Ce header appartient au composant `esp_driver_i2c`.
- **Solution validée :**
  - Le fichier `components/i2c/CMakeLists.txt` contient bien :
    ```cmake
    REQUIRES esp_driver_i2c
    ```
  - C'est nécessaire car `i2c.h` inclut `driver/i2c_master.h`, donc la dépendance doit être publique (`REQUIRES`) pour que les composants utilisant `i2c` puissent aussi voir les types I2C.

## 2. Avertissements CMake : `esp_wifi` et `wpa_supplicant`

Des avertissements peuvent apparaître concernant l'utilisation de répertoires d'inclusion privés entre `esp_wifi` et `wpa_supplicant`.

- **Explication :** Ces composants sont étroitement liés. Si vous deviez modifier leur configuration (ex: patch local), la recommandation officielle est d'utiliser `PRIV_REQUIRES` pour expliciter ce lien sans exposer les headers privés au reste du projet.
- **Directive :** Si ces avertissements proviennent du framework ESP-IDF lui-même (dans `managed_components` ou le SDK global), ils sont informatifs et n'empêchent pas la compilation. Si vous contrôlez ces composants, ajoutez :
  - Dans `esp_wifi/CMakeLists.txt`: `PRIV_REQUIRES wpa_supplicant`
  - Dans `wpa_supplicant/CMakeLists.txt`: `PRIV_REQUIRES esp_wifi`
  - Utilisez `PRIV_INCLUDE_DIRS` si nécessaire pour pointer vers les headers internes.

## 3. Structure des Composants

Pour éviter les erreurs de compilation (fichiers introuvables, cibles manquantes) :

1.  **Dossiers :** Chaque sous-dossier de `components/` doit contenir un **CMakeLists.txt**.
2.  **Contenu Minimal :** Si un dossier comme `A-voir` n'a pas de sources, son `CMakeLists.txt` doit au moins contenir `idf_component_register()`.
    - *Note :* Il est recommandé de simplement supprimer les dossiers vides ou inutilisés (comme `A-voir`) s'ils ne servent pas, pour ne pas encombrer le système de build.
3.  **Nommage :** Évitez d'utiliser des noms de composants qui existent déjà dans ESP-IDF (comme `gpio`, `i2c`, `console`, etc.) pour vos propres composants, car cela peut créer des conflits d'inclusion. Préférez des préfixes, par exemple `board_gpio`, `custom_i2c`.

## 4. Nettoyage de la configuration

- Les options obsolètes (comme `CCACHE_ENABLE`) doivent être retirées de `sdkconfig.defaults` car elles déclenchent des warnings et sont ignorées par les versions récentes de CMake/ESP-IDF.
- Le fichier `sdkconfig.defaults` a été vérifié et nettoyé.

## 5. Validation

Après ces corrections, la procédure de validation recommandée est :
```bash
idf.py fullclean
idf.py build
```
Cela garantit que toutes les dépendances sont régénérées proprement.
