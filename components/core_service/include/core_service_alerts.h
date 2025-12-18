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
 * L'appelant devient propriétaire du tableau renvoyé et doit appeler
 * `core_free_alert_list()` pour libérer la ressource, même lorsque aucune
 * alerte n'est disponible (dans ce cas `*alerts` vaut NULL et `*count` est 0).
 *
 * @param[out] alerts Pointeur vers le tableau d'alertes alloué (NULL si aucune).
 * @param[out] count  Nombre d'alertes récupérées (0 si aucune).
 * @return esp_err_t ESP_OK en cas de succès, ESP_ERR_INVALID_ARG si un pointeur
 *         requis est NULL.
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
