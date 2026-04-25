#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace {
constexpr const char* kLauncherTitle = "PaperS3 Launcher";
constexpr const char* kLauncherSubtitle = "SD package installer";
constexpr const char* kPackagesDir = "/sd/packages";
constexpr uint32_t kStatusPollMs = 2000;
constexpr uint32_t kInstallChunkBytes = 4096;
constexpr int kMaxPackages = 6;
constexpr int kSdSck = 39;
constexpr int kSdMiso = 40;
constexpr int kSdMosi = 38;
constexpr int kSdCs = 47;
constexpr uint32_t kSdFrequency = 25000000;

struct HitRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct PackageInfo {
  bool valid = false;
  bool installable = false;
  char id[48] = "";
  char name[64] = "";
  char compatibility[24] = "";
  char appFile[48] = "";
  char appPath[192] = "";
  uint32_t appSize = 0;
};

struct LauncherState {
  bool runtimePresent = false;
  bool bootTargetRuntime = false;
  bool runningLauncher = false;
  bool sdMounted = false;
  int packageCount = 0;
  int selectedIndex = -1;
  uint32_t runtimeAddress = 0;
  char status[96] = "Tap REFRESH to scan SD";
};

struct InstallState {
  bool active = false;
  uint32_t progress = 0;
  char packageName[64] = "";
  char phase[96] = "";
};

LauncherState gState;
InstallState gInstall;
PackageInfo gPackages[kMaxPackages];
HitRect gRefreshButton;
HitRect gBootRuntimeButton;
HitRect gInstallButton;
HitRect gPackageRects[kMaxPackages];
SPIClass& gSdSpi = SPI;

void assignRect(HitRect& rect, int x, int y, int w, int h) {
  rect.x = x;
  rect.y = y;
  rect.w = w;
  rect.h = h;
}

bool hitTest(const HitRect& rect, int x, int y) {
  return x >= rect.x && x < rect.x + rect.w &&
         y >= rect.y && y < rect.y + rect.h;
}

bool sameState(const LauncherState& a, const LauncherState& b) {
  return a.runtimePresent == b.runtimePresent &&
         a.bootTargetRuntime == b.bootTargetRuntime &&
         a.runningLauncher == b.runningLauncher &&
         a.sdMounted == b.sdMounted &&
         a.packageCount == b.packageCount &&
         a.selectedIndex == b.selectedIndex &&
         a.runtimeAddress == b.runtimeAddress &&
         strcmp(a.status, b.status) == 0;
}

void setStatus(const char* text) {
  strncpy(gState.status, text, sizeof(gState.status) - 1);
  gState.status[sizeof(gState.status) - 1] = 0;
}

const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

const esp_partition_t* findLauncherPartition() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "launcher");
  if (part != nullptr) {
    return part;
  }
  return esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
}

const esp_partition_t* findRuntimePartition() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "runtime");
  if (part != nullptr) {
    return part;
  }
  return esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
}

bool partitionHasAppImage(const esp_partition_t* partition) {
  if (partition == nullptr) {
    return false;
  }
  uint8_t magic = 0;
  if (esp_partition_read(partition, 0, &magic, sizeof(magic)) != ESP_OK) {
    return false;
  }
  return magic == ESP_IMAGE_HEADER_MAGIC;
}

bool refreshRuntimeState() {
  LauncherState next = gState;
  const esp_partition_t* launcher = findLauncherPartition();
  const esp_partition_t* runtime = findRuntimePartition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  const esp_partition_t* running = esp_ota_get_running_partition();

  next.runtimePresent = partitionHasAppImage(runtime);
  next.runtimeAddress = runtime ? runtime->address : 0;
  next.bootTargetRuntime = runtime && boot && runtime->address == boot->address;
  next.runningLauncher =
      launcher && running && launcher->address == running->address;

  bool changed = !sameState(gState, next);
  gState.runtimePresent = next.runtimePresent;
  gState.runtimeAddress = next.runtimeAddress;
  gState.bootTargetRuntime = next.bootTargetRuntime;
  gState.runningLauncher = next.runningLauncher;
  return changed;
}

