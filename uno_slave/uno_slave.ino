// Halftone Display — Uno Slave
//
// Set SCREEN_TYPE before uploading to each Uno:
//   0 = GHG            → connect to Mega Serial1 (Mega pin 18 TX → Uno pin 0 RX)
//   1 = Biodiversity   → connect to Mega Serial2 (Mega pin 16 TX → Uno pin 0 RX)
//   2 = Water Quality  → connect to Mega Serial3 (Mega pin 14 TX → Uno pin 0 RX)
//
// Color scheme:
//   Idle mode          → dark grey dots on light grey background
//   Active, increasing → black dots on white background
//   Active, decreasing → white dots on black background
//
// IMPORTANT: Unplug the wire from D0 before uploading via USB.
//            Reconnect after upload is complete.

#define SCREEN_TYPE 1   // ← change this (0 / 1 / 2) before uploading each unit
// #define DEMO_MODE      // ← uncomment for demo mode

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

// ---- Colors ----
#define BLACK      0x0000
#define WHITE      0xFFFF
#define DARK_GREY  0x4208   // dot + label color in idle / label on white bg
#define LIGHT_GREY    0x8410   // background in idle (~RGB 128,128,128)
#define WATER_IDLE_BG 0x7BEF   // water quality idle bg — true 50% grey (R15 G31 B15)
#define MED_GREY   0x9CD3   // label color on black background

// ---- Modes (must match master) ----
#define MODE_IDLE         0
#define MODE_CONVENTIONAL 1
#define MODE_FISH         2

// ---- Layout ----
#define CX         120
#define CY         125
#define MAX_RADIUS 112
#define GRID       3
#define DOT_SIZE   2

// ---- Grid coordinates (used by rain and halftone) ----
#define GX_START (CX - MAX_RADIUS)
#define GY_START (CY - MAX_RADIUS)
#define COLS     ((2 * MAX_RADIUS) / GRID + 1)
#define ROWS     ((2 * MAX_RADIUS) / GRID + 1)

// ---- Screen identity ----
struct ScreenConfig {
  const char* label1;
  const char* label2;
  const char* unit;
  uint8_t decimals;
};

const ScreenConfig SCREENS[] = {
  {"Greenhouse", "Gases",     "%", 0},   // 0 = GHG
  {"Biodiversity", "",         "%", 0},   // 1 = Biodiversity
  {"Water",      "Quality",   "%", 0},   // 2 = Water Quality
};

// ---- State ----
float    displayValue  = 50.0f;
float    prevDotValue  = 50.0f;
uint8_t  activeMode    = MODE_IDLE;
bool     isIncreasing  = true;

uint16_t COLOR_DOT   = DARK_GREY;
uint16_t COLOR_BG    = LIGHT_GREY;
uint16_t COLOR_LABEL = DARK_GREY;

char          prevValueStr[16] = "";
unsigned long lastFrameTime    = 0;

// ---- Serial receive ----
uint8_t rxBuf[7];
uint8_t rxIdx = 0;

// ---- Demo mode ----
#ifdef DEMO_MODE
struct DemoState { uint8_t mode; uint8_t value; bool increasing; };

const DemoState DEMO_STATES[] PROGMEM = {
  {MODE_IDLE,          50, true },
  {MODE_IDLE,          50, true },
  {MODE_IDLE,          50, true },
  {MODE_CONVENTIONAL,  25, false},
  {MODE_CONVENTIONAL,  50, false},
  {MODE_CONVENTIONAL,  75, false},
  {MODE_FISH,          30, true },
  {MODE_FISH,          60, true },
  {MODE_FISH,          85, true },
};

uint8_t       demoIdx      = 0;
unsigned long lastDemoTime = 0;

void demoTick(unsigned long now) {
  if (now - lastDemoTime >= 5000UL) {
    lastDemoTime = now;
    demoIdx = (demoIdx + 1) % 9;
    DemoState s;
    memcpy_P(&s, &DEMO_STATES[demoIdx], sizeof(DemoState));
    activeMode   = s.mode;
    displayValue = (float)s.value;
    isIncreasing = s.increasing;
    prevDotValue = displayValue;   // suppress direction-detection flip
    prevValueStr[0] = '\0';        // force value redraw
  }
}
#endif

