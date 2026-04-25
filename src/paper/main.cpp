// Paper buddy firmware.
//
// 4.7" 540x960 e-ink portrait. Always-on dashboard for Claude Code:
//   - top band: project/sessions (L) + model/budget (R)
//   - middle: latest assistant message ("Claude says")
//   - lower: recent activity log
//   - footer: state-driven vector buddy face + link/button hints
// Full-screen approval card takes over when a permission decision is needed.
//
// Controls:
//   - tap sessions to switch focus
//   - tap top-right to open settings
//   - use the settings bottom action bar to switch language / layout / close
//   - use the prompt action areas to approve / deny / answer questions

#include <stdarg.h>
#include <rom/rtc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "paper_compat.h"
#include "../ble_bridge.h"
#include "data_paper.h"
#include "buddy_frames.h"

#ifndef BUDDY_DEVICE_LABEL
#define BUDDY_DEVICE_LABEL "M5Paper V1.1"
#endif

#ifndef BUDDY_PIO_ENV
#define BUDDY_PIO_ENV "unknown"
#endif

#ifndef BUDDY_TARGET_PAPERS3
M5EPD_Canvas canvas(&M5.EPD);
#endif

static inline int screenW() { return canvas.width(); }
static inline int screenH() { return canvas.height(); }
#define W (screenW())
#define H (screenH())

#ifdef BUDDY_TARGET_PAPERS3
// PaperS3 currently uses M5GFX's built-in CJK font set, where setTextSize()
// is a scale factor, not a pixel height.
static const int TS_SM   = 1;
static const int TS_MD   = 2;
static const int TS_LG   = 2;
static const int TS_XL   = 3;
static const int TS_XXL  = 4;
static const int TS_HUGE = 5;
#else
// Text sizes — pixel heights (TTF rendering uses setTextSize as pixels,
// not a multiplier like the built-in font does). Each gets a
// createRender() in setup() so the glyph cache is warm for CJK.
static const int TS_SM   = 18;   // small body / labels
static const int TS_MD   = 26;   // primary body text
static const int TS_LG   = 34;   // emphasis
static const int TS_XL   = 44;   // tool name, option labels
static const int TS_XXL  = 56;   // big headline
static const int TS_HUGE = 72;   // passkey digits / splash
#endif

#ifdef BUDDY_TARGET_PAPERS3
static const uint16_t INK      = TFT_BLACK;
static const uint16_t INK_DIM  = TFT_BLACK;
static const uint16_t PAPER    = TFT_WHITE;
#else
static const uint16_t INK      = 15;
static const uint16_t INK_DIM  = 13;
static const uint16_t PAPER    = 0;
#endif

#ifdef BUDDY_TARGET_PAPERS3
static const int HEADER_RULE_Y = 86;
static const int TOP_BAND_Y = 102;
static const int TOP_BAND_H = 170;
static const int TOP_BAND_RULE_Y = 286;
static const int STATS_Y = 300;
static const int STATS_RULE_Y = 360;
static const int REPLY_Y = 378;
static const int REPLY_RULE_Y = 592;
static const int ACTIVITY_Y = 610;
static const int FOOTER_TOP = 790;
#else
static const int HEADER_RULE_Y = 94;
static const int TOP_BAND_Y = 100;
static const int TOP_BAND_H = 160;
static const int TOP_BAND_RULE_Y = 264;
static const int STATS_Y = 280;
static const int STATS_RULE_Y = 326;
static const int REPLY_Y = 338;
static const int REPLY_RULE_Y = 510;
static const int ACTIVITY_Y = 522;
static const int FOOTER_TOP = H - 170;
#endif

static const int LS_HEADER_RULE_Y = 64;
static const int LS_LEFT_X = 20;
static const int LS_LEFT_W = 550;
static const int LS_RIGHT_X = 590;
static const int LS_RIGHT_W = 350;
static const int LS_REPLY_Y = 86;
static const int LS_REPLY_H = 180;
static const int LS_ACTIVITY_Y = 286;
static const int LS_ACTIVITY_H = 164;
static const int LS_SUMMARY_Y = 86;
static const int LS_SUMMARY_H = 190;
static const int LS_STATS_Y = 296;
static const int LS_STATS_H = 64;
static const int LS_FOOTER_Y = 380;
static const int LS_FOOTER_H = 140;

// Section rules — use full INK + 2px thick so they're clearly visible
// under both GC16 (where grayscales differ) and DU (where everything
// under threshold collapses to white).
static void drawRule(int y) {
  canvas.drawFastHLine(0, y,     W, INK);
  canvas.drawFastHLine(0, y + 1, W, INK);
}
static void drawRuleInset(int y, int inset) {
  canvas.drawFastHLine(inset, y,     W - 2*inset, INK);
  canvas.drawFastHLine(inset, y + 1, W - 2*inset, INK);
}

static void drawPanel(int x, int y, int w, int h) {
  for (int d = 0; d < 2; d++) {
    canvas.drawRect(x + d, y + d, w - 2*d, h - 2*d, INK);
  }
}

static void drawActionBar(int x, int y, int w, int h,
                          const char* a, const char* b, const char* c) {
  int gap = 10;
  int cellW = (w - gap * 2) / 3;
  canvas.fillRect(x, y, cellW, h, PAPER);
  canvas.fillRect(x + cellW + gap, y, cellW, h, PAPER);
  canvas.fillRect(x + (cellW + gap) * 2, y, cellW, h, PAPER);
  drawPanel(x, y, cellW, h);
  drawPanel(x + cellW + gap, y, cellW, h);
  drawPanel(x + (cellW + gap) * 2, y, cellW, h);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK);
  canvas.drawString(a, x + cellW / 2, y + h / 2);
  canvas.drawString(b, x + cellW + gap + cellW / 2, y + h / 2);
  canvas.drawString(c, x + (cellW + gap) * 2 + cellW / 2, y + h / 2);
  canvas.setTextDatum(TL_DATUM);
}

static char btName[16] = "Claude";

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
#ifndef BUDDY_DISABLE_BLE
  bleInit(btName);
#else
  Serial.println("[ble] DISABLED via BUDDY_DISABLE_BLE");
#endif
}

static TamaState tama;
static char     lastPromptId[40] = "";
static uint32_t promptArrivedMs = 0;
static bool     responseSent = false;
static uint32_t lastFullRefreshMs = 0;
static uint32_t lastPartialRefreshMs = 0;
static bool     redrawPending = true;
static bool     lastMode = false;
static bool     forceFullRefresh = false;

static bool     dndMode = false;
static const uint32_t DND_AUTO_DELAY_MS = 600;
static bool     dndAutoSent = false;

// UI language. 0 = English, 1 = 中文. Persisted in NVS via Preferences.
// Usage: canvas.drawString(LX("PROJECT", "项目"), x, y);
static uint8_t  uiLang = 0;
static bool     uiLandscape = BUDDY_DEFAULT_LANDSCAPE;
#define LX(en, zh) (uiLang == 1 ? (zh) : (en))

// Short-lived "celebrate" flash after any approve/deny so the buddy face
// briefly shows a reaction even when no data-driven state change follows.
static uint32_t celebrateUntil = 0;

static bool returnToLauncher() {
  const esp_partition_t* otadata = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (otadata == nullptr) {
    Serial.println("[boot] otadata partition not found");
    return false;
  }

  esp_err_t rc = esp_partition_erase_range(otadata, 0, otadata->size);
  if (rc != ESP_OK) {
    Serial.printf("[boot] failed to erase otadata for launcher boot: 0x%x\n",
                  (unsigned)rc);
    return false;
  }

  Serial.println("[boot] otadata erased, rebooting to factory launcher");
  delay(200);
  esp_restart();
  return true;
}

static void sendCmd(const char* json) {
  Serial.println(json);
  size_t n = strlen(json);
  bleWrite((const uint8_t*)json, n);
  bleWrite((const uint8_t*)"\n", 1);
}

static void fmtTokens(char* out, size_t n, uint32_t v) {
  if (v >= 1000000)      snprintf(out, n, "%lu.%luM", v/1000000, (v/100000)%10);
  else if (v >= 1000)    snprintf(out, n, "%lu.%luK", v/1000, (v/100)%10);
  else                    snprintf(out, n, "%lu", v);
}

// Manual codepoint-width estimator. M5EPD's canvas.textWidth() returns
// questionable values for CJK glyphs (often way smaller than they actually
// render), so we count codepoint bytes and scale by the current text
// size: ASCII ~0.55*size, anything multi-byte ~size (square glyph).
static int estWidth(const char* s, int textSize) {
  int w = 0;
#ifdef BUDDY_TARGET_PAPERS3
  int approxPx = textSize * 16;
#else
  int approxPx = textSize;
#endif
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int cpLen = 1;
    if      ((*p & 0x80) == 0)     cpLen = 1;
    else if ((*p & 0xE0) == 0xC0)  cpLen = 2;
    else if ((*p & 0xF0) == 0xE0)  cpLen = 3;
    else if ((*p & 0xF8) == 0xF0)  cpLen = 4;
    w += (cpLen == 1) ? (approxPx * 55 / 100) : approxPx;
    p += cpLen;
  }
  return w;
}