void clearPackages() {
  for (int i = 0; i < kMaxPackages; ++i) {
    gPackages[i] = PackageInfo{};
    gPackageRects[i] = HitRect{};
  }
  gState.packageCount = 0;
  gState.selectedIndex = -1;
}

bool mountSdCard() {
  SD.end();
  gSdSpi.end();
  gSdSpi.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  if (!SD.begin(kSdCs, gSdSpi, kSdFrequency, "/sd", 8, false)) {
    gState.sdMounted = false;
    return false;
  }
  gState.sdMounted = SD.cardType() != CARD_NONE;
  return gState.sdMounted;
}

bool loadPackageManifest(const char* dirPath, PackageInfo& out) {
  char manifestPath[224];
  snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.json", dirPath);
  File manifest = SD.open(manifestPath, FILE_READ);
  if (!manifest) {
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifest);
  manifest.close();
  if (err) {
    return false;
  }

  const char* id = doc["id"] | baseName(dirPath);
  const char* name = doc["name"] | id;
  const char* compatibility = doc["compatibility"] | "unknown";
  const char* appFile = doc["runtime"]["app_file"] | "";

  strncpy(out.id, id, sizeof(out.id) - 1);
  strncpy(out.name, name, sizeof(out.name) - 1);
  strncpy(out.compatibility, compatibility, sizeof(out.compatibility) - 1);
  strncpy(out.appFile, appFile, sizeof(out.appFile) - 1);
  out.id[sizeof(out.id) - 1] = 0;
  out.name[sizeof(out.name) - 1] = 0;
  out.compatibility[sizeof(out.compatibility) - 1] = 0;
  out.appFile[sizeof(out.appFile) - 1] = 0;

  if (out.appFile[0]) {
    snprintf(out.appPath, sizeof(out.appPath), "%s/%s", dirPath, out.appFile);
    File app = SD.open(out.appPath, FILE_READ);
    if (app) {
      out.appSize = app.size();
      app.close();
    }
  }

  out.valid = true;
  out.installable =
      strcmp(out.compatibility, "switchable") == 0 && out.appPath[0] != 0;
  return true;
}

bool loadPackages() {
  clearPackages();
  if (!mountSdCard()) {
    setStatus("SD mount failed. Insert a card and tap REFRESH");
    return false;
  }

  File root = SD.open(kPackagesDir, FILE_READ);
  if (!root || !root.isDirectory()) {
    setStatus("No /sd/packages directory found");
    return false;
  }

  int count = 0;
  while (count < kMaxPackages) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    PackageInfo pkg;
    if (loadPackageManifest(entry.path(), pkg)) {
      gPackages[count] = pkg;
      ++count;
    }
    entry.close();
  }

  gState.packageCount = count;
  gState.selectedIndex = count > 0 ? 0 : -1;

  if (count == 0) {
    setStatus("No package manifests found on SD");
  } else {
    setStatus("Select a package, then tap INSTALL SELECTED");
  }
  return count > 0;
}