// ============================================================
// Conway's Game of Life — Biodiversity screen (SCREEN_TYPE 1)
// ============================================================
#if SCREEN_TYPE == 1

#define GOL_CELL  6                        // pixels per cell (5×5 drawn + 1px gap)
#define GOL_W     37                       // cells across
#define GOL_H     37                       // cells tall
#define GOL_BPR   ((GOL_W + 7) / 8)       // bytes per row = 5
#define GOL_X0    (CX - (GOL_W * GOL_CELL) / 2)   // = 9
#define GOL_Y0    (CY - (GOL_H * GOL_CELL) / 2)   // = 14

uint8_t golCur[GOL_H][GOL_BPR];   // 185 bytes — current generation
uint8_t golNxt[GOL_H][GOL_BPR];   // 185 bytes — next generation

bool golInCircle(int r, int c) {
  int px = GOL_X0 + c * GOL_CELL + GOL_CELL / 2;
  int py = GOL_Y0 + r * GOL_CELL + GOL_CELL / 2;
  int dx = px - CX, dy = py - CY;
  return (long)dx * dx + (long)dy * dy <= (long)MAX_RADIUS * MAX_RADIUS;
}

bool golGet(uint8_t g[][GOL_BPR], int r, int c) {
  if (r < 0 || r >= GOL_H || c < 0 || c >= GOL_W) return false;
  return (g[r][c >> 3] >> (c & 7)) & 1;
}

void golSet(uint8_t g[][GOL_BPR], int r, int c, bool v) {
  uint8_t mask = 1 << (c & 7);
  if (v) g[r][c >> 3] |= mask;
  else   g[r][c >> 3] &= ~mask;
}

void golSeed(float displayVal) {
  memset(golCur, 0, sizeof(golCur));
  int density = 25 + (int)(displayVal * 0.20f);
  for (int r = 0; r < GOL_H; r++)
    for (int c = 0; c < GOL_W; c++)
      if (golInCircle(r, c) && random(100) < density)
        golSet(golCur, r, c, true);
}

uint16_t golStep() {
  memset(golNxt, 0, sizeof(golNxt));
  uint16_t pop = 0;
  for (int r = 0; r < GOL_H; r++) {
    for (int c = 0; c < GOL_W; c++) {
      if (!golInCircle(r, c)) continue;
      int n = golGet(golCur,r-1,c-1) + golGet(golCur,r-1,c) + golGet(golCur,r-1,c+1)
            + golGet(golCur,r,  c-1)                         + golGet(golCur,r,  c+1)
            + golGet(golCur,r+1,c-1) + golGet(golCur,r+1,c) + golGet(golCur,r+1,c+1);
      bool cur  = golGet(golCur, r, c);
      bool next = cur ? (n == 2 || n == 3) : (n == 3);
      if (next) { golSet(golNxt, r, c, true); pop++; }
    }
  }
  memcpy(golCur, golNxt, sizeof(golCur));
  return pop;
}

void drawGoL() {
  for (int r = 0; r < GOL_H; r++) {
    for (int c = 0; c < GOL_W; c++) {
      if (!golInCircle(r, c)) continue;
      bool alive = golGet(golCur, r, c);
      int px = GOL_X0 + c * GOL_CELL;
      int py = GOL_Y0 + r * GOL_CELL;
      tft.fillRect(px, py, GOL_CELL - 1, GOL_CELL - 1, alive ? COLOR_DOT : COLOR_BG);
    }
  }
}

// ============================================================
// Falling rain — Water Quality screen (SCREEN_TYPE 2)
// ============================================================
#elif SCREEN_TYPE == 2

#define RAIN_N     35   // maximum simultaneous drops
#define TRAIL_LEN   5   // cells per streak (ring buffer)

