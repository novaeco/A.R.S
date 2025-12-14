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
