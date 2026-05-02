// Halftone Display — Uno Slave
// Receives mode + float value from Mega master over Serial (pin 0 RX).
//
// IMPORTANT: Unplug the wire from D0 before uploading via USB.
//            Reconnect after upload is complete.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

// ---------- Colors ----------
#define BLACK  0x0000
#define WHITE  0xFFFF
#define GREY   0x9CD3

// ---------- Layout ----------
#define CX 120
#define CY 125
#define MAX_RADIUS 112
#define GRID 3

// ---------- Mode definitions (must match master) ----------
#define MODE_IDLE  0
#define MODE_GHG   1
#define MODE_WATER 2
#define MODE_BIO   3

struct ModeConfig {
  const char* label1;
  const char* label2;
  const char* unit;
  float vmin;
  float vmax;
  uint8_t decimals;
};

const ModeConfig modes[] = {
  {"",           "",          "",  0.0, 1.0,   0},   // MODE_IDLE
  {"Greenhouse", "  Gases",   "",  0.0, 100.0, 0},   // MODE_GHG
  {"  Water",    " Quality",  "",  0.0, 100.0, 0},   // MODE_WATER
  {"  Bio-",     "diversity", "",  0.0, 100.0, 0},   // MODE_BIO
};

// ---------- Grid ----------
#define GX_START (CX - MAX_RADIUS)
#define GY_START (CY - MAX_RADIUS)
#define COLS     ((2 * MAX_RADIUS) / GRID + 1)
#define ROWS     ((2 * MAX_RADIUS) / GRID + 1)
#define BIT_COLS ((COLS + 7) / 8)

uint8_t dotState[ROWS][BIT_COLS];

// ---------- Bayer ----------
const uint8_t PROGMEM BAYER[8][8] = {
  { 0, 32,  8, 40,  2, 34, 10, 42},
  {48, 16, 56, 24, 50, 18, 58, 26},
  {12, 44,  4, 36, 14, 46,  6, 38},
  {60, 28, 52, 20, 62, 30, 54, 22},
  { 3, 35, 11, 43,  1, 33,  9, 41},
  {51, 19, 59, 27, 49, 17, 57, 25},
  {15, 47,  7, 39, 13, 45,  5, 37},
  {63, 31, 55, 23, 61, 29, 53, 21}
};

// ---------- State ----------
float displayValue = 0.0;
uint8_t activeMode = MODE_IDLE;
uint8_t rxMode = MODE_IDLE;
unsigned long lastDotTime = 0;
char prevValueStr[16] = "";

// ---------- Serial receive buffer ----------
uint8_t rxBuf[7];
uint8_t rxIdx = 0;

void processSerial() {
  while (Serial.available()) {
    uint8_t b = Serial.read();

    if (rxIdx == 0) {
      if (b == 0xAA) {
        rxBuf[rxIdx++] = b;
      }
    } else {
      rxBuf[rxIdx++] = b;

      if (rxIdx == 7) {
        if (rxBuf[6] == 0x55) {
          rxMode = rxBuf[1];
          float val;
          memcpy(&val, &rxBuf[2], sizeof(float));
          displayValue = val;
        }
        rxIdx = 0;
      }
    }
  }
}

// ---------- Bitfield helpers ----------
inline bool getBit(uint8_t row, uint8_t col) {
  return (dotState[row][col >> 3] >> (col & 7)) & 1;
}

inline void setBit(uint8_t row, uint8_t col, bool val) {
  uint8_t mask = 1 << (col & 7);
  if (val) dotState[row][col >> 3] |= mask;
  else     dotState[row][col >> 3] &= ~mask;
}

void setDot(uint8_t row, uint8_t col, int px, int py, bool on) {
  if (getBit(row, col) == on) return;
  tft.drawPixel(px, py, on ? WHITE : BLACK);
  setBit(row, col, on);
}

// ---------- Draw labels ----------
void drawLabels(uint8_t mode) {
  tft.fillRect(0, 250, 240, 80, BLACK);

  if (mode == MODE_IDLE) {
    tft.setTextColor(GREY, BLACK);
    tft.setTextSize(3);
    int w = 4 * 18;
    tft.setCursor((240 - w) / 2, 265);
    tft.print("Idle");
    return;
  }

  const ModeConfig &cfg = modes[mode];

  tft.setTextColor(GREY, BLACK);
  tft.setTextSize(2);

  int w1 = strlen(cfg.label1) * 12;
  int w2 = strlen(cfg.label2) * 12;
  tft.setCursor((240 - w1) / 2, 255);
  tft.print(cfg.label1);
  tft.setCursor((240 - w2) / 2, 275);
  tft.print(cfg.label2);
}

