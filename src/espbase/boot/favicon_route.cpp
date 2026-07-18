#include "espbase/boot/favicon_route.hpp"

#include <algorithm>
#include <cmath>
#include <esp_heap_caps.h>
#include <esp_http_server.h>

namespace {

static void write_ico_and_dib_headers(uint8_t* buf, int w, int h, int pixel_size) {
  // --- ICO Header (6 bytes) ---
  buf[0] = 0;
  buf[1] = 0;  // Reserved
  buf[2] = 1;
  buf[3] = 0;  // Type: 1 (ICO)
  buf[4] = 1;
  buf[5] = 0;  // Image Count: 1

  // --- ICO Directory Entry (16 bytes) ---
  buf[6] = w;  // Width
  buf[7] = h;  // Height
  buf[8] = 0;  // Colors (0 = truecolor)
  buf[9] = 0;  // Reserved
  buf[10] = 1;
  buf[11] = 0;  // Color Planes
  buf[12] = 32;
  buf[13] = 0;  // BPP

  uint32_t data_size = 40 + pixel_size + ((w * h) / 8);
  buf[14] = data_size & 0xFF;
  buf[15] = (data_size >> 8) & 0xFF;
  buf[16] = (data_size >> 16) & 0xFF;
  buf[17] = (data_size >> 24) & 0xFF;

  uint32_t offset = 22;  // Data starts immediately after headers
  buf[18] = offset & 0xFF;
  buf[19] = (offset >> 8) & 0xFF;
  buf[20] = (offset >> 16) & 0xFF;
  buf[21] = (offset >> 24) & 0xFF;

  // --- DIB Header (40 bytes) ---
  uint8_t* dib = buf + 22;
  dib[0] = 40;
  dib[1] = 0;
  dib[2] = 0;
  dib[3] = 0;  // DIB Size

  dib[4] = w & 0xFF;
  dib[5] = (w >> 8) & 0xFF;
  dib[6] = (w >> 16) & 0xFF;
  dib[7] = (w >> 24) & 0xFF;

  // CRITICAL: ICO spec demands the DIB height is DOUBLE the actual image height
  uint32_t dib_h = h * 2;
  dib[8] = dib_h & 0xFF;
  dib[9] = (dib_h >> 8) & 0xFF;
  dib[10] = (dib_h >> 16) & 0xFF;
  dib[11] = (dib_h >> 24) & 0xFF;

  dib[12] = 1;
  dib[13] = 0;  // Planes
  dib[14] = 32;
  dib[15] = 0;  // BPP

  // Compression (0 = BI_RGB)
  dib[16] = 0;
  dib[17] = 0;
  dib[18] = 0;
  dib[19] = 0;

  dib[20] = pixel_size & 0xFF;
  dib[21] = (pixel_size >> 8) & 0xFF;
  dib[22] = (pixel_size >> 16) & 0xFF;
  dib[23] = (pixel_size >> 24) & 0xFF;
  // Remaining 16 bytes of DIB header naturally remain 0 (handled by calloc)
}

// High-effort procedural drawing: A sleek, 3D ESP-Puck with a neon rim light
static void draw_procedural_puck(uint8_t* pixels, int w, int h) {
  const float center_x = w / 2.0f;
  const float center_y = h / 2.0f;
  const float radius = (w / 2.0f) - 1.5f;

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // +0.5f samples the center of the pixel
      float dx = x - center_x + 0.5f;
      float dy = y - center_y + 0.5f;
      float dist = std::sqrt(dx * dx + dy * dy);

      // Note: BMP/ICO pixel arrays are oriented BOTTOM-UP
      int idx = (y * w + x) * 4;

      if (dist > radius + 1.0f) {
        // Fully transparent exterior
        pixels[idx] = pixels[idx + 1] = pixels[idx + 2] = pixels[idx + 3] = 0;
      } else {
        // 1. Anti-aliasing alpha blend for a perfectly smooth circle
        float alpha = std::clamp(radius + 1.0f - dist, 0.0f, 1.0f);

        // 2. Base Color: Sleek slate-grey gradient fading downwards
        float gradient = std::clamp(1.0f - (dy + radius) / (2.0f * radius), 0.0f, 1.0f);
        uint8_t base = 30 + (uint8_t)(gradient * 50);

        uint8_t r = base, g = base, b = base;

        // 3. Neon Cyan Rim Light
        // In a bottom-up coordinate space, top-left means dx < 0 and dy > 0
        if (dist > radius - 4.0f && dx < 0 && dy > 0) {
          float rim = std::clamp(dist - (radius - 4.0f), 0.0f, 1.0f) *
                      std::clamp(-dx / radius, 0.0f, 1.0f) * std::clamp(dy / radius, 0.0f, 1.0f);

          // Add synthwave cyan highlights
          r = std::min(255, r + (int)(rim * 50));
          g = std::min(255, g + (int)(rim * 180));
          b = std::min(255, b + (int)(rim * 255));
        }

        // Write BGRA
        pixels[idx + 0] = b;
        pixels[idx + 1] = g;
        pixels[idx + 2] = r;
        pixels[idx + 3] = (uint8_t)(alpha * 255.0f);
      }
    }
  }
}

static esp_err_t get_handler(httpd_req_t* req) {
  constexpr int width = 32;
  constexpr int height = 32;
  constexpr int bpp = 4;

  constexpr int header_size = 62;
  constexpr int pixel_data_size = width * height * bpp;
  constexpr int and_mask_size = (width * height) / 8;  // Required by ICO spec

  constexpr int total_size = header_size + pixel_data_size + and_mask_size;

  // 1. Allocate buffer (Try PSRAM first, fallback to internal SRAM)
  // calloc ensures the trailing AND mask is zeroed out (which means "use alpha channel")
  uint8_t* buf = (uint8_t*)heap_caps_calloc(1, total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    buf = (uint8_t*)heap_caps_calloc(1, total_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
  }

  // 2. Write Headers
  write_ico_and_dib_headers(buf, width, height, pixel_data_size);

  // 3. Draw Pixels
  uint8_t* pixels = buf + header_size;
  draw_procedural_puck(pixels, width, height);

  // 4. Send Response
  httpd_resp_set_type(req, "image/x-icon");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");  // Cache for 24h
  esp_err_t res = httpd_resp_send(req, (const char*)buf, total_size);

  // 5. Cleanup
  heap_caps_free(buf);
  return res;
}

}  // namespace

EspResult<void> install_favicon_route(httpd_handle_t server) {
  httpd_uri_t favicon_uri = {
      .uri = "/favicon.ico",
      .method = HTTP_GET,
      .handler = get_handler,
      .user_ctx = nullptr,
  };
  return httpd_register_uri_handler(server, &favicon_uri);
}
