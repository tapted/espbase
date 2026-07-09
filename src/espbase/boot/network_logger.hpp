#pragma once

#include "espbase/esp_result.hpp"

struct httpd_req;
typedef struct httpd_req httpd_req_t;
typedef void* httpd_handle_t;

void initialize_network_logger();
EspResult<httpd_handle_t> install_network_logger_routes(httpd_handle_t server);