void drawButton(const HitRect& rect, const char* title, const char* subtitle,
                bool enabled) {
  M5.Display.drawRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
  if (!enabled) {
    M5.Display.drawRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, TFT_BLACK);
  }
  M5.Display.setTextColor(enabled ? TFT_BLACK : TFT_DARKGRAY, TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.drawString(title, rect.x + 18, rect.y + 18);
  M5.Display.setTextSize(1);
  M5.Display.drawString(subtitle, rect.x + 18, rect.y + 58);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void drawInstallScreen() {
  int width = M5.Display.width();
  int height = M5.Display.height();
  int cardX = 32;
  int cardY = 120;
  int cardW = width - 64;
  int cardH = height - 220;
  int barX = cardX + 24;
  int barY = cardY + 150;
  int barW = cardW - 48;
  int barH = 24;
  int fillW = (barW * static_cast<int>(gInstall.progress)) / 100;

  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(2);
  M5.Display.drawString("Installing Package", 32, 40);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Writing selected runtime to flash", 32, 78);

  M5.Display.drawRect(cardX, cardY, cardW, cardH, TFT_BLACK);
  M5.Display.drawRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4, TFT_BLACK);

  M5.Display.setTextSize(2);
  M5.Display.drawString(gInstall.packageName, cardX + 24, cardY + 24);
  M5.Display.setTextSize(1);
  M5.Display.drawString(gInstall.phase, cardX + 24, cardY + 88);

  M5.Display.drawRect(barX, barY, barW, barH, TFT_BLACK);
  if (fillW > 0) {
    M5.Display.fillRect(barX + 2, barY + 2, fillW - 4 > 0 ? fillW - 4 : 0,
                        barH - 4, TFT_BLACK);
  }

  char progressText[32];
  snprintf(progressText, sizeof(progressText), "%lu%%",
           static_cast<unsigned long>(gInstall.progress));
  M5.Display.setTextSize(2);
  M5.Display.drawString(progressText, cardX + 24, cardY + 200);
  M5.Display.setTextSize(1);
  M5.Display.drawString("This screen updates in coarse steps to reduce e-paper flashing.",
                        cardX + 24, cardY + 246);
}

void showInstallProgress(const char* packageName, const char* phase,
                         uint32_t progress) {
  gInstall.active = true;
  gInstall.progress = progress;
  strncpy(gInstall.packageName, packageName, sizeof(gInstall.packageName) - 1);
  strncpy(gInstall.phase, phase, sizeof(gInstall.phase) - 1);
  gInstall.packageName[sizeof(gInstall.packageName) - 1] = 0;
  gInstall.phase[sizeof(gInstall.phase) - 1] = 0;
  M5.Display.setEpdMode(m5gfx::epd_fast);
  drawInstallScreen();
  M5.Display.display();
}