// UTF-8 + pixel-width word wrap. Walks the input a codepoint at a time,
// using the manual estimator above. Caller passes textSize matching
// whatever setTextSize will be used at draw time.
static uint8_t wrapText(const char* in, char out[][256], uint8_t maxRows, int maxWidthPx, int textSize) {
  uint8_t row = 0;
  char cur[256]; cur[0] = 0; int curLen = 0;
  const char* p = in;

  auto flush = [&](bool force = false) {
    if (curLen > 0 || force) {
      if (row < maxRows) {
        strncpy(out[row], cur, 255);
        out[row][255] = 0;
      }
      row++; curLen = 0; cur[0] = 0;
    }
  };

  while (*p && row < maxRows) {
    if (*p == '\n') { flush(true); p++; continue; }

    uint8_t lead = (uint8_t)*p;
    int cpLen = 1;
    if      ((lead & 0x80) == 0)     cpLen = 1;
    else if ((lead & 0xE0) == 0xC0)  cpLen = 2;
    else if ((lead & 0xF0) == 0xE0)  cpLen = 3;
    else if ((lead & 0xF8) == 0xF0)  cpLen = 4;
    for (int i = 0; i < cpLen; i++) if (!p[i]) { cpLen = i; break; }
    if (cpLen == 0) break;

    if (cpLen == 1 && *p == ' ' && curLen == 0) { p++; continue; }

    // Build the probe line and measure.
    if (curLen + cpLen >= 255) { flush(); if (row >= maxRows) break; }
    char probe[256];
    memcpy(probe, cur, curLen);
    memcpy(probe + curLen, p, cpLen);
    probe[curLen + cpLen] = 0;
    int w = estWidth(probe, textSize);

    if (w > maxWidthPx && curLen > 0) {
      // Try to back up to the last ASCII space so we don't break a word.
      int lastSpace = -1;
      for (int i = curLen - 1; i >= 0; i--) if (cur[i] == ' ') { lastSpace = i; break; }
      if (lastSpace > 0) {
        char tail[256];
        int tailLen = curLen - (lastSpace + 1);
        memcpy(tail, cur + lastSpace + 1, tailLen);
        cur[lastSpace] = 0;
        curLen = lastSpace;
        flush();
        if (row >= maxRows) break;
        memcpy(cur, tail, tailLen);
        curLen = tailLen;
        cur[curLen] = 0;
      } else {
        flush();
        if (row >= maxRows) break;
      }
      continue;
    }

    memcpy(cur + curLen, p, cpLen);
    curLen += cpLen;
    cur[curLen] = 0;
    p += cpLen;
  }
  flush();
  return row;
}

// drawString, but truncates with a trailing "." if the text is too wide.
// Cap is in glyphs, not pixels — good enough given the default monospace-ish
// font.
static void drawTrunc(const char* s, int x, int y, int maxChars) {
  int n = (int)strlen(s);
  if (n <= maxChars) { canvas.drawString(s, x, y); return; }
  char buf[64];
  int take = maxChars - 1;
  if (take < 1) take = 1;
  if (take > (int)sizeof(buf) - 2) take = sizeof(buf) - 2;
  memcpy(buf, s, take);
  buf[take] = '.';
  buf[take + 1] = 0;
  canvas.drawString(buf, x, y);
}

// -----------------------------------------------------------------------------
// Buddy face (state-driven, ~80x80 vector drawing). Not animated per-frame
// — just repainted when state changes or on the 5-min full refresh. That's
// enough life on an e-ink panel without thrashing the display.
// -----------------------------------------------------------------------------

enum BuddyState { B_SLEEP, B_IDLE, B_BUSY, B_ATTENTION, B_CELEBRATE, B_DND };

static BuddyState currentBuddy() {
  if (dndMode)                     return B_DND;
  if (tama.promptId[0] && !responseSent) return B_ATTENTION;
  if (millis() < celebrateUntil)   return B_CELEBRATE;
  if (!tama.connected)             return B_SLEEP;
  if (tama.sessionsWaiting > 0)    return B_ATTENTION;
  if (tama.sessionsRunning > 0)    return B_BUSY;
  return B_IDLE;
}

// Pick the state's ASCII frame (lifted from src/buddies/cat.cpp — see
// buddy_frames.h).
static const BuddyFrame& currentFrame(BuddyState state) {
  switch (state) {
    case B_SLEEP:     return buddy_cat::SLEEP;
    case B_BUSY:      return buddy_cat::BUSY;
    case B_ATTENTION: return buddy_cat::ATTENTION;
    case B_CELEBRATE: return buddy_cat::CELEBRATE;
    case B_DND:       return buddy_cat::DND;
    case B_IDLE:
    default:          return buddy_cat::IDLE;
  }
}

// Render an ASCII buddy frame centered on (cx, cy). The ASCII art
// assumes a fixed-width font, but our TTF is proportional — so we draw
// each character into its own fixed cell (cellW × cellH) so columns
// line up. cellW tuned so 12 chars × cellW ≈ 180px, legible but compact.
static void drawBuddy(int cx, int cy, BuddyState state) {
  const BuddyFrame& f = currentFrame(state);
  const int cellW = 15;
  const int lineH = 24;
  const int totalW = 12 * cellW;
  const int totalH = 5 * lineH;
  int x0 = cx - totalW / 2;
  int y0 = cy - totalH / 2;
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK);
  canvas.setTextDatum(TC_DATUM);
  for (int i = 0; i < 5; i++) {
    const char* line = f.lines[i];
    for (int c = 0; c < 12; c++) {
      char ch[2] = { line[c], 0 };
      if (ch[0] && ch[0] != ' ') {
        canvas.drawString(ch, x0 + c * cellW + cellW / 2, y0 + i * lineH);
      }
    }
  }
  canvas.setTextDatum(TL_DATUM);
}

// -----------------------------------------------------------------------------
// Dashboard sections
// -----------------------------------------------------------------------------

// Touch hit regions + settings page state — declared here (before any
// draw function that references them) so both drawHeader (sets
// settingsTrigger), drawSettings (sets settingsCloseHit), and loop()
// (hit-tests) can all see them.
struct HitRect { int x, y, w, h; };
static HitRect optionRects[4] = {};
static uint8_t optionRectCount = 0;
static int8_t  selectedOption  = -1;

static bool   settingsOpen = false;
static HitRect settingsTrigger  = {0, 0, 0, 0};
static HitRect settingsCloseHit = {0, 0, 0, 0};
static HitRect settingsLangHit  = {0, 0, 0, 0};
static HitRect settingsLayoutHit = {0, 0, 0, 0};
static HitRect settingsLauncherHit = {0, 0, 0, 0};
static HitRect settingsActionLangHit = {0, 0, 0, 0};
static HitRect settingsActionLayoutHit = {0, 0, 0, 0};
static HitRect settingsActionCloseHit = {0, 0, 0, 0};

// Legacy tab hit rects (approval tabs were dropped; kept for future use).
static HitRect tabRects[4] = {};
static uint8_t tabRectCount = 0;

// Sessions list tap targets — tapping a row sends a focus_session cmd
// back to the daemon, shifting the dashboard view to that session.
static HitRect sessionRects[5] = {};
static uint8_t sessionRectCount = 0;

static void drawHeader() {
  char who[64];
  if (ownerName()[0]) snprintf(who, sizeof(who), "%s's %s", ownerName(), petName());
  else                snprintf(who, sizeof(who), "%s", petName());

  char bat[16];
  snprintf(bat, sizeof(bat), "%d%%", buddyBatteryPercent());

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(INK);

  if (uiLandscape) {
    canvas.setTextSize(TS_LG);
    canvas.drawString(LX("Paper Buddy", "Paper Buddy"), 24, 18);
    canvas.setTextSize(TS_SM);
    canvas.drawString(who, 24, 48);

    canvas.setTextDatum(TR_DATUM);
    canvas.drawString(bat, W - 24, 20);
    canvas.drawString(LX("SETTINGS", "设置"), W - 24, 48);
    settingsTrigger = { W - 190, 12, 170, 44 };

    if (dndMode) {
      int dndW = 72, dndH = 26;
      int dx = W - 24 - 170 - 12 - dndW;
      int dy = 30;
      canvas.fillRect(dx, dy, dndW, dndH, INK);
      canvas.setTextColor(PAPER, INK);
      canvas.setTextDatum(MC_DATUM);
      canvas.drawString("DND", dx + dndW / 2, dy + dndH / 2);
      canvas.setTextColor(INK);
      canvas.setTextDatum(TR_DATUM);
    }

    canvas.setTextDatum(TL_DATUM);
    drawRule(LS_HEADER_RULE_Y);
    return;
  }

  canvas.setTextSize(TS_MD);
  canvas.drawString(LX("Paper Buddy", "Paper Buddy"), 24, 22);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(who, 24, 56);

  canvas.setTextColor(INK_DIM);
  canvas.setTextDatum(TR_DATUM);
  canvas.drawString(bat, W - 24, 26);

  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK);
  canvas.drawString(LX("SETTINGS", "设置"), W - 24, 60);
  settingsTrigger = { W - 160, 52, 150, 40 };

  if (dndMode) {
    int dndW = 60, dndH = 30;
    int dx = W - 160 - 10 - dndW, dy = 50;
    canvas.fillRect(dx, dy, dndW, dndH, INK);
    canvas.setTextColor(PAPER, INK);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString("DND", dx + dndW/2, dy + dndH/2);
  }

  canvas.setTextColor(INK);
  canvas.setTextDatum(TL_DATUM);
  drawRule(HEADER_RULE_Y);
}

