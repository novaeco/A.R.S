#include "domain_models.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "domain_models";

static event_record s_timeline[4];

void domain_models_register_builtin(storage_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    static animal_record animals[1];
    memset(&animals[0], 0, sizeof(animal_record));
    strcpy(animals[0].id, "A-001");
    strcpy(animals[0].species_id, "T-001");
    strcpy(animals[0].sex, "F");
    strcpy(animals[0].birth_date, "2023-03-12");
    strcpy(animals[0].acquisition_date, "2023-05-01");
    strcpy(animals[0].origin, "Captive bred");
    strcpy(animals[0].status, "Actif");
    strcpy(animals[0].identifiers, "RFID:123456789");
    strcpy(animals[0].location, "Rack A1");

    static document_record documents[1];
    memset(&documents[0], 0, sizeof(document_record));
    strcpy(documents[0].id, "D-001");
    strcpy(documents[0].type, "Certificat");
    strcpy(documents[0].scope, "animal");
    strcpy(documents[0].reference, "REF-2024-01");
    strcpy(documents[0].fingerprint, "stub-sha");
    documents[0].valid = true;

    ctx->animals = animals;
    ctx->animal_count = 1;
    ctx->documents = documents;
    ctx->document_count = 1;

    memset(s_timeline, 0, sizeof(s_timeline));
    strcpy(s_timeline[0].id, "E-001");
    strcpy(s_timeline[0].type, "Acquisition");
    strcpy(s_timeline[0].timestamp, "2023-05-01");
    strcpy(s_timeline[0].actor, "Fournisseur X");
    strcpy(s_timeline[0].related_animal, "A-001");

    strcpy(s_timeline[1].id, "E-002");
    strcpy(s_timeline[1].type, "Mue");
    strcpy(s_timeline[1].timestamp, "2024-01-10");
    strcpy(s_timeline[1].actor, "Auto");
    strcpy(s_timeline[1].related_animal, "A-001");

    strcpy(s_timeline[2].id, "E-003");
    strcpy(s_timeline[2].type, "Controle");
    strcpy(s_timeline[2].timestamp, "2024-03-05");
    strcpy(s_timeline[2].actor, "Vet");
    strcpy(s_timeline[2].related_animal, "A-001");

    strcpy(s_timeline[3].id, "E-004");
    strcpy(s_timeline[3].type, "Document");
    strcpy(s_timeline[3].timestamp, "2024-05-02");
    strcpy(s_timeline[3].actor, "Admin");
    strcpy(s_timeline[3].related_animal, "A-001");
    strcpy(s_timeline[3].related_document, "D-001");

    ESP_LOGI(TAG, "Builtin domain models loaded");
}

const event_record *domain_models_get_timeline(size_t *count)
{
    if (count) {
        *count = sizeof(s_timeline) / sizeof(s_timeline[0]);
    }
    return s_timeline;
}
