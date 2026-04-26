#include <stdbool.h>
#include <string.h>

#include "bootloader_common.h"
#include "esp_err.h"
#include "esp_flash_partitions.h"
#include "papers3_bootcfg.h"

extern esp_err_t bootloader_flash_read(size_t src_addr, void* dest, size_t size,
                                       bool allow_decrypt);
extern esp_err_t bootloader_flash_write(size_t dest_addr, void* src, size_t size,
                                        bool write_encrypted);
extern esp_err_t bootloader_flash_erase_range(uint32_t start_addr, uint32_t size);

void bootloader_hooks_include(void) {
}

static bool flash_read(uint32_t offset, void* out, uint32_t size) {
  return bootloader_flash_read(offset, out, size, true) == ESP_OK;
}

static bool flash_erase(uint32_t offset, uint32_t size) {
  return bootloader_flash_erase_range(offset, size) == ESP_OK;
}

static bool flash_write(uint32_t offset, const void* data, uint32_t size) {
  return bootloader_flash_write(offset, (void*)data, size, false) == ESP_OK;
}

static void erase_otadata(void) {
  (void)flash_erase(PAPERS3_OTADATA_OFFSET, PAPERS3_OTADATA_SIZE);
}

static void consume_bootcfg(uint32_t generation) {
  papers3_bootcfg_record_t empty =
      papers3_bootcfg_make_empty(generation + 1u);
  bool erased = flash_erase(PAPERS3_BOOTCFG_PARTITION_OFFSET,
                            PAPERS3_BOOTCFG_PARTITION_SIZE);
  if (erased) {
    (void)flash_write(PAPERS3_BOOTCFG_PARTITION_OFFSET, &empty, sizeof(empty));
  }
}

static void write_runtime_otadata_once(void) {
  esp_ota_select_entry_t entry;
  memset(&entry, 0xFF, sizeof(entry));
  entry.ota_seq = 1u;
  entry.ota_state = ESP_OTA_IMG_UNDEFINED;
  entry.crc = bootloader_common_ota_select_crc(&entry);

  bool erased = flash_erase(PAPERS3_OTADATA_OFFSET, PAPERS3_OTADATA_SIZE);
  if (erased) {
    (void)flash_write(PAPERS3_OTADATA_OFFSET, &entry, sizeof(entry));
    (void)flash_write(PAPERS3_OTADATA_OFFSET + 0x1000u, &entry, sizeof(entry));
  }
}

void bootloader_after_init(void) {
  papers3_bootcfg_record_t record;
  bool bootcfg_read =
      flash_read(PAPERS3_BOOTCFG_PARTITION_OFFSET, &record, sizeof(record));
  bool runtime_once = bootcfg_read && papers3_bootcfg_is_runtime_once(&record);
  if (runtime_once) {
    consume_bootcfg(record.generation);
    write_runtime_otadata_once();
    return;
  }

  erase_otadata();
}
