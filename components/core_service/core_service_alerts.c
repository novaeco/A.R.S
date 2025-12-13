#include "core_service_alerts.h"
#include <stddef.h>

esp_err_t core_get_alerts(core_alert_t **alerts, size_t *count) {
  if (alerts == NULL || count == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Version initiale : aucune alerte stockée pour le moment.
  // On retourne 'NULL' et count=0, ce qui est un état valide "pas d'alertes".
  *alerts = NULL;
  *count = 0;

  return ESP_OK;
}

void core_free_alert_list(core_alert_t *alerts, size_t count) {
  (void)count; // Non utilisé pour une allocation simple ou NULL

  if (alerts != NULL) {
    // Si alerts a été alloué dynamiquement (malloc/calloc), il faudrait le free
    // ici. Dans cette version stub où on renvoie toujours NULL, il n'y a rien à
    // faire. Mais par sécurité/bonne pratique pour le futur : free(alerts);
  }
}
