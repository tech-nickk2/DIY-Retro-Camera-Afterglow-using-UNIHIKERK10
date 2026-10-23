#include "display.h"
#include <string.h>
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "xl9535.h"
#include "../board/k10_pins.h"

static const char* TAG = "display";
static constexpr int PANEL_W = 240;  // native portrait
static constexpr int PANEL_H = 320;
static constexpr int CHUNK_ROWS = 20;  // panel rows per bounce buffer
static constexpr size_t CHUNK_BYTES = PANEL_W * CHUNK_ROWS * 2;
static constexpr uint8_t MADCTL_PORTRAIT = 0x88;  // MY | BGR

static esp_lcd_panel_io_handle_t s_io = nullptr;
static uint8_t* s_bounce[2] = {nullptr, nullptr};
static SemaphoreHandle_t s_free = nullptr;  // counts bounce buffers not in flight
static int s_rot = 0;

static bool IRAM_ATTR onDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_free, &woken);
  return woken == pdTRUE;
}

static void cmd(uint8_t c, const uint8_t* data = nullptr, size_t n = 0) {
  esp_lcd_panel_io_tx_param(s_io, c, data, n);
}
template <size_t N>
static void cmd(uint8_t c, const uint8_t (&data)[N]) { cmd(c, data, N); }

namespace display {

bool begin(int rotation) {
  spi_bus_config_t bus = {};
  bus.sclk_io_num = K10_LCD_SCLK;
  bus.mosi_io_num = K10_LCD_MOSI;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = CHUNK_BYTES + 16;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed");
    return false;
  }
  esp_lcd_panel_io_spi_config_t io = {};
  io.dc_gpio_num = K10_LCD_DC;
  io.cs_gpio_num = K10_LCD_CS;
  io.pclk_hz = K10_LCD_HZ;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  io.spi_mode = 0;
  io.trans_queue_depth = 4;
  io.on_color_trans_done = onDone;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_io) != ESP_OK) {
    ESP_LOGE(TAG, "panel IO init failed");
    return false;
  }
  for (auto& b : s_bounce) {
    b = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b) { ESP_LOGE(TAG, "no DMA memory"); return false; }
  }
  s_free = xSemaphoreCreateCounting(2, 2);

  // The panel has no reset pin (RC reset at power-up), so start with a software reset.
  cmd(0x01);
  vTaskDelay(pdMS_TO_TICKS(120));
  cmd(0xCF, {0x00, 0xC1, 0x30});
  cmd(0xED, {0x64, 0x03, 0x12, 0x81});
  cmd(0xE8, {0x85, 0x00, 0x78});
  cmd(0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02});
  cmd(0xF7, {0x20});
  cmd(0xEA, {0x00, 0x00});
  cmd(0xC0, {0x23});        // power control 1
  cmd(0xC1, {0x10});        // power control 2
  cmd(0xC5, {0x3E, 0x28});  // VCOM
  cmd(0xC7, {0x86});
  s_rot = rotation % ROTATION_COUNT;
  cmd(0x36, {MADCTL_PORTRAIT});
  cmd(0x3A, {0x55});        // 16 bits per pixel
  cmd(0xB1, {0x00, 0x18});  // ~79 Hz refresh
  cmd(0xB6, {0x08, 0x82, 0x27});
  cmd(0xF2, {0x00});
  cmd(0x26, {0x01});
  cmd(0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00});
  cmd(0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F});
  cmd(0x11);                // sleep out
  vTaskDelay(pdMS_TO_TICKS(120));
  cmd(0x29);                // display on
  return true;
}

void setRotation(int r) { s_rot = r % ROTATION_COUNT; }
int rotation() { return s_rot; }

// Fills panel rows py0..py0+CHUNK_ROWS-1 from the landscape canvas. Walking the
// canvas row by row keeps PSRAM reads sequential; the scattered writes land in
// internal RAM, which is cheap.
static void rotateChunk(const uint16_t* fb, int py0, uint16_t* dst) {
  for (int y = 0; y < H; y++) {
    const uint16_t* src = fb + y * W;
    if (s_rot == 0) {
      // USB-C left: canvas x = panel y, canvas y = 239 - panel x
      uint16_t* d = dst + (PANEL_W - 1 - y);
      for (int k = 0; k < CHUNK_ROWS; k++) d[k * PANEL_W] = src[py0 + k];
    } else {
      // USB-C right: canvas x = 319 - panel y, canvas y = panel x
      uint16_t* d = dst + y;
      for (int k = 0; k < CHUNK_ROWS; k++) d[k * PANEL_W] = src[W - 1 - py0 - k];
    }
  }
}

void push(const uint16_t* fb) {
  if (!s_io) return;
  const uint8_t caset[] = {0, 0, (PANEL_W - 1) >> 8, (PANEL_W - 1) & 0xFF};
  const uint8_t paset[] = {0, 0, (PANEL_H - 1) >> 8, (PANEL_H - 1) & 0xFF};
  cmd(0x2A, caset);
  cmd(0x2B, paset);
  int which = 0;
  for (int py = 0; py < PANEL_H; py += CHUNK_ROWS) {
    xSemaphoreTake(s_free, portMAX_DELAY);
    rotateChunk(fb, py, (uint16_t*)s_bounce[which]);
    // 0x2C starts a memory write, 0x3C continues it
    esp_lcd_panel_io_tx_color(s_io, py == 0 ? 0x2C : 0x3C, s_bounce[which], CHUNK_BYTES);
    which ^= 1;
  }
  // Wait for both buffers to come back so the next window command is ordered after them.
  xSemaphoreTake(s_free, portMAX_DELAY);
  xSemaphoreTake(s_free, portMAX_DELAY);
  xSemaphoreGive(s_free);
  xSemaphoreGive(s_free);
}

void backlight(bool on) { xl9535::write(K10_EXP_LCD_BL, on); }

}  // namespace display