// Each drop is a ring buffer of TRAIL_LEN positions.
// head = index of the newest (front) cell.
// (head+1)%TRAIL_LEN = oldest (tail) cell — erased each step.
struct Drop {
  uint8_t cols[TRAIL_LEN];
  uint8_t rows[TRAIL_LEN];
  uint8_t head;
  uint8_t speed;  // rows per step (2 or 3)
};

Drop    rainDrops[RAIN_N];
uint8_t rainActiveN   = RAIN_N;
float   rainShownValue = 50.0f;

uint8_t rainCountForMode() {
  if (activeMode == MODE_CONVENTIONAL) return 5;
  if (activeMode == MODE_FISH)         return 35;
  return 18;  // idle — average
}

// Erase all trail cells then park the drop at a new start position.
void rainReset(uint8_t i, bool stagger) {
  long maxRSq = (long)MAX_RADIUS * MAX_RADIUS;
  for (uint8_t t = 0; t < TRAIL_LEN; t++) {
    int px = GX_START + rainDrops[i].cols[t] * GRID;
    int py = GY_START + rainDrops[i].rows[t] * GRID;
    int dx = px - CX, dy = py - CY;
    if ((long)dx*dx + (long)dy*dy <= maxRSq)
      tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, COLOR_BG);
  }
  uint8_t sc = random(0, COLS);
  uint8_t sr = stagger ? random(0, ROWS) : 0;
  for (uint8_t t = 0; t < TRAIL_LEN; t++) {
    rainDrops[i].cols[t] = sc;
    rainDrops[i].rows[t] = sr;
  }
  rainDrops[i].head  = 0;
  rainDrops[i].speed = random(2, 4);  // 2 or 3 rows per step
}

void rainInit() {
  for (uint8_t i = 0; i < RAIN_N; i++)
    rainReset(i, true);
}

void rainStep() {
  long maxRSq = (long)MAX_RADIUS * MAX_RADIUS;

  // Adjust active count
  uint8_t target = rainCountForMode();
  if (target < rainActiveN)
    for (uint8_t i = target; i < rainActiveN; i++)
      rainReset(i, false);
  else if (target > rainActiveN)
    for (uint8_t i = rainActiveN; i < target; i++)
      rainReset(i, false);
  rainActiveN = target;

  for (uint8_t i = 0; i < rainActiveN; i++) {
    uint8_t oh = rainDrops[i].head;
    uint8_t nh = (oh + 1) % TRAIL_LEN;  // new head = old tail slot

    // Erase the tail (oldest cell, about to be reused)
    int px = GX_START + rainDrops[i].cols[nh] * GRID;
    int py = GY_START + rainDrops[i].rows[nh] * GRID;
    int dx = px - CX, dy = py - CY;
    if ((long)dx*dx + (long)dy*dy <= maxRSq)
      tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, COLOR_BG);

    // Advance diagonally: +1 col right, +speed rows down
    uint8_t nc = rainDrops[i].cols[oh] + 1;
    uint8_t nr = rainDrops[i].rows[oh] + rainDrops[i].speed;

    if (nr >= ROWS || nc >= COLS) { rainReset(i, false); continue; }

    rainDrops[i].cols[nh] = nc;
    rainDrops[i].rows[nh] = nr;
    rainDrops[i].head = nh;

    // Draw new head
    px = GX_START + nc * GRID;
    py = GY_START + nr * GRID;
    dx = px - CX; dy = py - CY;
    if ((long)dx*dx + (long)dy*dy <= maxRSq)
      tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, COLOR_DOT);
  }
}

// ============================================================
// Halftone grid — GHG screen (SCREEN_TYPE 0)
// ============================================================
#else

#define BIT_COLS ((COLS + 7) / 8)
uint8_t dotState[ROWS][BIT_COLS];

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
  tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, on ? COLOR_DOT : COLOR_BG);
  setBit(row, col, on);
}

#endif  // SCREEN_TYPE

void processSerial() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (rxIdx == 0) {
      if (b == 0xAA) rxBuf[rxIdx++] = b;
    } else {
      rxBuf[rxIdx++] = b;
      if (rxIdx == 7) {
        if (rxBuf[6] == 0x55) {
          activeMode = rxBuf[1];
          float val;
          memcpy(&val, &rxBuf[2], sizeof(float));
          displayValue = val;
        }
        rxIdx = 0;
      }
    }
  }
}