static void drawTopBand() {
  if (uiLandscape) {
    drawPanel(LS_RIGHT_X, LS_SUMMARY_Y, LS_RIGHT_W, LS_SUMMARY_H);
    sessionRectCount = 0;

    int x = LS_RIGHT_X + 18;
    int y = LS_SUMMARY_Y + 16;

    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    canvas.drawString(LX("PROJECT", "项目"), x, y);
    canvas.drawString(LX("MODEL", "模型"), x + 176, y);
    y += 26;

    canvas.setTextSize(TS_LG);
    drawTrunc(tama.project[0] ? tama.project : "-", x, y, 16);
    drawTrunc(tama.modelName[0] ? tama.modelName : "-", x + 176, y, 14);
    y += 36;

    canvas.setTextSize(TS_SM);
    if (tama.branch[0]) {
      canvas.drawString(tama.branch, x, y);
    }
    y += 30;

    canvas.drawString(LX("CONTEXT", "上下文"), x, y);
    y += 22;
    char tok[16]; fmtTokens(tok, sizeof(tok), tama.tokensToday);
    if (tama.budgetLimit > 0) {
      char lim[16]; fmtTokens(lim, sizeof(lim), tama.budgetLimit);
      char line[48]; snprintf(line, sizeof(line), "%s / %s", tok, lim);
      canvas.drawString(line, x, y);
      y += 22;
      int bw = LS_RIGHT_W - 36, bh = 10;
      canvas.drawRect(x, y, bw, bh, INK);
      int pct = (int)((uint64_t)tama.tokensToday * 100 / tama.budgetLimit);
      if (pct > 100) pct = 100;
      int fill = (int)((uint64_t)bw * pct / 100);
      if (fill > 2) canvas.fillRect(x + 1, y + 1, fill - 2, bh - 2, INK);
      y += 24;
    } else {
      canvas.drawString(tok, x, y);
      y += 26;
    }

    canvas.drawString(LX("SESSIONS", "会话"), x, y);
    y += 22;
    if (tama.sessionCount > 1) {
      for (uint8_t i = 0; i < tama.sessionCount && i < 4; i++) {
        const auto& s = tama.sessions[i];
        int rowY = y - 3;
        int rowH = 24;
        int rowW = LS_RIGHT_W - 36;
        if (s.focused) {
          canvas.fillRect(x - 6, rowY, rowW, rowH, INK);
          canvas.setTextColor(PAPER, INK);
        } else {
          canvas.setTextColor(INK);
        }
        char row[40];
        snprintf(row, sizeof(row), "%s %.16s", s.waiting ? "!" : (s.running ? "*" : "."), s.project[0] ? s.project : "-");
        canvas.drawString(row, x, y);
        sessionRects[sessionRectCount++] = { x - 6, rowY, rowW, rowH };
        y += 26;
      }
    } else {
      char line[32];
      snprintf(line, sizeof(line), LX("%u run  %u wait", "运行 %u  等待 %u"),
               tama.sessionsRunning, tama.sessionsWaiting);
      canvas.setTextColor(INK);
      canvas.drawString(line, x, y);
    }
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  canvas.setTextDatum(TL_DATUM);
  // Column rule — 2px for visual parity with horizontal rules.
  canvas.drawFastVLine(W/2,     TOP_BAND_Y, TOP_BAND_H, INK);
  canvas.drawFastVLine(W/2 + 1, TOP_BAND_Y, TOP_BAND_H, INK);

  // --- LEFT: session list (2+) OR classic single-project view ---------
  int lx = 16, ly = TOP_BAND_Y + 10;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(tama.sessionCount > 1 ? LX("SESSIONS", "会话") : LX("PROJECT", "项目"), lx, ly);
  ly += 26;

  if (tama.sessionCount > 1) {
    // Multi-session: row per session. Tap a row to focus the dashboard
    // on that session. The focused row gets reverse-video; others show
    // a prefix marker: "!" = waiting on approval, "*" = running,
    // "." = idle. Project name on left, branch info on right.
    sessionRectCount = 0;
    canvas.setTextSize(TS_SM);
    int rowH = 28;
    for (uint8_t i = 0; i < tama.sessionCount && ly < (TOP_BAND_Y + TOP_BAND_H - 16); i++) {
      const auto& s = tama.sessions[i];
      int rowX = 4, rowY = ly - 4, rowW = (W/2) - 8;
      if (s.focused) {
        canvas.fillRect(rowX, rowY, rowW, rowH, INK);
        canvas.setTextColor(PAPER, INK);
      } else {
        canvas.setTextColor(INK, PAPER);
      }
      const char* mark = s.waiting ? "!" : (s.running ? "*" : ".");
      canvas.drawString(mark, lx, ly);
      char row[32];
      snprintf(row, sizeof(row), "%.18s",
               s.project[0] ? s.project : LX("(unknown)", "（未知）"));
      canvas.drawString(row, lx + 16, ly);
      if (s.branch[0]) {
        if (!s.focused) canvas.setTextColor(INK_DIM, PAPER);
        char bra[20];
        if (s.dirty > 0) snprintf(bra, sizeof(bra), "%.10s*%u", s.branch, s.dirty);
        else             snprintf(bra, sizeof(bra), "%.14s", s.branch);
        canvas.setTextDatum(TR_DATUM);
        canvas.drawString(bra, W/2 - 6, ly);
        canvas.setTextDatum(TL_DATUM);
      }
      sessionRects[sessionRectCount++] = { rowX, rowY, rowW, rowH };
      ly += rowH;
    }
  } else {
    sessionRectCount = 0;
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK);
    drawTrunc(tama.project[0] ? tama.project : "-", lx, ly, 14);
    ly += 34;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    if (tama.branch[0]) {
      char bra[48];
      if (tama.dirty > 0) snprintf(bra, sizeof(bra), "%s  *%u", tama.branch, tama.dirty);
      else                 snprintf(bra, sizeof(bra), "%s", tama.branch);
      drawTrunc(bra, lx, ly, 20);
    }
    ly = TOP_BAND_Y + 96;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(LX("SESSIONS", "会话"), lx, ly); ly += 22;
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK);
    if (!tama.connected) {
      canvas.drawString("-", lx, ly);
    } else if (tama.sessionsTotal == 0) {
      canvas.drawString(LX("idle", "空闲"), lx, ly);
    } else {
      char s[48];
      snprintf(s, sizeof(s), LX("%u run  %u wait", "运行 %u  等待 %u"),
               tama.sessionsRunning, tama.sessionsWaiting);
      canvas.drawString(s, lx, ly);
    }
  }

  // --- RIGHT: model + budget --------------------------------------------
  int rx = W/2 + 16, ry = TOP_BAND_Y + 10;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("MODEL", "模型"), rx, ry);  ry += 22;
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK);
  // No em dash fallback — an empty model field just leaves the slot
  // blank rather than rendering a single-glyph that can look like a
  // stray line next to the column divider.
  if (tama.modelName[0]) drawTrunc(tama.modelName, rx, ry, 14);
  ry = TOP_BAND_Y + 90;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("CONTEXT", "上下文"), rx, ry); ry += 22;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK);
  char tok[16]; fmtTokens(tok, sizeof(tok), tama.tokensToday);
  if (tama.budgetLimit > 0) {
    char lim[16]; fmtTokens(lim, sizeof(lim), tama.budgetLimit);
    char line[64]; snprintf(line, sizeof(line), "%s / %s", tok, lim);
    canvas.drawString(line, rx, ry);
    ry += 24;
    int bx = rx, by = ry, bw = (W/2) - 32, bh = 12;
    canvas.drawRect(bx, by, bw, bh, INK);
    int pct = (int)((uint64_t)tama.tokensToday * 100 / tama.budgetLimit);
    if (pct > 100) pct = 100;
    int fill = (int)((uint64_t)bw * pct / 100);
    if (fill > 2) canvas.fillRect(bx + 1, by + 1, fill - 2, bh - 2, INK);
  } else {
    canvas.drawString(tok, rx, ry);
  }

  drawRule(TOP_BAND_RULE_Y);
}

// Slim stats row — level (by tokens), approval/denial tallies. MOOD was
// dropped: its velocity-based tier isn't a useful signal in a coding
// workflow, and the stars read ambiguously.
static void drawStats() {
  if (uiLandscape) {
    drawPanel(LS_RIGHT_X, LS_STATS_Y, LS_RIGHT_W, LS_STATS_H);
    int y = LS_STATS_Y + 10;

    auto drawCell = [&](int x, const char* label, const char* value) {
      canvas.setTextSize(TS_SM);
      canvas.setTextColor(INK);
      canvas.drawString(label, x, y);
      canvas.setTextSize(TS_MD);
      canvas.drawString(value, x, y + 22);
    };

    char lvl[12], appr[12], deny[12];
    snprintf(lvl,  sizeof(lvl),  "%u", stats().level);
    snprintf(appr, sizeof(appr), "%u", stats().approvals);
    snprintf(deny, sizeof(deny), "%u", stats().denials);
    drawCell(LS_RIGHT_X + 14,  LX("LEVEL", "等级"), lvl);
    drawCell(LS_RIGHT_X + 126, LX("APPROVED", "批准"), appr);
    drawCell(LS_RIGHT_X + 248, LX("DENIED", "拒绝"), deny);
    return;
  }

  canvas.setTextDatum(TL_DATUM);
  int y = STATS_Y;

  auto drawCell = [&](int x, const char* label, const char* value) {
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(label, x, y);
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK);
    canvas.drawString(value, x, y + 24);
  };

  char lvl[12], appr[12], deny[12];
  snprintf(lvl,  sizeof(lvl),  "%u", stats().level);
  snprintf(appr, sizeof(appr), "%u", stats().approvals);
  snprintf(deny, sizeof(deny), "%u", stats().denials);

  drawCell( 20, LX("LEVEL",    "等级"),    lvl);
  drawCell(200, LX("APPROVED", "已批准"),  appr);
  drawCell(380, LX("DENIED",   "已拒绝"),  deny);

  drawRule(STATS_RULE_Y);
}

// "Latest reply" — most recent assistant text pulled from the session's
// transcript_path. Shows whatever Claude last said in prose (not tool
// calls), so you can glance at the Paper and know what Claude is up to.
static void drawClaudeSays() {
  if (uiLandscape) {
    drawPanel(LS_LEFT_X, LS_REPLY_Y, LS_LEFT_W, LS_REPLY_H);
    int y = LS_REPLY_Y + 14;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    canvas.drawString(LX("LATEST REPLY", "最新回复"), LS_LEFT_X + 16, y);
    y += 30;
    canvas.setTextSize(TS_MD);
    if (!tama.assistantMsg[0]) {
      canvas.drawString(LX("(nothing yet)", "（暂无）"), LS_LEFT_X + 16, y);
      return;
    }
    static char wrapped[4][256];
    uint8_t rows = wrapText(tama.assistantMsg, wrapped, 4, LS_LEFT_W - 32, TS_MD);
    for (uint8_t i = 0; i < rows; i++) {
      canvas.drawString(wrapped[i], LS_LEFT_X + 16, y);
      y += 34;
    }
    return;
  }

  int y = REPLY_Y;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("LATEST REPLY", "最新回复"), 16, y); y += 30;

  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK);
  if (!tama.assistantMsg[0]) {
    canvas.setTextColor(INK_DIM);
    canvas.drawString(LX("(nothing yet)", "（暂无）"), 16, y);
    drawRule(REPLY_RULE_Y);
    return;
  }
  static char wrapped[4][256];
  // Body width with 16px margins. Call setTextSize BEFORE wrapText so
  // the pixel measurement uses the right render.
  uint8_t rows = wrapText(tama.assistantMsg, wrapped, 4, W - 32, TS_MD);
  for (uint8_t i = 0; i < rows; i++) {
    canvas.drawString(wrapped[i], 16, y);
    y += 38;
    if (y > REPLY_RULE_Y - 26) break;
  }
  drawRule(REPLY_RULE_Y);
}

