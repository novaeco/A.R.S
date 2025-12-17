# Check-list revue P0 (A.R.S)

Cette check-list obligatoire formalise les règles racine du dépôt pour éviter les régressions matérielles/boot et les dépendances interdites. Elle doit être parcourue pour chaque PR avant validation.

## 1. Sécurité boot & SD (fail-safe)
- [ ] L'initialisation SD reste **optionnelle** : en cas d'échec, l'application continue à booter sans panic ni abort.
- [ ] Le pipeline SDSPI respecte la séquence standard (CMD0/CMD8/ACMD41/CMD58) avec fréquence d'init conservatrice et basculement CS externe correct.
- [ ] Aucun changement n'introduit de blocage ou de tentative d'accès SD dans des contextes interdits (ISR, tâches longues non sérialisées).

## 2. Interdictions matérielles
- [ ] Aucun code/artefact lié au **CH422G** n'est ajouté (sources, includes, probing, CMake/Kconfig, pins hérités).
- [ ] Aucun code/artefact lié à `components/sd_waveshare` n'est ajouté ou référencé.

## 3. Politique dépendances
- [ ] Les dépendances restent limitées aux APIs ESP-IDF natives ou déjà présentes; aucune bibliothèque tierce nouvelle n'est ajoutée.
- [ ] Les composants respectent les dépendances existantes (pas de doublon ou alias de composants système).

## 4. Conformité composants (CMake/Kconfig)
- [ ] Chaque composant possède un `CMakeLists.txt` contenant un `idf_component_register(...)` complet.
- [ ] Les fichiers `Kconfig` présents sont non vides et exposent uniquement des options pertinentes au composant.
- [ ] Le lint automatisé `python tools/lint_components.py` est passé et propre.

## 5. Traceabilité & logs
- [ ] Les journaux de boot conservent le caractère synthétique attendu (display/touch/SD/Wi‑Fi) sans fuite de secrets.
- [ ] Aucun identifiant Wi‑Fi ni secret n'est logué.