// ---- Draw labels ----
void drawLabels() {
  const ScreenConfig &cfg = SCREENS[SCREEN_TYPE];
  tft.fillRect(0, 250, 240, 70, COLOR_BG);
  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.setTextSize(2);
  int w1 = strlen(cfg.label1) * 12;
  int w2 = strlen(cfg.label2) * 12;
  int y1 = (w2 > 0) ? 255 : 265;
  tft.setCursor((240 - w1) / 2, y1);
  tft.print(cfg.label1);
  if (w2 > 0) {
    tft.setCursor((240 - w2) / 2, 275);
    tft.print(cfg.label2);
  }
}

// ---- Draw numeric value ----
void drawValue(float val) {
  const ScreenConfig &cfg = SCREENS[SCREEN_TYPE];
  char str[16];
  dtostrf(val, 1, cfg.decimals, str);
  strcat(str, cfg.unit);

  if (strcmp(str, prevValueStr) == 0) return;
  strcpy(prevValueStr, str);

  char padded[12];
  int len      = strlen(str);
  int total    = 8;
  int leftPad  = (total - len) / 2 + 1;
  int rightPad = total - len - leftPad;
  int idx = 0;
  for (int i = 0; i < leftPad;  i++) padded[idx++] = ' ';
  for (int i = 0; i < len;      i++) padded[idx++] = str[i];
  for (int i = 0; i < rightPad; i++) padded[idx++] = ' ';
  padded[idx] = '\0';

  tft.setTextColor(COLOR_LABEL, COLOR_BG);
  tft.setTextSize(3);
  int w = total * 18;
  tft.setCursor((240 - w) / 2, 296);
  tft.print(padded);
}

// ---- Color scheme ----
void applyColorScheme() {
  uint16_t newDot, newBg, newLabel;

  if (activeMode == MODE_IDLE) {
#if SCREEN_TYPE == 2
    newDot   = DARK_GREY;
    newBg    = WATER_IDLE_BG;
    newLabel = BLACK;
#else
    newDot   = DARK_GREY;
    newBg    = LIGHT_GREY;
    newLabel = DARK_GREY;
#endif
  } else if (isIncreasing) {
    newDot   = BLACK;
    newBg    = LIGHT_GREY;
    newLabel = BLACK;
  } else {
    newDot   = WHITE;
    newBg    = BLACK;
    newLabel = WHITE;
  }

  if (newDot == COLOR_DOT && newBg == COLOR_BG) return;

  COLOR_DOT   = newDot;
  COLOR_BG    = newBg;
  COLOR_LABEL = newLabel;
  tft.fillScreen(COLOR_BG);
  drawLabels();
#if SCREEN_TYPE == 0
  memset(dotState, 0, sizeof(dotState));
#endif
  prevValueStr[0] = '\0';
}

// ---- Setup ----
void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(5));

  uint16_t ID = tft.readID();
  if (ID == 0xD3D3) ID = 0x9481;
  tft.begin(ID);
  tft.setRotation(0);
  tft.fillScreen(COLOR_BG);
  drawLabels();

#if SCREEN_TYPE == 1
  golSeed(displayValue);
  drawGoL();
#elif SCREEN_TYPE == 2
  rainInit();
#else
  memset(dotState, 0, sizeof(dotState));
#endif
}

