#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Définition complète de la structure pour permettre l'accès aux champs (message, etc.)
// et l'itération par index (alerts[i]) dans l'UI.
typedef struct core_alert_s {
    char message[128];
    // On pourra ajouter ici : timestamp, level, titre, etc.
} core_alert_t;

/**
 * @brief Récupère la liste des alertes actives.
 * 
 * @param[out] alerts Pointeur vers le tableau d'alertes alloué.
 * @param[out] count  Nombre d'alertes récupérées.
 * @return esp_err_t ESP_OK en cas de succès.
 */
esp_err_t core_get_alerts(core_alert_t **alerts, size_t *count);

/**
 * @brief Libère la mémoire allouée pour la liste d'alertes.
 * 
 * @param[in] alerts Tableau d'alertes à libérer.
 * @param[in] count  Nombre d'éléments (pour information/contrôle).
 */
void core_free_alert_list(core_alert_t *alerts, size_t count);

#ifdef __cplusplus
}
#endif
