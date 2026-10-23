#include "sdcard.h"
#include <stdio.h>
#include <sys/stat.h>
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "../board/k10_pins.h"

static const char* TAG = "sdcard";
static sdmmc_card_t* s_card = nullptr;
static int s_nextIndex = 1;

namespace sdcard {

bool begin() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = K10_SD_MOSI;
  bus.miso_io_num = K10_SD_MISO;
  bus.sclk_io_num = K10_SD_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 16 * 1024;
  if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed");
    return false;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = K10_SD_KHZ;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = (gpio_num_t)K10_SD_CS;
  slot.host_id = SPI3_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {};
  mc.format_if_mount_failed = false;  // never wipe someone's card
  mc.max_files = 5;
  mc.allocation_unit_size = 16 * 1024;
  esp_err_t err = esp_vfs_fat_sdspi_mount(ROOT, &host, &slot, &mc, &s_card);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
    s_card = nullptr;
    return false;
  }
  sdmmc_card_print_info(stdout, s_card);
  ensureDir("/sd/DCIM");
  return true;
}

bool mounted() { return s_card != nullptr; }

void usage(uint32_t* totalMB, uint32_t* freeMB) {
  *totalMB = *freeMB = 0;
  if (!s_card) return;
  uint64_t total = 0, freeB = 0;
  if (esp_vfs_fat_info(ROOT, &total, &freeB) == ESP_OK) {
    *totalMB = (uint32_t)(total >> 20);
    *freeMB = (uint32_t)(freeB >> 20);
  }
}

bool ensureDir(const char* path) {
  struct stat st;
  if (stat(path, &st) == 0) return true;
  return mkdir(path, 0775) == 0;
}

bool nextName(const char* prefix, const char* ext, char* out, int outLen) {
  if (!s_card) return false;
  // Photos and clips share one numbering, so AFT_0004.JPG and AFT_0004.AVI
  // can never both exist (the album keeps its copies by number).
  static const char* EXTS[] = {"JPG", "AVI"};
  struct stat st;
  for (; s_nextIndex < 10000; s_nextIndex++) {
    bool taken = false;
    for (const char* e : EXTS) {
      snprintf(out, outLen, "/sd/DCIM/%s%04d.%s", prefix, s_nextIndex, e);
      if (stat(out, &st) == 0) { taken = true; break; }
    }
    if (taken) continue;
    snprintf(out, outLen, "/sd/DCIM/%s%04d.%s", prefix, s_nextIndex, ext);
    s_nextIndex++;
    return true;
  }
  return false;
}

}  // namespace sdcard