// ---- Main loop ----
void loop() {
  unsigned long now = millis();

#ifdef DEMO_MODE
  demoTick(now);
#else
  processSerial();
#endif

#if SCREEN_TYPE == 1
  // ---- Game of Life (200 ms) ----
  if (now - lastFrameTime >= 200) {
    lastFrameTime = now;

#ifndef DEMO_MODE
    float delta = displayValue - prevDotValue;
    if      (delta >  0.1f) isIncreasing = true;
    else if (delta < -0.1f) isIncreasing = false;
    prevDotValue = displayValue;
#endif

    applyColorScheme();
    drawValue(displayValue);

    uint16_t pop = golStep();
    drawGoL();
    if (pop < 15) golSeed(displayValue);
  }

#elif SCREEN_TYPE == 2
  // ---- Rain (35 ms) ----
  if (now - lastFrameTime >= 35) {
    lastFrameTime = now;

#ifndef DEMO_MODE
    float delta = displayValue - prevDotValue;
    if      (delta >  0.1f) isIncreasing = true;
    else if (delta < -0.1f) isIncreasing = false;
    prevDotValue = displayValue;
#endif

    applyColorScheme();
    float rainTarget = (activeMode == MODE_CONVENTIONAL) ? 0.0f : displayValue;
    if      (rainShownValue < rainTarget - 0.5f) rainShownValue += 1.5f;
    else if (rainShownValue > rainTarget + 0.5f) rainShownValue -= 1.5f;
    else                                          rainShownValue  = rainTarget;
    drawValue(rainShownValue);
    rainStep();
  }

#else
  // ---- Halftone (50 ms) ----
  if (now - lastFrameTime >= 50) {
    lastFrameTime = now;

#ifndef DEMO_MODE
    float delta = displayValue - prevDotValue;
    if      (delta >  0.1f) isIncreasing = true;
    else if (delta < -0.1f) isIncreasing = false;
    prevDotValue = displayValue;
#endif

    applyColorScheme();
    drawValue(displayValue);

    float fill = displayValue / 100.0f;
    if (fill < 0.0f) fill = 0.0f;
    if (fill > 1.0f) fill = 1.0f;

    float fadeZone  = fill * 0.45f;
    float solidEdge = fill - fadeZone;
    float solidR    = solidEdge * MAX_RADIUS;
    float fillR     = fill * MAX_RADIUS;
    long  solidRSq  = (long)(solidR * solidR);
    long  fillRSq   = (long)(fillR * fillR);
    long  maxRSq    = (long)MAX_RADIUS * MAX_RADIUS;
    float invFadeR  = 1.0f / max(fillR - solidR, 0.001f);

    for (uint8_t row = 0; row < ROWS; row++) {
      int  py   = GY_START + (int)row * GRID;
      int  dy   = py - CY;
      long dySq = (long)dy * dy;

      for (uint8_t col = 0; col < COLS; col++) {
        int  px     = GX_START + (int)col * GRID;
        int  dx     = px - CX;
        long distSq = (long)dx * dx + dySq;
        if (distSq > maxRSq) continue;

        bool target;
        if (distSq <= solidRSq) {
          target = true;
        } else if (distSq >= fillRSq) {
          target = false;
        } else {
          float   dist  = sqrt((float)distSq);
          float   t     = (dist - solidR) * invFadeR;
          uint8_t bayer = pgm_read_byte(&BAYER[row & 7][col & 7]);
          target = bayer >= (uint8_t)(t * 64.0f);
        }

        setDot(row, col, px, py, target);
      }
    }

    // ---- Twinkle ----
    for (uint8_t t = 0; t < 30; t++) {
      uint8_t row = random(0, ROWS);
      uint8_t col = random(0, COLS);
      int px = GX_START + (int)col * GRID;
      int py = GY_START + (int)row * GRID;
      int dx = px - CX;
      int dy = py - CY;
      long distSq = (long)dx * dx + (long)dy * dy;
      if (distSq > maxRSq) continue;

      float dist         = sqrt((float)distSq);
      float norm         = dist / (float)MAX_RADIUS;
      float distFromEdge = fabs(norm - fill);

      bool doTwinkle = false;
      if (distFromEdge < 0.1f)
        doTwinkle = (random(0, 4) == 0);
      else if (norm < solidEdge)
        doTwinkle = (random(0, 6) == 0);
      else if (norm > fill) {
        int chance = (int)(fill * fill * 20.0f);
        doTwinkle = (random(0, 100) < chance);
      }

      if (doTwinkle) {
        bool cur = getBit(row, col);
        tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, cur ? COLOR_BG : COLOR_DOT);
        setBit(row, col, !cur);
      }
    }
  }
#endif
}