// ---------- Draw value ----------
void drawValue(float val, uint8_t mode) {
  if (mode == MODE_IDLE) return;

  const ModeConfig &cfg = modes[mode];

  char str[16];
  dtostrf(val, 1, cfg.decimals, str);
  strcat(str, cfg.unit);

  if (strcmp(str, prevValueStr) == 0) return;
  strcpy(prevValueStr, str);

  char padded[12];
  int len = strlen(str);
  int total = 8;
  int leftPad = (total - len) / 2;
  int rightPad = total - len - leftPad;
  int idx = 0;
  for (int i = 0; i < leftPad; i++) padded[idx++] = ' ';
  for (int i = 0; i < len; i++) padded[idx++] = str[i];
  for (int i = 0; i < rightPad; i++) padded[idx++] = ' ';
  padded[idx] = '\0';

  tft.setTextColor(GREY, BLACK);
  tft.setTextSize(3);
  int w = total * 18;
  tft.setCursor((240 - w) / 2, 296);
  tft.print(padded);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(5));

  uint16_t ID = tft.readID();
  if (ID == 0xD3D3) ID = 0x9481;
  tft.begin(ID);
  tft.setRotation(0);
  tft.fillScreen(BLACK);

  drawLabels(activeMode);
  memset(dotState, 0, sizeof(dotState));
}

// ---------- Main loop ----------
void loop() {
  unsigned long now = millis();

  // --- Read serial data from master ---
  processSerial();

  // --- Mode switch: redraw labels, clear dots ---
  if (rxMode != activeMode) {
    activeMode = rxMode;
    drawLabels(activeMode);
    prevValueStr[0] = '\0';

    for (uint8_t row = 0; row < ROWS; row++) {
      for (uint8_t col = 0; col < COLS; col++) {
        if (getBit(row, col)) {
          int px = GX_START + (int)col * GRID;
          int py = GY_START + (int)row * GRID;
          tft.drawPixel(px, py, BLACK);
        }
      }
    }
    memset(dotState, 0, sizeof(dotState));
  }

  // --- Value display ---
  float val = displayValue;
  drawValue(val, activeMode);

  // --- Dot update (skip in idle) ---
  if (activeMode != MODE_IDLE && now - lastDotTime >= 50) {
    lastDotTime = now;

    const ModeConfig &cfg = modes[activeMode];
    float fill = (val - cfg.vmin) / (cfg.vmax - cfg.vmin);
    float fadeZone = fill * 0.45f;
    float solidEdge = fill - fadeZone;

    float solidR = solidEdge * MAX_RADIUS;
    float fillR  = fill * MAX_RADIUS;
    long solidRSq = (long)(solidR * solidR);
    long fillRSq  = (long)(fillR * fillR);
    long maxRSq   = (long)MAX_RADIUS * MAX_RADIUS;

    float invFadeR = 1.0f / max(fillR - solidR, 0.001f);

    for (uint8_t row = 0; row < ROWS; row++) {
      int py = GY_START + (int)row * GRID;
      int dy = py - CY;
      long dySq = (long)dy * dy;

      for (uint8_t col = 0; col < COLS; col++) {
        int px = GX_START + (int)col * GRID;
        int dx = px - CX;
        long distSq = (long)dx * dx + dySq;

        if (distSq > maxRSq) continue;

        bool target;
        if (distSq <= solidRSq) {
          target = true;
        } else if (distSq >= fillRSq) {
          target = false;
        } else {
          float dist = sqrt((float)distSq);
          float t = (dist - solidR) * invFadeR;
          uint8_t bayer = pgm_read_byte(&BAYER[row & 7][col & 7]);
          target = bayer >= (uint8_t)(t * 64.0f);
        }

        setDot(row, col, px, py, target);
      }
    }

    // --- Twinkle ---
    for (uint8_t t = 0; t < 30; t++) {
      uint8_t row = random(0, ROWS);
      uint8_t col = random(0, COLS);
      int px = GX_START + (int)col * GRID;
      int py = GY_START + (int)row * GRID;

      int dx = px - CX;
      int dy = py - CY;
      long distSq = (long)dx * dx + (long)dy * dy;
      if (distSq > maxRSq) continue;

      float dist = sqrt((float)distSq);
      float norm = dist / (float)MAX_RADIUS;
      float distFromEdge = fabs(norm - fill);

      bool doTwinkle = false;
      if (distFromEdge < 0.1f) {
        doTwinkle = (random(0, 4) == 0);
      } else if (norm < solidEdge) {
        doTwinkle = (random(0, 6) == 0);
      } else if (norm > fill) {
        int chance = (int)(fill * fill * 20.0f);
        doTwinkle = (random(0, 100) < chance);
      }

      if (doTwinkle) {
        bool cur = getBit(row, col);
        tft.drawPixel(px, py, cur ? BLACK : WHITE);
        setBit(row, col, !cur);
      }
    }
  }
}