# UI / UX

## Ligne directrice
- Style moderne et apaisé, teintes sombres organiques.
- Lisibilité et espacement généreux pour tablette 1024×600.
- Navigation fixe : top bar (heure + statuts Wi-Fi/SD) + barre basse (Dashboard, Animaux, Export, Echéances, Documents, Conformité, Paramètres).

## Écrans MVP
- **Dashboard** : synthèse des compteurs (animaux, documents, événements).
- **Animaux** : liste + timeline par animal + accès export.
- **Export** : génération du dossier animal CSV sur SD (si montée).
- **Echéances** : liste issue du timeline en attendant service dédié.
- **Documents** : index des pièces (stub).
- **Conformité** : checklist des règles et état OK/manquant.
- **Paramètres** : placeholder pour profil élevage et juridiction.

## Interaction tactile
- GT911 via I²C, IRQ et reset par IO extender. Transformation basique (modulo) en attendant calibration.
- Tous les callbacks UI exécutés dans la tâche LVGL (`lv_loop`).

## Assets
- Pas d'assets externes requis pour le MVP. Prévoir support PNG/BMP si ajout d'icônes (activer LVGL_USE_PNG/BMP au besoin).
