# Modèle de données

## Entités principales
- **Animal** : id, species_id, sexe, dates (naissance/entrée), origine, statut, identifiants, localisation.
- **Taxon** : nom scientifique, statut réglementaire, notes.
- **Document** : id, type, scope, dates, référence, fichier, empreinte, liens.
- **Événement** : id, type, timestamp, acteur, liens doc/animal.
- **Échéance** : obligation/renouvellement/contrôle, état, date, relation.
- **Transaction** : achat/vente, parties, justificatifs.

## Versioning & sérialisation
- Prévu : version de schéma dans chaque blob JSON/CBOR, ex : `{ "schema_version": 1, "animals": [...] }`.
- Migrations : appliquées avant chargement en RAM, rollback si erreur (journal minimal).

## Identifiants
- IDs stables de type chaîne courte (ex: `A-001`, `D-001`).
- Relations par clés (pas de pointeurs directs) pour sérialisation simple.

## Empreintes
- Fingerprint des fichiers : SHA-256 (stub actuel), stocké en hex.
- CRC des métadonnées possible pour vérification rapide.
