# components/web_server/AGENTS — Web server rules (Strict)

## Scope
Covers HTTP server code, endpoints, and related Kconfig options under `components/web_server`.

## Expectations
- Use ESP-IDF HTTP(S) server APIs; avoid custom socket loops that could block other tasks.
- Never log credentials, session tokens, or personal data; sanitize inputs and validate lengths.
- Keep endpoints optional and fail-closed: if provisioning data is missing, return clear errors without crashing.
- TLS (if enabled) must use repo-trusted certificates/PSKs only; do not hardcode secrets in source.
- Provide concise route documentation and include error handling paths in tests or examples.

## Definition of Done
- Build: `idf.py fullclean build` passe.
- Boot: aucun panic/assert; serveur HTTP peut être désactivé sans bloquer UI.
- Logs: TAG web_server indique démarrage/endpoints et erreurs avec `esp_err_to_name` sans fuite de données sensibles.
- Threading: handlers non bloquants; sockets/threads ferment proprement en cas d’erreur ou d’arrêt.
