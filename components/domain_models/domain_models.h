#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct storage_context storage_context_t;

typedef struct {
    char id[16];
    char species_id[16];
    char sex[8];
    char birth_date[12];
    char acquisition_date[12];
    char origin[32];
    char status[16];
    char identifiers[64];
    char location[32];
} animal_record;

typedef struct {
    char id[16];
    char scientific_name[64];
    char regulatory_status[64];
    char notes[128];
} taxon_record;

typedef struct {
    char id[16];
    char type[32];
    char scope[32];
    char reference[64];
    char file_path[128];
    char fingerprint[64];
    bool valid;
} document_record;

typedef struct {
    char id[16];
    char type[32];
    char timestamp[24];
    char actor[32];
    char related_animal[16];
    char related_document[16];
    char note[128];
} event_record;

typedef struct {
    char id[16];
    char type[16];
    char status[16];
    char due_date[12];
    char related_animal[16];
} due_record;

esp_err_t domain_models_bootstrap_if_empty(storage_context_t *ctx);
const event_record *domain_models_get_timeline(const storage_context_t *ctx, size_t *count);
