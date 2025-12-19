# components/net/AGENTS — Wi-Fi provisioning and network state (Strict)

## 1) Provisioning state machine
- If not provisioned:
  - do not attempt connect
  - expose UI path to configure
- If provisioned:
  - connect with retries (bounded)
  - expose status to UI

## 2) Credentials policy
- Never log SSID+password together.
- Password must never appear in plain logs.

## 3) Resilience
- Wi-Fi failure must not stop UI.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; absence de provisionnement n’entraîne pas de connexions intempestives.
- Logs: TAG net affiche état provisionnement/connexion et erreurs avec `esp_err_to_name` sans divulguer SSID/mdp.
- Threading: retries bornés et callbacks réseau ne bloquent pas la tâche UI.
