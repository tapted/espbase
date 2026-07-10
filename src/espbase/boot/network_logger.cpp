#include "espbase/boot/network_logger.hpp"

#include <algorithm>
#include <cstdio>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

static constexpr char spa_html[] = {
#embed "network_logger.html"
    , '\0'  // Ensure null-termination
};

static RingbufHandle_t log_ringbuf_ = nullptr;
static vprintf_like_t original_vprintf_ = nullptr;
static StaticRingbuffer_t static_ringbuf_ = {};

static int log_hook(const char* fmt, va_list args) {
  char buf[256];
  int len = vsnprintf(buf, sizeof(buf), fmt, args);

  if (len > 0 && log_ringbuf_) {
    xRingbufferSend(log_ringbuf_, buf, std::min<int>(len, sizeof(buf)), 0);  // No timeout.
  }
  return original_vprintf_(fmt, args);  // Still output to USB/UART
}

static esp_err_t sse_log_handler(httpd_req_t* req) {
  // Set headers for a persistent SSE connection
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");

  while (true) {
    size_t item_size;
    // Block for up to 1 second waiting for new logs
    char* item = (char*)xRingbufferReceive(log_ringbuf_, &item_size, pdMS_TO_TICKS(1000));

    if (item) {
      // SSE format: "data: <payload>\n\n"
      httpd_resp_send_chunk(req, "data: ", 6);
      httpd_resp_send_chunk(req, item, item_size);
      httpd_resp_send_chunk(req, "\n\n", 2);

      // Return memory back to the ringbuffer
      vRingbufferReturnItem(log_ringbuf_, item);
    } else {
      // Send a keep-alive ping to prevent browser/router timeouts
      if (httpd_resp_send_chunk(req, ": keepalive\n\n", 13) != ESP_OK) {
        break;  // Client disconnected
      }
    }
  }
  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");

  // sizeof(spa_html) includes the null terminator, so we subtract 1
  return httpd_resp_send(req, spa_html, sizeof(spa_html) - 1);
}

void initialize_network_logger() {
  uint8_t* psram_buf = (uint8_t*)heap_caps_malloc(256 * 1024, MALLOC_CAP_SPIRAM);
  log_ringbuf_ =
      xRingbufferCreateStatic(256 * 1024, RINGBUF_TYPE_BYTEBUF, psram_buf, &static_ringbuf_);
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
      .handler = sse_log_handler,
      .user_ctx = nullptr,
  };
  if (EspError err = httpd_register_uri_handler(server, &stream_uri)) {
    httpd_stop(server);
    return err.log("HTTP", "Failed to register stream handler");
  }
  return server;
}