void drawLauncherScreen() {
  int width = M5.Display.width();
  int height = M5.Display.height();
  M5.Display.setEpdMode(m5gfx::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(top_left);

  M5.Display.setTextSize(2);
  M5.Display.drawString(kLauncherTitle, 24, 24);
  M5.Display.setTextSize(1);
  M5.Display.drawString(kLauncherSubtitle, 24, 62);

  assignRect(gRefreshButton, width - 184, 24, 160, 40);
  M5.Display.drawRect(
      gRefreshButton.x, gRefreshButton.y, gRefreshButton.w, gRefreshButton.h,
      TFT_BLACK);
  M5.Display.drawString("REFRESH", gRefreshButton.x + 28, gRefreshButton.y + 12);

  M5.Display.drawFastHLine(24, 92, width - 48, TFT_BLACK);
  M5.Display.drawString(
      gState.runningLauncher ? "Running: launcher" : "Running: runtime", 24, 118);
  M5.Display.drawString(
      gState.bootTargetRuntime ? "Next boot target: runtime"
                               : "Next boot target: launcher",
      24, 144);
  M5.Display.drawString(
      gState.runtimePresent ? "Runtime slot: app present"
                            : "Runtime slot: empty",
      24, 170);

  char runtimeAddress[48];
  snprintf(runtimeAddress, sizeof(runtimeAddress), "Runtime addr: 0x%lx",
           static_cast<unsigned long>(gState.runtimeAddress));
  M5.Display.drawString(runtimeAddress, 24, 196);
  M5.Display.drawString(
      gState.sdMounted ? "SD: mounted" : "SD: not mounted", 24, 222);

  M5.Display.drawFastHLine(24, 252, width - 48, TFT_BLACK);
  M5.Display.drawString("/sd/packages", 24, 268);

  int rowY = 300;
  for (int i = 0; i < gState.packageCount && i < kMaxPackages; ++i) {
    assignRect(gPackageRects[i], 24, rowY, width - 48, 74);
    const HitRect& rect = gPackageRects[i];
    M5.Display.drawRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
    if (gState.selectedIndex == i) {
      M5.Display.drawRect(rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4, TFT_BLACK);
    }

    char label[96];
    snprintf(label, sizeof(label), "%s [%s]",
             gPackages[i].name[0] ? gPackages[i].name : gPackages[i].id,
             gPackages[i].compatibility);
    M5.Display.setTextSize(1);
    M5.Display.drawString(label, rect.x + 16, rect.y + 16);

    char detail[96];
    snprintf(detail, sizeof(detail), "%s  %lu bytes",
             gPackages[i].installable ? "installable" : "not installable",
             static_cast<unsigned long>(gPackages[i].appSize));
    M5.Display.drawString(detail, rect.x + 16, rect.y + 42);
    rowY += 86;
  }

  if (gState.packageCount == 0) {
    M5.Display.setTextSize(1);
    M5.Display.drawString("No packages loaded. Insert SD and tap REFRESH.", 24, 320);
  }

  assignRect(gBootRuntimeButton, 24, height - 220, (width - 72) / 2, 96);
  assignRect(gInstallButton, gBootRuntimeButton.x + gBootRuntimeButton.w + 24,
             height - 220, (width - 72) / 2, 96);

  drawButton(gBootRuntimeButton, "BOOT CURRENT",
             "Start the current runtime slot", gState.runtimePresent);

  bool canInstall =
      gState.selectedIndex >= 0 && gPackages[gState.selectedIndex].installable;
  drawButton(gInstallButton, "INSTALL SELECTED",
             canInstall ? "Write SD package to runtime slot"
                        : "Select a switchable package first",
             canInstall);

  M5.Display.drawFastHLine(24, height - 106, width - 48, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Status:", 24, height - 92);
  M5.Display.drawString(gState.status, 24, height - 64);
}

void redrawWithStatus(const char* text) {
  setStatus(text);
  drawLauncherScreen();
  M5.Display.display();
}

void rebootLauncher() {
  const esp_partition_t* otadata = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (otadata == nullptr) {
    redrawWithStatus("otadata partition not found");
    return;
  }

  esp_err_t rc = esp_partition_erase_range(otadata, 0, otadata->size);
  if (rc != ESP_OK) {
    char msg[96];
    snprintf(msg, sizeof(msg), "otadata erase failed: 0x%x",
             static_cast<unsigned>(rc));
    redrawWithStatus(msg);
    return;
  }

  redrawWithStatus("Booting launcher");
  delay(200);
  esp_restart();
}

void bootRuntime() {
  const esp_partition_t* runtime = findRuntimePartition();
  if (runtime == nullptr) {
    redrawWithStatus("Runtime partition not found");
    return;
  }
  if (!partitionHasAppImage(runtime)) {
    redrawWithStatus("Runtime slot is empty");
    return;
  }

  esp_err_t rc = esp_ota_set_boot_partition(runtime);
  if (rc != ESP_OK) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Boot switch failed: 0x%x",
             static_cast<unsigned>(rc));
    redrawWithStatus(msg);
    return;
  }

  redrawWithStatus("Booting runtime");
  delay(200);
  esp_restart();
}

