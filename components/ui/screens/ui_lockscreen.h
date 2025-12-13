#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the Lock Screen.
 * Blocks access until correct PIN is entered.
 */
void ui_create_lockscreen(void);

#ifdef __cplusplus
}
#endif