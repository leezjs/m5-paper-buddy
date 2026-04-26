#include "bootcfg.h"

#include <esp_partition.h>
#include <papers3_bootcfg.h>

namespace {
const esp_partition_t* findBootcfgPartition() {
  const esp_partition_t* bootcfg = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
      PAPERS3_BOOTCFG_PARTITION_LABEL);
  return bootcfg;
}

bool readBootcfgRecord(papers3_bootcfg_record_t& record,
                       uint32_t& nextGeneration) {
  nextGeneration = 1;
  const esp_partition_t* bootcfg = findBootcfgPartition();
  if (bootcfg == nullptr) {
    return false;
  }
  if (esp_partition_read(bootcfg, 0, &record, sizeof(record)) != ESP_OK) {
    return false;
  }
  if (!papers3_bootcfg_is_valid(&record)) {
    return false;
  }
  nextGeneration = record.generation + 1;
  return true;
}

esp_err_t writeBootcfgRecord(const papers3_bootcfg_record_t& record) {
  const esp_partition_t* bootcfg = findBootcfgPartition();
  if (bootcfg == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }
  esp_err_t rc = esp_partition_erase_range(bootcfg, 0, bootcfg->size);
  if (rc != ESP_OK) {
    return rc;
  }
  return esp_partition_write(bootcfg, 0, &record, sizeof(record));
}
}  // namespace

bool runtimeOnceBootRequested() {
  papers3_bootcfg_record_t current = {};
  uint32_t nextGeneration = 1;
  return readBootcfgRecord(current, nextGeneration) &&
         papers3_bootcfg_is_runtime_once(&current);
}

esp_err_t clearRuntimeBootRequest() {
  papers3_bootcfg_record_t current = {};
  uint32_t nextGeneration = 1;
  (void)readBootcfgRecord(current, nextGeneration);
  return writeBootcfgRecord(papers3_bootcfg_make_empty(nextGeneration));
}

esp_err_t writeRuntimeOnceBootRequest() {
  papers3_bootcfg_record_t current = {};
  uint32_t nextGeneration = 1;
  (void)readBootcfgRecord(current, nextGeneration);
  return writeBootcfgRecord(
      papers3_bootcfg_make_runtime_once(nextGeneration));
}
