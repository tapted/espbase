#pragma once

#include "espbase/esp_result.hpp"

typedef void* httpd_handle_t;

EspResult<void> install_favicon_route(httpd_handle_t server);