#include "espbase/boot/network_logger.hpp"

#include <cstdio>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "espbase/circular_history_buffer.hpp"

static constexpr char spa_html[] = {
#embed "network_logger.html"
    , '\0'  // Ensure null-termination
};

static vprintf_like_t original_vprintf_ = nullptr;
static constinit CircularHistoryBuffer buffer_;

static int log_hook(const char* fmt, va_list args) {
  char buf[256];
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  
  if (len > 0) {
    buffer_.write(buf, len);
  }
  return original_vprintf_(fmt, args);  // Still output to USB/UART
}

static esp_err_t stream_log_handler(httpd_req_t* req) {
  // Send as an infinite, raw chunked data stream
  httpd_resp_set_type(req, "text/plain");

  buffer_.register_listener(xTaskGetCurrentTaskHandle());

  uint64_t cursor = 0;
  char chunk[512];  // Stack-allocated buffer for the read chunk

  while (true) {
    size_t bytes = buffer_.read_next(cursor, chunk, sizeof(chunk));

    if (bytes > 0) {
      if (httpd_resp_send_chunk(req, chunk, bytes) != ESP_OK) {
        break;  // Connection dropped
      }
    } else {
      // Block until writer calls xTaskNotifyGive.
      // Timeout every 2 seconds to send a heartbeat space to keep the router happy.
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
        if (httpd_resp_send_chunk(req, " ", 1) != ESP_OK) break;
      }
    }
  }

  buffer_.remove_listener(xTaskGetCurrentTaskHandle());

  // Close the stream cleanly (usually unreachable due to connection drop)
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");

  // sizeof(spa_html) includes the null terminator, so we subtract 1
  return httpd_resp_send(req, spa_html, sizeof(spa_html) - 1);
}

void initialize_network_logger() {
  buffer_.init(256 * 1024);  // 256 KB circular buffer for logs
  original_vprintf_ = esp_log_set_vprintf(log_hook);
}

EspResult<httpd_handle_t> install_network_logger_routes(httpd_handle_t server) {
  if (!server) {
    // No server - start one.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Allow SPA, SSE stream, and standard REST requests concurrently
    config.max_open_sockets = 4;

    // Automatically kill old dead connections if a new client tries to connect
    config.lru_purge_enable = true;

    if (EspError err = httpd_start(&server, &config)) {
      return err.log("NetworkLogger", "Failed to start HTTP server");
    }
    ESP_LOGI("HTTP", "Server started on port %d", config.server_port);
  }

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = nullptr,
  };
  if (EspError err = httpd_register_uri_handler(server, &index_uri)) {
    httpd_stop(server);
    return err.log("HTTP", "Failed to register index handler");
  }

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_log_handler,
      .user_ctx = nullptr,
  };
  if (EspError err = httpd_register_uri_handler(server, &stream_uri)) {
    httpd_stop(server);
    return err.log("HTTP", "Failed to register stream handler");
  }
  return server;
}
