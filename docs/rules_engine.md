# Moteur de règles

## Principes
- Règles data-driven (JSON/CBOR) : id, titre, sévérité, scope, preuve attendue, expression logique.
- Evaluation périodique ou à la demande, production d'une checklist (OK / manquant / blocant).
- Sévérités proposées : `haute`, `moyenne`, `basse`.

## Format JSON cible (exemple)
```json
{
  "schema_version": 1,
  "rules": [
    {"id": "R-001", "title": "Document d'origine requis", "severity": "haute", "scope": "animal", "evidence": "Certificat"},
    {"id": "R-002", "title": "Identification marquée", "severity": "moyenne", "scope": "animal", "evidence": "RFID"}
  ]
}
```

## Implémentation MVP
- Règles codées en dur dans `compliance_rules` (3 règles) pour démonstration.
- Evaluation : vérifie présence de documents et d'un identifiant dans les données en mémoire.

## Extensions prévues
- Charger les règles depuis SD ou flash, validation de schéma.
- Ajout d'opérateurs temporels (échéance dépassée, renouvellement à <30 jours).
- Historique des évaluations pour traçabilité.
