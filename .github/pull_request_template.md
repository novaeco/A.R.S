## Résumé
- [ ] Description courte des changements (1 à 2 puces)

## Check-list revue P0 (obligatoire)
- [ ] Check-list revue [docs/REVIEW_CHECKLIST.md](docs/REVIEW_CHECKLIST.md) parcourue et conforme (fail-safe SD, interdictions CH422G/sd_waveshare, politique dépendances, logs).
- [ ] Pas d’ajout de dépendances tierces non prévues ni de composants interdits.
- [ ] Conformité CMake/Kconfig vérifiée pour chaque composant touché.

## Tests
- [ ] `python tools/lint_components.py`
- [ ] `idf.py fullclean`
- [ ] `idf.py set-target esp32s3`
- [ ] `idf.py build`
- [ ] Autres (préciser) :