static void drawActivity() {
  if (uiLandscape) {
    drawPanel(LS_LEFT_X, LS_ACTIVITY_Y, LS_LEFT_W, LS_ACTIVITY_H);
    int y = LS_ACTIVITY_Y + 14;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    canvas.drawString(LX("ACTIVITY", "活动"), LS_LEFT_X + 16, y);
    y += 28;
    canvas.setTextSize(TS_MD);
    if (tama.nLines == 0) {
      canvas.drawString("-", LS_LEFT_X + 16, y);
      return;
    }
    uint8_t show = tama.nLines > 4 ? 4 : tama.nLines;
    for (uint8_t i = 0; i < show && y < LS_ACTIVITY_Y + LS_ACTIVITY_H - 24; i++) {
      static char wrapped[2][256];
      uint8_t rows = wrapText(tama.lines[i], wrapped, 2, LS_LEFT_W - 32, TS_MD);
      for (uint8_t r = 0; r < rows && y < LS_ACTIVITY_Y + LS_ACTIVITY_H - 24; r++) {
        canvas.drawString(wrapped[r], LS_LEFT_X + 16, y);
        y += 30;
      }
    }
    return;
  }

  canvas.setTextDatum(TL_DATUM);
  int y = ACTIVITY_Y;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("ACTIVITY", "活动"), 16, y); y += 30;

  canvas.setTextSize(TS_MD);
  if (tama.nLines == 0) {
    canvas.setTextColor(INK_DIM);
    canvas.drawString("-", 16, y);
    return;
  }
  uint8_t show = tama.nLines > 6 ? 6 : tama.nLines;
  for (uint8_t i = 0; i < show && y < FOOTER_TOP - 24; i++) {
    canvas.setTextColor(INK);
    // Wrap long Chinese entries so they don't clip off the right edge.
    // Most activity lines are short ("14:23 Bash done") and take 1 row.
    static char wrapped[2][256];
    uint8_t rows = wrapText(tama.lines[i], wrapped, 2, W - 32, TS_MD);
    for (uint8_t r = 0; r < rows && y < FOOTER_TOP - 24; r++) {
      canvas.drawString(wrapped[r], 16, y);
      y += 32;
    }
  }
}

static void drawFooter() {
  if (uiLandscape) {
    drawPanel(LS_RIGHT_X, LS_FOOTER_Y, LS_RIGHT_W, LS_FOOTER_H);
    drawBuddy(LS_RIGHT_X + 96, LS_FOOTER_Y + 78, currentBuddy());

    int rx = LS_RIGHT_X + 180;
    int ry = LS_FOOTER_Y + 18;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    const char* linkStr =
        (bleConnected() && dataBtActive()) ? LX("LINKED", "已连接") :
        bleConnected() ? LX("BLE", "蓝牙") :
        LX("USB / BLE adv", "USB / 蓝牙广播中");
    canvas.drawString(linkStr, rx, ry);
    ry += 28;
    canvas.setTextColor(INK_DIM);
    canvas.drawString(dndMode ? LX("DND ON", "勿扰已开")
                              : LX("DND OFF", "勿扰已关"), rx, ry);
    ry += 24;
    canvas.setTextColor(INK);
    canvas.drawString(LX("Tap session to focus", "点会话切换焦点"), rx, ry); ry += 24;
    canvas.drawString(LX("Top-right = settings", "右上角 = 设置"), rx, ry);
    return;
  }

  // ASCII buddy at text size 3 is ~216×120, so the footer has to be
  // roughly 160px tall to hold it plus a rule + some air.
  int top = FOOTER_TOP;
  drawRule(top);

  // Buddy centered vertically in the footer, left side.
  drawBuddy(120, top + 80, currentBuddy());

  // Right column: link state + touch-first hints.
  canvas.setTextSize(TS_SM);
  int rx = 254;
  int ry = top + 16;
  bool linked = bleConnected() && dataBtActive();
  canvas.setTextColor(linked ? INK : INK_DIM);
  const char* linkStr =
      linked           ? LX("LINKED",        "已连接") :
      bleConnected()   ? LX("BLE connected", "蓝牙已连") :
                         LX("USB / BLE adv", "USB / 蓝牙广播中");
  canvas.drawString(linkStr, rx, ry);
  ry += 28;
  canvas.setTextColor(INK_DIM);
  canvas.drawString(dndMode ? LX("DND ON", "勿扰已开")
                            : LX("DND OFF", "勿扰已关"), rx, ry);
  ry += 24;
  canvas.setTextColor(INK);
  canvas.drawString(LX("Tap session to focus", "点会话切换焦点"), rx, ry); ry += 24;
  canvas.drawString(LX("Top-right = settings", "右上角 = 设置"), rx, ry);
}

static void drawSettings() {
  if (uiLandscape) {
    canvas.fillScreen(PAPER);
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_LG);
    canvas.setTextColor(INK);
    canvas.drawString(LX("SETTINGS", "设置"), W / 2, 24);
    drawRule(56);

    canvas.setTextDatum(TL_DATUM);
    auto row = [&](int x, int y, const char* label, const char* value) {
      canvas.setTextSize(TS_SM);
      canvas.setTextColor(INK);
      canvas.drawString(label, x, y);
      canvas.setTextSize(TS_MD);
      canvas.drawString(value, x, y + 18);
    };

    int x1 = 40, x2 = 500;
    int y1 = 86;
    int y2 = 86;
    char buf[80];

    row(x1, y1, LX("language", "语言"), uiLang == 1 ? "中文  >  English" : "English  >  中文");
    settingsLangHit = { x1 - 8, y1 - 6, 340, 42 };
    y1 += 54;

    row(x1, y1, LX("layout", "布局"),
        uiLandscape ? LX("Landscape  >  Portrait", "横版  >  竖版")
                    : LX("Portrait  >  Landscape", "竖版  >  横版"));
    settingsLayoutHit = { x1 - 8, y1 - 6, 340, 42 };
    y1 += 54;

    row(x1, y1, LX("system", "系统"),
        LX("Return to launcher", "返回启动器"));
    settingsLauncherHit = { x1 - 8, y1 - 6, 340, 42 };
    y1 += 54;

    const char* xport = !tama.connected ? LX("offline", "离线")
                      : (bleConnected() && bleSecure()) ? LX("BLE (paired)", "蓝牙（已配对）")
                      : (bleConnected()) ? "BLE"
                      : LX("USB serial", "USB 串口");
    row(x1, y1, LX("transport", "传输"), xport);
    y1 += 54;

    snprintf(buf, sizeof(buf), LX("%u total / %u run / %u wait",
                                  "共 %u / 运行 %u / 等待 %u"),
             tama.sessionsTotal, tama.sessionsRunning, tama.sessionsWaiting);
    row(x1, y1, LX("sessions", "会话"), buf);
    y1 += 54;

    row(x1, y1, LX("device", "设备"), btName);
    y1 += 54;

    snprintf(buf, sizeof(buf), "%d%%  (%lu mV)", buddyBatteryPercent(), (unsigned long)buddyBatteryVoltage());
    row(x2, y2, LX("battery", "电量"), buf);
    y2 += 54;

    row(x2, y2, LX("DND", "勿扰"),
        dndMode ? LX("ON  (auto-approve)", "开启（自动同意）") : LX("OFF", "关闭"));
    y2 += 54;

    if (tama.budgetLimit > 0) {
      char used[16], lim[16];
      fmtTokens(used, sizeof(used), tama.tokensToday);
      fmtTokens(lim, sizeof(lim), tama.budgetLimit);
      snprintf(buf, sizeof(buf), "%s / %s", used, lim);
      row(x2, y2, LX("budget", "预算"), buf);
    } else {
      row(x2, y2, LX("budget", "预算"), LX("(not set)", "（未设）"));
    }
    y2 += 54;

    uint32_t up = millis() / 1000;
    snprintf(buf, sizeof(buf), LX("%luh %02lum", "%lu 小时 %02lu 分"),
             up / 3600, (up / 60) % 60);
    row(x2, y2, LX("uptime", "运行"), buf);
    y2 += 54;

    uint32_t age = (millis() - tama.lastUpdated) / 1000;
    snprintf(buf, sizeof(buf), LX("%lus ago", "%lu 秒前"), (unsigned long)age);
    row(x2, y2, LX("last msg", "上次消息"), buf);

    int by = H - 72;
    int barX = 40, barW = W - 80, barH = 44, gap = 10;
    int cellW = (barW - gap * 2) / 3;
    drawActionBar(barX, by, barW, barH,
                  LX("LANG", "语言"),
                  LX("LAYOUT", "布局"),
                  LX("LAUNCHER", "启动器"));
    settingsActionLangHit = { barX, by, cellW, barH };
    settingsActionLayoutHit = { barX + cellW + gap, by, cellW, barH };
    settingsActionCloseHit = { barX + (cellW + gap) * 2, by, cellW, barH };
    settingsCloseHit = settingsActionCloseHit;
    return;
  }

  canvas.fillScreen(PAPER);
  canvas.setTextDatum(TC_DATUM);

  canvas.setTextSize(TS_LG);
  canvas.setTextColor(INK);
  canvas.drawString(LX("SETTINGS", "设置"), W / 2, 30);

  drawRule(80);

  canvas.setTextDatum(TL_DATUM);
  int y = 110;
  int lx = 30;

  auto row = [&](const char* label, const char* value, HitRect* hit = nullptr) {
    int rowTop = y - 8;
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(label, lx, y);
    y += 20;
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK);
    canvas.drawString(value, lx, y);
    if (hit) *hit = { lx - 10, rowTop, W - 40, 38 };
    y += 46;
  };

  // Language row — tappable. Label shows the current choice; tapping
  // the value area cycles to the other language and persists.
  {
    row(LX("language", "语言"), uiLang == 1 ? "中文  >  English" : "English  >  中文", &settingsLangHit);
  }

  {
    row(LX("layout", "布局"), uiLandscape ? LX("Landscape  >  Portrait", "横版  >  竖版")
                                          : LX("Portrait  >  Landscape", "竖版  >  横版"),
        &settingsLayoutHit);
  }

  {
    row(LX("system", "系统"),
        LX("Return to launcher", "返回启动器"),
        &settingsLauncherHit);
  }

  const char* xport = !tama.connected ? LX("offline", "离线")
                    : (bleConnected() && bleSecure()) ? LX("BLE (paired)", "蓝牙（已配对）")
                    : (bleConnected()) ? "BLE"
                    : LX("USB serial", "USB 串口");
  row(LX("transport", "传输"), xport);

  char buf[80];
  snprintf(buf, sizeof(buf), LX("%u total / %u run / %u wait",
                                "共 %u / 运行 %u / 等待 %u"),
           tama.sessionsTotal, tama.sessionsRunning, tama.sessionsWaiting);
  row(LX("sessions", "会话"), buf);

  row(LX("device", "设备"), btName);

  uint32_t vBat = buddyBatteryVoltage();
  int pct = buddyBatteryPercent();
  snprintf(buf, sizeof(buf), "%d%%  (%lu mV)", pct, (unsigned long)vBat);
  row(LX("battery", "电量"), buf);

  row(LX("DND", "勿扰"),
      dndMode ? LX("ON  (auto-approve)", "开启（自动同意）") : LX("OFF", "关闭"));

  if (tama.budgetLimit > 0) {
    char used[16], lim[16];
    fmtTokens(used, sizeof(used), tama.tokensToday);
    fmtTokens(lim, sizeof(lim), tama.budgetLimit);
    snprintf(buf, sizeof(buf), "%s / %s", used, lim);
    row(LX("budget", "预算"), buf);
  } else {
    row(LX("budget", "预算"), LX("(not set)", "（未设）"));
  }

  uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), LX("%luh %02lum", "%lu 小时 %02lu 分"),
           up / 3600, (up / 60) % 60);
  row(LX("uptime", "运行"), buf);

  uint32_t age = (millis() - tama.lastUpdated) / 1000;
  snprintf(buf, sizeof(buf), LX("%lus ago", "%lu 秒前"), (unsigned long)age);
  row(LX("last msg", "上次消息"), buf);

  // Tips
  drawRule(y + 10);
  y += 40;
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("TIPS", "提示"), lx, y); y += 32;
  canvas.setTextColor(INK);
  canvas.drawString(LX("Tap a row to change value", "点对应行可直接切换"), lx, y); y += 28;
  canvas.drawString(LX("Bottom bar: lang / layout / close",
                       "底栏：语言 / 布局 / 关闭"), lx, y); y += 28;
  canvas.drawString(LX("Top-right = close settings",
                       "右上角 = 关闭设置"), lx, y); y += 28;
  canvas.drawString(LX("Changes save automatically",
                       "修改会自动保存"), lx, y);

  int by = H - 74;
  int barX = 24, barW = W - 48, barH = 46, gap = 10;
  int cellW = (barW - gap * 2) / 3;
  drawActionBar(barX, by, barW, barH,
                LX("LANG", "语言"),
                LX("LAYOUT", "布局"),
                LX("LAUNCHER", "启动器"));
  settingsActionLangHit = { barX, by, cellW, barH };
  settingsActionLayoutHit = { barX + cellW + gap, by, cellW, barH };
  settingsActionCloseHit = { barX + (cellW + gap) * 2, by, cellW, barH };
  settingsCloseHit = settingsActionCloseHit;
}

