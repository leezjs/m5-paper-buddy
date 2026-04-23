#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_err.h>

#ifdef BUDDY_TARGET_PAPERS3

#include <M5Unified.h>
#include <M5GFX.h>

using rtc_time_t = m5::rtc_time_t;
using rtc_date_t = m5::rtc_date_t;
using rtc_datetime_t = m5::rtc_datetime_t;

#define canvas M5.Display
#define BUDDY_RTC M5.Rtc
#define BTN_UP M5.BtnA
#define BTN_APPROVE M5.BtnB
#define BTN_DENY M5.BtnC
#define BTN_UP_LABEL "A"
#define BTN_APPROVE_LABEL "B"
#define BTN_DENY_LABEL "C"

#ifdef BUDDY_LANDSCAPE
static constexpr bool BUDDY_DEFAULT_LANDSCAPE = true;
#else
static constexpr bool BUDDY_DEFAULT_LANDSCAPE = false;
#endif

static inline void buddyBegin(void) {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = false;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.fallback_board = m5::board_t::board_M5PaperS3;
  M5.begin(cfg);

  M5.Display.setEpdMode(m5gfx::epd_fastest);
}

static inline void buddyUpdate(void) {
  M5.update();
}

static inline void buddySetTouchButtonHeight(uint16_t px) {
  M5.setTouchButtonHeight(px);
}

static inline uint32_t buddyBatteryVoltage(void) {
  return (uint32_t)M5.Power.getBatteryVoltage();
}

static inline int buddyBatteryPercent(void) {
  return M5.Power.getBatteryLevel();
}

static inline bool buddyUsbConnected(void) {
  return M5.Power.isCharging() != m5::Power_Class::is_discharging;
}

static inline void buddyCreateCanvas(int, int) {
}

static inline void buddyClearDisplay(bool) {
  M5.Display.clear();
}

static inline void buddyFrameBegin(void) {
  M5.Display.startWrite();
}

static inline void buddyFrameEnd(void) {
  M5.Display.endWrite();
}

static inline bool buddyDisplayBusy(void) {
  return M5.Display.displayBusy();
}

static inline void buddyTouchSetRotation(int) {
}

static inline void buddySetLandscape(bool landscape) {
  bool isLandscape = M5.Display.width() > M5.Display.height();
  if (landscape != isLandscape) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
    M5.Display.clear();
  }
}

static inline bool buddyPushFullDisplay(void) {
  if (M5.Display.displayBusy()) return false;
  M5.Display.setEpdMode(m5gfx::epd_quality);
  M5.Display.display();
  return true;
}

static inline bool buddyPushPartialDisplay(void) {
  if (M5.Display.displayBusy()) return false;
  M5.Display.setEpdMode(m5gfx::epd_fast);
  M5.Display.display();
  return true;
}

#else

#include <M5EPD.h>

extern M5EPD_Canvas canvas;

#define BUDDY_RTC M5.RTC
#define BTN_UP M5.BtnL
#define BTN_APPROVE M5.BtnP
#define BTN_DENY M5.BtnR
#define BTN_UP_LABEL "UP"
#define BTN_APPROVE_LABEL "PUSH"
#define BTN_DENY_LABEL "DOWN"
static constexpr bool BUDDY_DEFAULT_LANDSCAPE = false;

static inline void buddyBegin(void) {
  // The firmware never mounts or reads the SD card, so keep that peripheral
  // off and reduce board-specific wiring requirements.
  M5.begin(true, false, true, true, true);
}

static inline void buddyUpdate(void) {
  M5.update();
}

static inline void buddySetTouchButtonHeight(uint16_t) {
}

static inline uint32_t buddyBatteryVoltage(void) {
  return M5.getBatteryVoltage();
}

static inline int buddyBatteryPercent(void) {
  int vBat = (int)M5.getBatteryVoltage();
  int pct = (vBat - 3200) * 100 / (4350 - 3200);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static inline bool buddyUsbConnected(void) {
  return M5.getBatteryVoltage() > 4250;
}

static inline void buddyCreateCanvas(int w, int h) {
  canvas.createCanvas(w, h);
}

static inline void buddyClearDisplay(bool init) {
  M5.EPD.Clear(init);
}

static inline void buddyFrameBegin(void) {
}

static inline void buddyFrameEnd(void) {
}

static inline bool buddyDisplayBusy(void) {
  return false;
}

static inline void buddyTouchSetRotation(int rot) {
  M5.TP.SetRotation(rot);
}

static inline void buddySetLandscape(bool) {
}

static inline bool buddyPushFullDisplay(void) {
  canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
  return true;
}

static inline bool buddyPushPartialDisplay(void) {
  canvas.pushCanvas(0, 0, UPDATE_MODE_GL16);
  return true;
}

#endif
