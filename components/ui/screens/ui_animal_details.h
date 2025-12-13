#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the Animal Details Screen (Overview, Weights, Events).
 * 
 * @param animal_id The UUID of the animal to display.
 */
void ui_create_animal_details_screen(const char *animal_id);

#ifdef __cplusplus
}
#endif