static void drawIdle() {
  canvas.fillScreen(PAPER);
  drawHeader();
  drawTopBand();
  drawStats();
  drawClaudeSays();
  drawActivity();
  drawFooter();
}

// (HitRect + settings state declared earlier, before drawSettings().)

// Tab strip at the very top of the approval card when 2+ prompts are
// pending. Each tab is a tappable rect → firmware sends `{"cmd":"focus"}`
// on tap and the daemon swaps ACTIVE_PROMPT. Returns the y below which
// the card body should start (so callers can shift their own content).
static int drawPendingTabs() {
  tabRectCount = 0;
  if (tama.pendingCount <= 1) return 0;

  const int tabH = 48;
  const int margin = 6;
  int n = tama.pendingCount;
  int tabW = (W - margin * (n + 1)) / n;

  canvas.setTextDatum(MC_DATUM);
  canvas.setTextSize(TS_SM);
  for (int i = 0; i < n; i++) {
    int tx = margin + i * (tabW + margin);
    int ty = 4;
    bool active = strcmp(tama.promptId, tama.pending[i].id) == 0;
    if (active) {
      canvas.fillRect(tx, ty, tabW, tabH, INK);
      canvas.setTextColor(PAPER, INK);
    } else {
      for (int d = 0; d < 2; d++)
        canvas.drawRect(tx + d, ty + d, tabW - 2*d, tabH - 2*d, INK);
      canvas.setTextColor(INK);
    }
    // First line: tool name. Second line: project (dim).
    canvas.drawString(tama.pending[i].tool, tx + tabW/2, ty + 16);
    if (!active) canvas.setTextColor(INK_DIM);
    canvas.drawString(tama.pending[i].project, tx + tabW/2, ty + 36);
    tabRects[i] = { tx, ty, tabW, tabH };
    tabRectCount++;
  }
  canvas.setTextDatum(TL_DATUM);
  return tabH + 8;   // body should start this far below y=0
}

static void drawPermissionCard() {
  if (uiLandscape) {
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    canvas.drawString(dndMode ? LX("AUTO-APPROVING (DND)", "自动同意（勿扰）")
                              : LX("PERMISSION REQUESTED", "请求权限"),
                      W / 2, 18);
    canvas.setTextSize(TS_LG);
    canvas.drawString(tama.promptTool[0] ? tama.promptTool : "(tool)", W / 2, 48);

    if (tama.promptProject[0] || tama.promptSid[0]) {
      char who[48];
      if (tama.promptProject[0] && tama.promptSid[0]) snprintf(who, sizeof(who), "%.24s  [%s]", tama.promptProject, tama.promptSid);
      else if (tama.promptProject[0]) snprintf(who, sizeof(who), "%.24s", tama.promptProject);
      else snprintf(who, sizeof(who), "session %s", tama.promptSid);
      canvas.setTextSize(TS_SM);
      canvas.drawString(who, W / 2, 76);
    }

    int bodyX = 20, bodyY = 98, bodyW = 620, bodyH = 332;
    int sideX = 666, sideY = 98, sideW = 274, sideH = 332;
    drawPanel(bodyX, bodyY, bodyW, bodyH);
    drawPanel(sideX, sideY, sideW, sideH);

    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(TS_MD);
    const char* src = tama.promptBody[0] ? tama.promptBody : tama.promptHint;
    if (src[0]) {
      static char wrapped[10][256];
      uint8_t rows = wrapText(src, wrapped, 10, bodyW - 24, TS_MD);
      int ty = bodyY + 16;
      for (uint8_t i = 0; i < rows && ty < bodyY + bodyH - 20; i++) {
        canvas.drawString(wrapped[i], bodyX + 12, ty);
        ty += 30;
      }
    }

    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(TS_LG);
    canvas.drawString(BTN_APPROVE_LABEL, sideX + sideW / 2, sideY + 92);
    canvas.drawString(BTN_DENY_LABEL, sideX + sideW / 2, sideY + 210);
    canvas.setTextSize(TS_SM);
    canvas.drawString(LX("approve", "同意"), sideX + sideW / 2, sideY + 126);
    canvas.drawString(LX("deny", "拒绝"), sideX + sideW / 2, sideY + 244);

    uint32_t waited = (millis() - promptArrivedMs) / 1000;
    char wline[64];
    snprintf(wline, sizeof(wline), LX("waiting %lus", "等待 %lu 秒"), (unsigned long)waited);
    canvas.drawString(wline, sideX + sideW / 2, sideY + sideH - 34);
    if (responseSent) {
      canvas.drawString(LX("sent", "已发送"), sideX + sideW / 2, sideY + sideH - 68);
    }
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  canvas.setTextDatum(TC_DATUM);

  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(dndMode ? LX("AUTO-APPROVING (DND)", "自动同意（勿扰）")
                            : LX("PERMISSION REQUESTED", "请求权限"),
                    W / 2, 20);

  canvas.setTextSize(TS_LG);
  canvas.setTextColor(INK);
  canvas.drawString(tama.promptTool[0] ? tama.promptTool : "(tool)",
                    W / 2, 56);

  // Which project/window this came from — matters when multiple Claude
  // Code windows are open at once.
  if (tama.promptProject[0] || tama.promptSid[0]) {
    char who[48];
    if (tama.promptProject[0] && tama.promptSid[0])
      snprintf(who, sizeof(who), "%.24s  [%s]", tama.promptProject, tama.promptSid);
    else if (tama.promptProject[0])
      snprintf(who, sizeof(who), "%.24s", tama.promptProject);
    else
      snprintf(who, sizeof(who), "session %s", tama.promptSid);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(who, W / 2, 102);
  }

  drawRule(124);

  // Body takes the lion's share of the card now. Size 3 (18x24 glyphs)
  // is significantly more readable than the previous size 2 and still
  // leaves room for ~16 lines.
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK);
  const char* src = tama.promptBody[0] ? tama.promptBody : tama.promptHint;
  if (src[0]) {
    static char wrapped[18][256];
    uint8_t rows = wrapText(src, wrapped, 18, W - 40, TS_MD);
    int ty = 140;
    for (uint8_t i = 0; i < rows; i++) {
      canvas.drawString(wrapped[i], 20, ty);
      ty += 32;
      if (ty > 740) break;
    }
  }

  drawRule(770);

  canvas.setTextDatum(TC_DATUM);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  char wline[48]; snprintf(wline, sizeof(wline), LX("waiting %lus", "等待 %lu 秒"),
                           (unsigned long)waited);
  canvas.drawString(wline, W / 2, 790);

  if (responseSent) {
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK_DIM);
    canvas.drawString(LX("sent - waiting for Claude...", "已发送 - 等 Claude 继续..."),
                      W / 2, 870);
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  // Side-by-side action columns at the bottom — approve on left, deny on
  // right. More compact than the stacked layout, leaves more body room.
  int cy = 870;
  canvas.setTextSize(TS_LG);
  canvas.setTextColor(INK);
  canvas.drawString(BTN_APPROVE_LABEL, W / 4, cy);
  canvas.drawString(BTN_DENY_LABEL, 3 * W / 4, cy);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("approve", "同意"), W / 4,     cy + 50);
  canvas.drawString(LX("deny",    "拒绝"), 3 * W / 4, cy + 50);

  canvas.setTextDatum(TL_DATUM);
}

