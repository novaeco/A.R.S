## Symptômes
- `idf.py monitor` échoue sous Windows avec `GetOverlappedResult failed (PermissionError(13, 'Accès refusé.', ...))`.
- Port série non accessible ou coupé après un reset USB de l’ESP32-S3.

## Causes probables
- Le port COM est déjà ouvert par un autre outil (IDE, terminal série, antivirus qui scanne les ports).
- L’USB se ré-énumère après un reset RTS/DTR, ce qui invalide le handle COM existant.
- Pilote USB/UART absent ou corrompu (CDC ou driver spécifique du module).
- Droits insuffisants ou port marqué « Occupé » par Windows après un crash de monitor.
- Câble ou port USB instable (chute d’alimentation pendant le reset automatique).

## Check-list d’actions
1. Fermer tous les autres moniteurs série (IDE, terminals externes) avant `idf.py monitor`.
2. Vérifier dans le Gestionnaire de périphériques que le port COM de l’ESP32-S3 est présent et sans icône d’erreur.
3. Rebrancher le câble USB ou essayer un autre port USB si le périphérique disparaît après le reset.
4. Si le port reste « occupé », débrancher/rebrancher pour forcer une nouvelle énumération puis relancer le terminal.
5. Mettre à jour ou réinstaller le pilote USB/UART utilisé par la carte si le périphérique apparaît avec un avertissement.
6. Relancer `idf.py monitor` dans une nouvelle session de terminal pour éviter un handle COM bloqué.

## Commandes utiles
- Lancer le monitor (remplacer `COMx` par le port détecté) :  
  `idf.py -p COMx monitor`
- Relancer le monitor après un flash si nécessaire :  
  `idf.py -p COMx flash monitor`
- Ajuster le débit si la carte utilise un baud différent (exemple) :  
  `idf.py -p COMx monitor --baud 115200`

## Si le problème persiste
- Tester le port COM avec un autre terminal série pour confirmer qu’il s’ouvre correctement.
- Observer les logs Windows (Observateur d’événements) pour repérer des erreurs de pilote USB.
- Essayer sur un autre câble/PC pour isoler un problème matériel.