bool installSelectedPackage() {
  if (gState.selectedIndex < 0 || gState.selectedIndex >= gState.packageCount) {
    redrawWithStatus("No package selected");
    return false;
  }

  PackageInfo& pkg = gPackages[gState.selectedIndex];
  if (!pkg.installable) {
    redrawWithStatus("Selected package is not installable");
    return false;
  }

  const esp_partition_t* runtime = findRuntimePartition();
  if (runtime == nullptr) {
    redrawWithStatus("Runtime partition not found");
    return false;
  }
  if (pkg.appSize == 0 || pkg.appSize > runtime->size) {
    redrawWithStatus("Package is too large for the runtime slot");
    return false;
  }

  File app = SD.open(pkg.appPath, FILE_READ);
  if (!app) {
    redrawWithStatus("Failed to open app.bin from SD");
    return false;
  }

  showInstallProgress(pkg.name[0] ? pkg.name : pkg.id, "Erasing runtime partition", 0);
  esp_err_t rc = esp_partition_erase_range(runtime, 0, runtime->size);
  if (rc != ESP_OK) {
    app.close();
    gInstall.active = false;
    char msg[96];
    snprintf(msg, sizeof(msg), "Runtime erase failed: 0x%x",
             static_cast<unsigned>(rc));
    redrawWithStatus(msg);
    return false;
  }

  uint8_t buffer[kInstallChunkBytes];
  uint32_t written = 0;
  uint32_t lastProgress = 0;
  while (true) {
    size_t count = app.read(buffer, sizeof(buffer));
    if (count == 0) {
      break;
    }
    rc = esp_partition_write(runtime, written, buffer, count);
    if (rc != ESP_OK) {
      app.close();
      gInstall.active = false;
      char msg[96];
      snprintf(msg, sizeof(msg), "Runtime write failed: 0x%x",
               static_cast<unsigned>(rc));
      redrawWithStatus(msg);
      return false;
    }

    written += count;
    uint32_t progress = (written * 100UL) / pkg.appSize;
    if (progress >= lastProgress + 20 || progress == 100) {
      showInstallProgress(pkg.name[0] ? pkg.name : pkg.id,
                          "Writing runtime image",
                          progress);
      lastProgress = progress;
    }
  }
  app.close();

  showInstallProgress(pkg.name[0] ? pkg.name : pkg.id,
                      "Verifying installed image",
                      100);
  if (!partitionHasAppImage(runtime)) {
    gInstall.active = false;
    redrawWithStatus("Installed image did not validate");
    return false;
  }

  showInstallProgress(pkg.name[0] ? pkg.name : pkg.id,
                      "Switching boot target",
                      100);
  rc = esp_ota_set_boot_partition(runtime);
  if (rc != ESP_OK) {
    gInstall.active = false;
    char msg[96];
    snprintf(msg, sizeof(msg), "Boot switch failed: 0x%x",
             static_cast<unsigned>(rc));
    redrawWithStatus(msg);
    return false;
  }

  showInstallProgress(pkg.name[0] ? pkg.name : pkg.id,
                      "Rebooting runtime",
                      100);
  delay(200);
  esp_restart();
  return true;
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = false;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.fallback_board = m5::board_t::board_M5PaperS3;
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setEpdMode(m5gfx::epd_quality);
  refreshRuntimeState();
  loadPackages();
  drawLauncherScreen();
  M5.Display.display();
  Serial.println("[launcher] booted");
}

void loop() {
  M5.update();
  static uint32_t lastPoll = 0;
  static int lastX = 0;
  static int lastY = 0;
  static bool hadTouch = false;
  uint32_t now = millis();

  auto td = M5.Touch.getDetail();
  if (td.isPressed()) {
    lastX = td.x;
    lastY = td.y;
    hadTouch = true;
  } else if (hadTouch && td.wasReleased()) {
    hadTouch = false;

    if (hitTest(gRefreshButton, lastX, lastY)) {
      refreshRuntimeState();
      loadPackages();
      drawLauncherScreen();
      M5.Display.display();
      lastPoll = now;
    } else if (hitTest(gBootRuntimeButton, lastX, lastY)) {
      bootRuntime();
    } else if (hitTest(gInstallButton, lastX, lastY)) {
      (void)installSelectedPackage();
    } else {
      for (int i = 0; i < gState.packageCount && i < kMaxPackages; ++i) {
        if (hitTest(gPackageRects[i], lastX, lastY)) {
          gState.selectedIndex = i;
          if (gPackages[i].installable) {
            setStatus("Selected package. Tap INSTALL SELECTED");
          } else {
            setStatus("Selected package is not switchable");
          }
          drawLauncherScreen();
          M5.Display.display();
          lastPoll = now;
          break;
        }
      }
    }
  }

  if (now - lastPoll >= kStatusPollMs) {
    if (refreshRuntimeState()) {
      drawLauncherScreen();
      M5.Display.display();
    }
    lastPoll = now;
  }

  delay(20);
}