// Question card: big touch-target buttons, one per option. Tapping sends
// the answer back via the daemon; the daemon resolves option index → label
// and tells Claude "user selected X, proceed".
static void drawQuestionCard() {
  if (uiLandscape) {
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    canvas.drawString(LX("QUESTION FROM CLAUDE", "Claude 提问"), W / 2, 18);

    const char* src = tama.promptBody[0] ? tama.promptBody : tama.promptHint;
    int bodyX = 20, bodyY = 72, bodyW = 400, bodyH = 360;
    int optX = 450, optY = 72, optW = 490, optH = 360;
    drawPanel(bodyX, bodyY, bodyW, bodyH);
    drawPanel(optX, optY, optW, optH);

    canvas.setTextDatum(TL_DATUM);
    canvas.setTextSize(TS_MD);
    if (src[0]) {
      static char wrapped[8][256];
      uint8_t rows = wrapText(src, wrapped, 8, bodyW - 24, TS_MD);
      int y = bodyY + 16;
      for (uint8_t i = 0; i < rows && y < bodyY + bodyH - 20; i++) {
        canvas.drawString(wrapped[i], bodyX + 12, y);
        y += 30;
      }
    }

    optionRectCount = 0;
    int n = tama.promptOptionCount > 0 ? tama.promptOptionCount : 1;
    int gap = 10;
    int btnH = (optH - gap * (n + 1)) / n;
    canvas.setTextDatum(MC_DATUM);
    for (int i = 0; i < tama.promptOptionCount; i++) {
      int by = optY + gap + i * (btnH + gap);
      if (i == selectedOption) {
        canvas.fillRect(optX + 12, by, optW - 24, btnH, INK);
        canvas.setTextColor(PAPER, INK);
      } else {
        drawPanel(optX + 12, by, optW - 24, btnH);
        canvas.setTextColor(INK);
      }
      canvas.setTextSize(TS_MD);
      char line[72];
      snprintf(line, sizeof(line), "%d  %s", i + 1, tama.promptOptions[i]);
      canvas.drawString(line, optX + optW / 2, by + btnH / 2);
      optionRects[i] = { optX + 12, by, optW - 24, btnH };
      optionRectCount++;
    }
    if (tama.promptOptionCount == 0) {
      canvas.setTextColor(INK);
      canvas.drawString("(no options provided)", optX + optW / 2, optY + optH / 2);
    }
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK);
    uint32_t waited = (millis() - promptArrivedMs) / 1000;
    char wline[64];
    snprintf(wline, sizeof(wline), LX("waiting %lus · %s = cancel",
                                      "等待 %lu 秒 · %s = 取消"),
             (unsigned long)waited, BTN_DENY_LABEL);
    canvas.drawString(wline, W / 2, H - 22);
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  canvas.setTextDatum(TC_DATUM);

  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("QUESTION FROM CLAUDE", "Claude 提问"), W / 2, 20);

  // Project / session origin — helps disambiguate when multiple windows.
  if (tama.promptProject[0] || tama.promptSid[0]) {
    char who[48];
    if (tama.promptProject[0] && tama.promptSid[0])
      snprintf(who, sizeof(who), "%.24s  [%s]", tama.promptProject, tama.promptSid);
    else if (tama.promptProject[0])
      snprintf(who, sizeof(who), "%.24s", tama.promptProject);
    else
      snprintf(who, sizeof(who), "session %s", tama.promptSid);
    canvas.drawString(who, W / 2, 46);
  }

  // Question text (falls back to hint if no body). Up to ~3 lines at size 3
  // above the options area.
  const char* src = tama.promptBody[0] ? tama.promptBody : tama.promptHint;
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK);
  int qy = 90;
  if (src[0]) {
    static char wrapped[4][256];
    uint8_t rows = wrapText(src, wrapped, 4, W - 40, TS_MD);
    for (uint8_t i = 0; i < rows && qy < 220; i++) {
      canvas.drawString(wrapped[i], 24, qy);
      qy += 34;
    }
  }

  // Option buttons — stack up to 4, equal height, 10px gap.
  optionRectCount = 0;
  if (tama.promptOptionCount == 0) {
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_SM);
    canvas.setTextColor(INK_DIM);
    canvas.drawString("(no options provided)", W / 2, 500);
    canvas.setTextDatum(TL_DATUM);
  } else {
    const int top = 250;
    const int bottom = 870;
    const int gap = 10;
    int n = tama.promptOptionCount;
    int btnH = (bottom - top - gap * (n - 1)) / n;
    int bx = 20, bw = W - 40;

    canvas.setTextDatum(MC_DATUM);
    for (int i = 0; i < n; i++) {
      int by = top + i * (btnH + gap);
      bool tapped = (i == selectedOption);
      if (tapped) {
        // Inverted fill — black background, white text — makes the tap
        // feedback unambiguous even at arm's length.
        canvas.fillRect(bx, by, bw, btnH, INK);
        canvas.setTextColor(PAPER, INK);
      } else {
        // 3px outline when not selected.
        for (int d = 0; d < 3; d++) {
          canvas.drawRect(bx + d, by + d, bw - 2*d, btnH - 2*d, INK);
        }
        canvas.setTextColor(INK);
      }
      canvas.setTextSize(TS_LG);
      char line[56];
      snprintf(line, sizeof(line), "%d  %s", i + 1, tama.promptOptions[i]);
      canvas.drawString(line, W / 2, by + btnH / 2);

      optionRects[i] = { bx, by, bw, btnH };
      optionRectCount++;
    }
    canvas.setTextDatum(TL_DATUM);
  }

  // Footer: waited counter + physical button hint (deny button = cancel).
  canvas.setTextDatum(TC_DATUM);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  char wline[64]; snprintf(wline, sizeof(wline),
                           LX("waiting %lus   ·   %s = cancel",
                              "等待 %lu 秒   ·   %s = 取消"),
                           (unsigned long)waited, BTN_DENY_LABEL);
  canvas.drawString(wline, W / 2, 910);

  if (responseSent) {
    canvas.setTextColor(INK);
    canvas.drawString(LX("sent - waiting for Claude to resume...",
                         "已发送 - 等 Claude 继续..."),
                      W / 2, 940);
  }
  canvas.setTextDatum(TL_DATUM);
}

static void drawApproval() {
  canvas.fillScreen(PAPER);
  // No tabs on the approval card. Approvals FIFO out of the daemon's
  // queue; only one is shown at a time, resolving it pops the next.
  tabRectCount = 0;
  bool isQuestion = (strcmp(tama.promptKind, "question") == 0);
  if (isQuestion) drawQuestionCard();
  else            drawPermissionCard();
}

