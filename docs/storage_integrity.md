# Intégrité du stockage

## Principes
- Mutexe `storage_core` pour sérialiser l'accès aux données.
- Persistance future : fichiers JSON/CBOR versionnés sur SD ou SPIFFS.
- Journal minimal avant mise à jour pour rollback en cas de coupure.

## Migrations
1. Lire la version courante du schéma.
2. Appliquer les migrations séquentiellement vers la version cible.
3. Vérifier CRC/fingerprint des blocs après migration.
4. En cas d'échec, restaurer le snapshot précédent.

## Contrôles d'intégrité
- Empreinte SHA-256 des fichiers de documents.
- CRC32 sur métadonnées (structures sérialisées) pour détection de corruption.
- Log structuré des erreurs (pas de secrets).

## Stratégie en absence de SD
- Continuer en mémoire (MVP) avec données par défaut.
- Avertir l'utilisateur que la persistance est désactivée.
