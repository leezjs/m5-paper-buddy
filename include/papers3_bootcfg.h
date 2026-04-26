#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PAPERS3_BOOTCFG_PARTITION_LABEL "bootcfg"
#define PAPERS3_BOOTCFG_PARTITION_OFFSET 0x12000u
#define PAPERS3_BOOTCFG_PARTITION_SIZE 0x1000u
#define PAPERS3_OTADATA_OFFSET 0xE000u
#define PAPERS3_OTADATA_SIZE 0x2000u
#define PAPERS3_BOOTCFG_MAGIC 0x33534250u
#define PAPERS3_BOOTCFG_VERSION 1u
#define PAPERS3_BOOTCFG_COMMAND_NONE 0u
#define PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE 1u

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t command;
  uint32_t generation;
  uint32_t crc32;
} papers3_bootcfg_record_t;

static inline uint32_t papers3_bootcfg_crc32_bytes(const uint8_t* data,
                                                   uint32_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static inline uint32_t papers3_bootcfg_crc32(
    const papers3_bootcfg_record_t* record) {
  return papers3_bootcfg_crc32_bytes((const uint8_t*)record, 16u);
}

static inline bool papers3_bootcfg_is_valid(
    const papers3_bootcfg_record_t* record) {
  return record->magic == PAPERS3_BOOTCFG_MAGIC &&
         record->version == PAPERS3_BOOTCFG_VERSION &&
         record->crc32 == papers3_bootcfg_crc32(record);
}

static inline bool papers3_bootcfg_is_runtime_once(
    const papers3_bootcfg_record_t* record) {
  return papers3_bootcfg_is_valid(record) &&
         record->command == PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE;
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make(
    uint32_t command, uint32_t generation) {
  papers3_bootcfg_record_t record = {
      PAPERS3_BOOTCFG_MAGIC,
      PAPERS3_BOOTCFG_VERSION,
      command,
      generation,
      0u,
  };
  record.crc32 = papers3_bootcfg_crc32(&record);
  return record;
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make_runtime_once(
    uint32_t generation) {
  return papers3_bootcfg_make(PAPERS3_BOOTCFG_COMMAND_RUNTIME_ONCE, generation);
}

static inline papers3_bootcfg_record_t papers3_bootcfg_make_empty(
    uint32_t generation) {
  return papers3_bootcfg_make(PAPERS3_BOOTCFG_COMMAND_NONE, generation);
}