static void drawSplash() {
  if (uiLandscape) {
    canvas.fillScreen(PAPER);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(TS_XXL);
    canvas.setTextColor(INK);
    canvas.drawString("Paper Buddy", W / 2, 90);
    canvas.setTextDatum(TL_DATUM);
    drawBuddy(W / 2, H / 2 + 10, B_IDLE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextSize(TS_MD);
    canvas.drawString(BUDDY_DEVICE_LABEL, W / 2, H - 90);
    canvas.setTextSize(TS_SM);
    canvas.drawString(btName, W / 2, H - 56);
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  canvas.fillScreen(PAPER);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextSize(TS_XXL);
  canvas.setTextColor(INK);
  canvas.drawString("Paper Buddy", W/2, H/2 - 160);
  canvas.setTextDatum(TL_DATUM);
  drawBuddy(W/2, H/2, B_IDLE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(BUDDY_DEVICE_LABEL, W/2, H/2 + 120);
  canvas.setTextSize(TS_SM);
  canvas.drawString(btName, W/2, H/2 + 170);
  canvas.setTextDatum(TL_DATUM);
}

static void drawPasskey() {
  if (uiLandscape) {
    canvas.fillScreen(PAPER);
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(TS_MD);
    canvas.setTextColor(INK);
    canvas.drawString(LX("BLUETOOTH PAIRING", "蓝牙配对"), W / 2, 96);
    canvas.setTextSize(TS_HUGE);
    char b[8]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
    canvas.drawString(b, W / 2, 212);
    canvas.setTextSize(TS_SM);
    canvas.drawString(LX("enter this on the desktop", "在电脑上输入这个数字"), W / 2, 316);
    canvas.setTextDatum(TL_DATUM);
    return;
  }

  canvas.fillScreen(PAPER);
  canvas.setTextDatum(TC_DATUM);
  canvas.setTextSize(TS_MD);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("BLUETOOTH PAIRING", "蓝牙配对"), W/2, 200);
  canvas.setTextSize(TS_HUGE);
  canvas.setTextColor(INK);
  char b[8]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  canvas.drawString(b, W/2, 340);
  canvas.setTextSize(TS_SM);
  canvas.setTextColor(INK_DIM);
  canvas.drawString(LX("enter this on the desktop", "在电脑上输入这个数字"), W/2, 560);
  canvas.setTextDatum(TL_DATUM);
}

// -----------------------------------------------------------------------------

// GC16 = 16-gray with flash, crispest but blinks. Use it for mode
// changes (approval card ↔ idle) where the flash is acceptable.
// GL16 = 16-gray without flash — preserves TTF anti-aliasing so text
// doesn't look muddy after many partial updates. Slightly slower than
// DU (~450ms vs 260ms) but much cleaner for small-font content.
static bool pushFull() {
  if (!buddyPushFullDisplay()) return false;
  lastFullRefreshMs = lastPartialRefreshMs = millis();
  return true;
}
static bool pushPartial() {
  if (!buddyPushPartialDisplay()) return false;
  lastPartialRefreshMs = millis();
  return true;
}

static bool repaint(bool wantFull) {
  uint32_t pk = blePasskey();
  bool pairingMode = pk != 0;
  bool promptMode = tama.promptId[0] != 0;
  bool overlayMode = pairingMode || settingsOpen || promptMode;

  buddyFrameBegin();
  if (pairingMode)         drawPasskey();
  else if (settingsOpen)   drawSettings();
  else if (promptMode)     drawApproval();
  else                     drawIdle();
  buddyFrameEnd();

  bool modeChanged = overlayMode != lastMode;
  lastMode = overlayMode;

  if (wantFull || modeChanged) return pushFull();
  return pushPartial();
}

// -----------------------------------------------------------------------------

static void dndLoad() {
  Preferences p; p.begin("buddy", true);
  dndMode = p.getBool("dnd", false);
  uiLang  = p.getUChar("lang", 0);
  if (uiLang > 1) uiLang = 0;
  uiLandscape = p.getBool("land", BUDDY_DEFAULT_LANDSCAPE);
  p.end();
}
static void dndSave() {
  Preferences p; p.begin("buddy", false);
  p.putBool("dnd", dndMode);
  p.end();
}
static void langSave() {
  Preferences p; p.begin("buddy", false);
  p.putUChar("lang", uiLang);
  p.end();
}
static void layoutSave() {
  Preferences p; p.begin("buddy", false);
  p.putBool("land", uiLandscape);
  p.end();
}

#if CONFIG_IDF_TARGET_ESP32S3 && !defined(BUDDY_TARGET_PAPERS3)
static bool isValidEsp32S3Gpio(int pin) {
  return pin >= 0 && pin <= 48 && pin != 22 && pin != 23 && pin != 24 && pin != 25;
}

static bool requireEsp32S3Gpio(const char* name, int pin) {
  if (isValidEsp32S3Gpio(pin)) return true;
  Serial.printf("[boot] invalid ESP32-S3 pin for %s: GPIO%d\n", name, pin);
  return false;
}

static bool validateEsp32S3PinMap() {
  bool ok = true;
  ok &= requireEsp32S3Gpio("M5EPD_MAIN_PWR_PIN",   M5EPD_MAIN_PWR_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_CS_PIN",         M5EPD_CS_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_SCK_PIN",        M5EPD_SCK_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_MOSI_PIN",       M5EPD_MOSI_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_BUSY_PIN",       M5EPD_BUSY_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_MISO_PIN",       M5EPD_MISO_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_EXT_PWR_EN_PIN", M5EPD_EXT_PWR_EN_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_EPD_PWR_EN_PIN", M5EPD_EPD_PWR_EN_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_KEY_RIGHT_PIN",  M5EPD_KEY_RIGHT_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_KEY_PUSH_PIN",   M5EPD_KEY_PUSH_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_KEY_LEFT_PIN",   M5EPD_KEY_LEFT_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_BAT_VOL_PIN",    M5EPD_BAT_VOL_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_TOUCH_SDA_PIN",  M5EPD_TOUCH_SDA_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_TOUCH_SCL_PIN",  M5EPD_TOUCH_SCL_PIN);
  ok &= requireEsp32S3Gpio("M5EPD_TOUCH_INT_PIN",  M5EPD_TOUCH_INT_PIN);
  return ok;
}
#endif

void setup() {
#if CONFIG_IDF_TARGET_ESP32S3 && !defined(BUDDY_TARGET_PAPERS3)
  Serial.begin(115200);
  delay(50);
  Serial.printf("[boot] env=%s device=%s target=esp32s3\n",
                BUDDY_PIO_ENV, BUDDY_DEVICE_LABEL);
  if (!validateEsp32S3PinMap()) {
    Serial.println("[boot] default M5Paper pin map is not valid on ESP32-S3.");
    Serial.println("[boot] set M5EPD_* build flags to match your actual wiring, then rebuild.");
    while (true) delay(1000);
  }
#else
  Serial.begin(115200);
  delay(50);
  Serial.printf("[boot] env=%s device=%s target=%s\n",
                BUDDY_PIO_ENV, BUDDY_DEVICE_LABEL,
#ifdef BUDDY_TARGET_PAPERS3
                "papers3"
#else
                "esp32"
#endif
                );
#endif

  buddyBegin();

  // Print the cause of the previous reset so crash loops can be debugged
  // over serial. rtc_get_reset_reason() codes:
  //   1=POWERON, 3=SW, 5=DEEPSLEEP, 6=SDIO, 7=TG0WDT_SYS, 8=TG1WDT_SYS,
  //   9=RTCWDT_SYS, 11=INTRUSION, 12=TGWDT_CPU, 13=SW_CPU,
  //   14=RTCWDT_CPU, 15=EXT_CPU, 16=RTCWDT_BROWN_OUT, 17=RTCWDT_RTC
  Serial.printf("[boot] reset reason cpu0=%d cpu1=%d free_heap=%u\n",
                (int)rtc_get_reset_reason(0), (int)rtc_get_reset_reason(1),
                ESP.getFreeHeap());

  buddyTouchSetRotation(90);
  buddyClearDisplay(true);

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount failed — continuing without it");
  }
  // Debug: dump LittleFS root so we can verify the font file is present.
  {
    File root = LittleFS.open("/");
    if (root && root.isDirectory()) {
      Serial.printf("[fs] LittleFS total=%u used=%u\n",
                    LittleFS.totalBytes(), LittleFS.usedBytes());
      File f = root.openNextFile();
      while (f) {
        Serial.printf("[fs]  %s  %u bytes\n", f.name(), f.size());
        f = root.openNextFile();
      }
    }
  }

  buddyCreateCanvas(W, H);

#ifdef BUDDY_TARGET_PAPERS3
  canvas.setFont(&fonts::efontCN_16);
  bool fontOk = true;
  Serial.println("[font] using built-in M5GFX efontCN_16");
#else
  // Load the CJK TTF from LittleFS.
  // M5EPD's loadFont returns esp_err_t — ESP_OK = 0 means success, so we
  // compare instead of treating it as bool.
  esp_err_t rc = canvas.loadFont("/cjk.ttf", LittleFS);
  bool fontOk = (rc == ESP_OK);
  Serial.printf("[font] loadFont cjk.ttf rc=%d (%s)\n", (int)rc,
                fontOk ? "OK" : "FAIL");
  if (fontOk) {
    // Warm a render for every size we draw at. 128-glyph cache per size
    // keeps common CJK glyphs resident without blowing PSRAM.
    for (int sz : { TS_SM, TS_MD, TS_LG, TS_XL, TS_XXL, TS_HUGE }) {
      canvas.createRender(sz, 128);
    }
  }
#endif

  statsLoad();
  settingsLoad();
  petNameLoad();
  dndLoad();
  buddySetLandscape(uiLandscape);
  startBt();

  drawSplash();
  (void)pushFull();
  delay(1500);

  redrawPending = true;
  lastFullRefreshMs = millis();
  lastPartialRefreshMs = 0;   // allow the first dashboard repaint immediately
}

void loop() {
  buddySetTouchButtonHeight(settingsOpen ? 56 : 0);
  M5.update();
  uint32_t now = millis();

  dataPoll(&tama);

  static uint32_t lastPasskey = 0;
  uint32_t currentPasskey = blePasskey();
  if (currentPasskey != lastPasskey) {
    lastPasskey = currentPasskey;
    redrawPending = true;
    lastPartialRefreshMs = 0;
  }

  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    dndAutoSent = false;
    selectedOption = -1;
    if (tama.promptId[0]) promptArrivedMs = now;
    redrawPending = true;
    // Mode transition (prompt arrives or leaves) — bypass the 2s DU
    // rate limit so the screen flips over immediately. Otherwise a tap
    // feedback or approval card entrance can lag up to 2s.
    lastPartialRefreshMs = 0;
  }

  bool inPrompt = tama.promptId[0] && !responseSent;
  bool isQuestion = inPrompt && strcmp(tama.promptKind, "question") == 0;

  auto handleTouchRelease = [&](int lastX, int lastY) {
    Serial.printf("[tp] up   @ %d,%d  (inPrompt=%d isQ=%d opts=%u settings=%d)\n",
                  lastX, lastY, (int)inPrompt, (int)isQuestion,
                  (unsigned)optionRectCount, (int)settingsOpen);

    auto hitTest = [&](const HitRect& r) {
      return lastX >= r.x && lastX < r.x + r.w &&
             lastY >= r.y && lastY < r.y + r.h;
    };

    if (settingsOpen) {
      // Language row first — tap toggles EN/ZH in place.
      if (hitTest(settingsActionLangHit) || hitTest(settingsLangHit)) {
        uiLang = uiLang == 0 ? 1 : 0;
        langSave();
        lastFullRefreshMs = 0;
        lastPartialRefreshMs = 0;
        redrawPending = true;
      } else if (hitTest(settingsActionLayoutHit) || hitTest(settingsLayoutHit)) {
        uiLandscape = !uiLandscape;
        layoutSave();
        buddySetLandscape(uiLandscape);
        settingsOpen = true;
        lastPartialRefreshMs = 0;
        forceFullRefresh = true;
        redrawPending = true;
      } else if (hitTest(settingsActionCloseHit)) {
        settingsOpen = false;
        lastPartialRefreshMs = 0;
        forceFullRefresh = true;
        redrawPending = true;
        (void)returnToLauncher();
      } else if (hitTest(settingsLauncherHit)) {
        settingsOpen = false;
        lastPartialRefreshMs = 0;
        forceFullRefresh = true;
        redrawPending = true;
        (void)returnToLauncher();
      } else if (hitTest(settingsCloseHit) || hitTest(settingsTrigger)) {
        settingsOpen = false;
        lastPartialRefreshMs = 0;
        forceFullRefresh = true;
        redrawPending = true;
      } else {
        settingsOpen = false;
        lastPartialRefreshMs = 0;
        forceFullRefresh = true;
        redrawPending = true;
      }
    } else if (!inPrompt && hitTest(settingsTrigger)) {
      settingsOpen = true;
      lastPartialRefreshMs = 0;
      forceFullRefresh = true;
      redrawPending = true;
    } else if (!inPrompt && sessionRectCount > 1) {
      for (uint8_t i = 0; i < sessionRectCount; i++) {
        if (hitTest(sessionRects[i])) {
          char cmd[80];
          snprintf(cmd, sizeof(cmd), "{\"cmd\":\"focus_session\",\"sid\":\"%s\"}",
                   tama.sessions[i].full[0] ? tama.sessions[i].full : tama.sessions[i].sid);
          sendCmd(cmd);
          break;
        }
      }
    } else if (inPrompt && tabRectCount > 1 && lastY < 60) {
      for (uint8_t i = 0; i < tabRectCount; i++) {
        if (hitTest(tabRects[i])) {
          char cmd[96];
          snprintf(cmd, sizeof(cmd), "{\"cmd\":\"focus\",\"id\":\"%s\"}",
                   tama.pending[i].id);
          sendCmd(cmd);
          break;
        }
      }
    } else if (isQuestion && optionRectCount > 0) {
      for (uint8_t i = 0; i < optionRectCount; i++) {
        const HitRect& r = optionRects[i];
        if (lastX >= r.x && lastX < r.x + r.w &&
            lastY >= r.y && lastY < r.y + r.h) {
          Serial.printf("[tp] HIT option %u\n", (unsigned)i);
          selectedOption = i;
          responseSent = true;
          lastPartialRefreshMs = 0;
          repaint(false);

          char cmd[96];
          snprintf(cmd, sizeof(cmd),
                   "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"option:%u\"}",
                   tama.promptId, (unsigned)i);
          sendCmd(cmd);
          uint32_t tookS = (now - promptArrivedMs) / 1000;
          statsOnApproval(tookS);
          celebrateUntil = now + 4000;
          redrawPending = true;
          break;
        }
      }
    }
  };

  static int  lastX = 0, lastY = 0;
  static bool hadTouch = false;
#ifdef BUDDY_TARGET_PAPERS3
  auto td = M5.Touch.getDetail();
  if (td.isPressed()) {
    lastX = td.x;
    lastY = td.y;
    hadTouch = true;
    Serial.printf("[tp] down @ %d,%d\n", lastX, lastY);
  } else if (hadTouch && td.wasReleased()) {
    hadTouch = false;
    handleTouchRelease(lastX, lastY);
  }
#else
  // Touch input. GT911 is interrupt-driven: available() goes true on
  // each finger-down/finger-up event. We track the latest coords while
  // finger is pressed, then hit-test when isFingerUp() fires.
  if (M5.TP.available()) {
    M5.TP.update();

    bool up = M5.TP.isFingerUp();
    if (!up) {
      tp_finger_t f = M5.TP.readFinger(0);
      lastX = f.x; lastY = f.y;
      hadTouch = true;
      Serial.printf("[tp] down @ %d,%d\n", lastX, lastY);
    } else if (hadTouch) {
      hadTouch = false;
      handleTouchRelease(lastX, lastY);
    }
    M5.TP.flush();
  }
#endif


  if (inPrompt && dndMode && !dndAutoSent && now - promptArrivedMs >= DND_AUTO_DELAY_MS) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
    sendCmd(cmd);
    responseSent = true;
    dndAutoSent = true;
    uint32_t tookS = (now - promptArrivedMs) / 1000;
    statsOnApproval(tookS);
    celebrateUntil = now + 4000;
    redrawPending = true;
  }

  if (BTN_APPROVE.wasPressed()) {
    if (settingsOpen) {
      uiLandscape = !uiLandscape;
      layoutSave();
      buddySetLandscape(uiLandscape);
      settingsOpen = true;
      lastPartialRefreshMs = 0;
      forceFullRefresh = true;
      redrawPending = true;
    } else if (inPrompt) {
      char cmd[96];
      snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
      sendCmd(cmd);
      responseSent = true;
      uint32_t tookS = (now - promptArrivedMs) / 1000;
      statsOnApproval(tookS);
      celebrateUntil = now + 4000;
      redrawPending = true;
    } else {
      redrawPending = true;
    }
  }

  if (BTN_DENY.wasPressed()) {
    if (settingsOpen) {
      settingsOpen = false;
      lastPartialRefreshMs = 0;
      forceFullRefresh = true;
      redrawPending = true;
      (void)returnToLauncher();
    } else if (inPrompt) {
      char cmd[96];
      snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", tama.promptId);
      sendCmd(cmd);
      responseSent = true;
      statsOnDenial();
      redrawPending = true;
    } else {
      dataSetDemo(!dataDemo());
      redrawPending = true;
    }
  }

  static bool upLongFired = false;
  if (BTN_UP.pressedFor(1500) && !upLongFired && !inPrompt) {
    upLongFired = true;
    if (settingsOpen) {
      uiLang = uiLang == 0 ? 1 : 0;
      langSave();
      lastPartialRefreshMs = 0;
      forceFullRefresh = true;
      redrawPending = true;
    } else {
      dndMode = !dndMode;
      dndSave();
      forceFullRefresh = true;
      redrawPending = true;
    }
  }
  if (BTN_UP.wasReleased()) {
    if (!upLongFired && !inPrompt) {
      lastFullRefreshMs = 0;
      redrawPending = true;
    }
    upLongFired = false;
  }

  static uint32_t launcherComboSince = 0;
  static bool launcherComboFired = false;
  bool launcherComboActive =
      !inPrompt && !settingsOpen && BTN_APPROVE.isPressed() && BTN_DENY.isPressed();
  if (launcherComboActive) {
    if (launcherComboSince == 0) {
      launcherComboSince = now;
    } else if (!launcherComboFired && now - launcherComboSince >= 2000) {
      launcherComboFired = true;
      (void)returnToLauncher();
    }
  } else {
    launcherComboSince = 0;
    launcherComboFired = false;
  }

  static uint16_t   lastLineGen = 0, lastAsstGen = 0;
  static uint8_t    lastT = 255, lastR = 255, lastW = 255;
  static bool       lastConn = false;
  static uint32_t   lastTokDay = 0xFFFFFFFF;
  static uint16_t   lastDirty = 0xFFFF;
  static uint32_t   lastBudget = 0xFFFFFFFF;
  static char       lastBranch[40] = "\x01";
  static char       lastModel[32]  = "\x01";
  bool dataChanged = (tama.lineGen != lastLineGen)
                  || (tama.assistantGen != lastAsstGen)
                  || (tama.sessionsTotal != lastT) || (tama.sessionsRunning != lastR)
                  || (tama.sessionsWaiting != lastW) || (tama.connected != lastConn)
                  || (tama.tokensToday != lastTokDay) || (tama.dirty != lastDirty)
                  || (tama.budgetLimit != lastBudget)
                  || strcmp(tama.branch, lastBranch) != 0
                  || strcmp(tama.modelName, lastModel) != 0;
  if (dataChanged) {
    lastLineGen = tama.lineGen;  lastAsstGen = tama.assistantGen;
    lastT = tama.sessionsTotal;
    lastR = tama.sessionsRunning; lastW = tama.sessionsWaiting;
    lastConn = tama.connected; lastTokDay = tama.tokensToday;
    lastDirty = tama.dirty; lastBudget = tama.budgetLimit;
    strncpy(lastBranch, tama.branch, sizeof(lastBranch) - 1); lastBranch[sizeof(lastBranch) - 1] = 0;
    strncpy(lastModel, tama.modelName, sizeof(lastModel) - 1); lastModel[sizeof(lastModel) - 1] = 0;
    // Bypass the 30s idle throttle when real state changes arrive from the daemon.
    lastPartialRefreshMs = 0;
    redrawPending = true;
  }

  static uint32_t lastPromptTick = 0;
  if (inPrompt && now - lastPromptTick >= 1000) {
    lastPromptTick = now;
    redrawPending = true;
  }

  // Idle dashboard: slow refresh (every 30s) so the e-ink isn't constantly
  // re-inking on heartbeat noise. Approval card / question card /
  // settings: fast refresh (2s) because they're interactive.
  // Mode transitions (prompt arrives/leaves) already bypass this by
  // setting lastPartialRefreshMs = 0.
  bool interactive = inPrompt || settingsOpen;
  uint32_t partialGap = interactive ? 2000UL : 30000UL;
  bool canPartial = (now - lastPartialRefreshMs) >= partialGap;
  bool shouldFull = forceFullRefresh || (now - lastFullRefreshMs) >= 120000UL;   // GC16 sweep every 2 min

  if (redrawPending && !buddyDisplayBusy() && (canPartial || shouldFull)) {
    if (repaint(shouldFull)) {
      redrawPending = false;
      forceFullRefresh = false;
    }
  }

  delay(20);
}
