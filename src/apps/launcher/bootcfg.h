#pragma once

#include <stdbool.h>
#include <esp_err.h>

bool runtimeOnceBootRequested();
esp_err_t clearRuntimeBootRequest();
esp_err_t writeRuntimeOnceBootRequest();
