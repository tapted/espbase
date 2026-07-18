#pragma once

#include "espbase/esp_result.hpp"

typedef void* httpd_handle_t;

void initialize_network_logger(size_t size_bytes = 256 * 1024, bool use_psram = true);
EspResult<httpd_handle_t> install_network_logger_routes(httpd_handle_t server);