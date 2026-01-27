#pragma once

#include "settings.h"

void storage_init(void);
void storage_load_settings(app_settings_t *settings);
void storage_save_settings(const app_settings_t *settings);
