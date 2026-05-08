// Halftone Display — Uno Slave
//
// Set SCREEN_TYPE before uploading to each Uno:
//   0 = GHG            → connect to Mega Serial1 (Mega pin 18 TX → Uno pin 0 RX)
//   1 = Biodiversity   → connect to Mega Serial2 (Mega pin 16 TX → Uno pin 0 RX)
//   2 = Water Quality  → connect to Mega Serial3 (Mega pin 14 TX → Uno pin 0 RX)
//
// ONE_SCREEN_MODE: all three screen types on one Uno.
//   Each NFC tap (idle→active transition) advances to the next screen type.
//   During idle, cycles through all three screen types every 5 seconds.
//
// Color scheme:
//   Idle mode          → dark grey dots on light grey background
//   Active, increasing → black dots on white background
//   Active, decreasing → white dots on black background
//
// IMPORTANT: Unplug the wire from D0 before uploading via USB.
//            Reconnect after upload is complete.

#define SCREEN_TYPE 0   // ← change this (0 / 1 / 2) before uploading each unit
// #define DEMO_MODE        // ← uncomment for demo mode
// #define ONE_SCREEN_MODE  // ← uncomment to cycle all three screens on one Uno

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

// ---- Colors ----
#define BLACK         0x0000
#define WHITE         0xFFFF
#define DARK_GREY     0x4208
#define LIGHT_GREY    0x8410
#define WATER_IDLE_BG 0x7BEF
#define MED_GREY      0x9CD3

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

// ---- Grid coordinates ----
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
  {"Greenhouse", "Gases",   "%", 0},   // 0 = GHG
  {"Biodiversity", "",       "%", 0},   // 1 = Biodiversity
  {"Water",     "Quality",  "%", 0},   // 2 = Water Quality
};

// ---- Global state ----
float    displayValue  = 50.0f;
float    prevDotValue  = 50.0f;
uint8_t  activeMode    = MODE_IDLE;
bool     isIncreasing  = true;

uint16_t COLOR_DOT   = WHITE;
uint16_t COLOR_BG    = BLACK;
uint16_t COLOR_LABEL = WHITE;

char          prevValueStr[16] = "";
unsigned long lastFrameTime    = 0;

// ---- ONE_SCREEN_MODE runtime screen selection ----
#ifdef ONE_SCREEN_MODE
uint8_t       currentScreenType = 0;
uint8_t       prevActiveMode    = MODE_IDLE;
unsigned long lastIdleCycleTime = 0;
#define ACTIVE_SCREEN_TYPE currentScreenType
#else
#define ACTIVE_SCREEN_TYPE SCREEN_TYPE
#endif

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
    prevDotValue = displayValue;
    prevValueStr[0] = '\0';
  }
}
#endif

// ============================================================
// Animation constants — #defines cost no RAM; always declared
// ============================================================

// Game of Life
#define GOL_CELL 6
#define GOL_W    37
#define GOL_H    37
#define GOL_BPR  ((GOL_W + 7) / 8)
#define GOL_X0   (CX - (GOL_W * GOL_CELL) / 2)
#define GOL_Y0   (CY - (GOL_H * GOL_CELL) / 2)

// Rain
#define RAIN_N    35
#define TRAIL_LEN  5

struct Drop {
  uint8_t cols[TRAIL_LEN];
  uint8_t rows[TRAIL_LEN];
  uint8_t head;
  uint8_t speed;
};

// Halftone
#define BIT_COLS ((COLS + 7) / 8)

// ============================================================
// Animation buffers
//
// ONE_SCREEN_MODE: union so all three share the same memory
//   (~750 bytes max vs ~1540 bytes if allocated separately).
//   Only one animation runs at a time so the overlap is safe.
// Otherwise: only the buffer for the compile-time SCREEN_TYPE.
// ============================================================

#ifdef ONE_SCREEN_MODE

union {
  struct { uint8_t cur[GOL_H][GOL_BPR]; uint8_t nxt[GOL_H][GOL_BPR]; } gol;
  Drop rain[RAIN_N];
  uint8_t halftone[ROWS][BIT_COLS];
} animBuf;

#define golCur    animBuf.gol.cur
#define golNxt    animBuf.gol.nxt
#define rainDrops animBuf.rain
#define dotState  animBuf.halftone

#else

#if SCREEN_TYPE == 1
uint8_t golCur[GOL_H][GOL_BPR];
uint8_t golNxt[GOL_H][GOL_BPR];
#elif SCREEN_TYPE == 2
Drop rainDrops[RAIN_N];
#else
uint8_t dotState[ROWS][BIT_COLS];
#endif

#endif  // ONE_SCREEN_MODE

// ============================================================
// Conway's Game of Life — Biodiversity (screen type 1)
// ============================================================
#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 1

uint8_t golLastMode = MODE_IDLE;

const uint8_t PROGMEM PULSAR_R[48] = {
   0, 0, 0, 0, 0, 0,  2, 2, 2, 2,  3, 3, 3, 3,  4, 4, 4, 4,  5, 5, 5, 5, 5, 5,
   7, 7, 7, 7, 7, 7,  8, 8, 8, 8,  9, 9, 9, 9, 10,10,10,10, 12,12,12,12,12,12
};
const uint8_t PROGMEM PULSAR_C[48] = {
   2, 3, 4, 8, 9,10,  0, 5, 7,12,  0, 5, 7,12,  0, 5, 7,12,  2, 3, 4, 8, 9,10,
   2, 3, 4, 8, 9,10,  0, 5, 7,12,  0, 5, 7,12,  0, 5, 7,12,  2, 3, 4, 8, 9,10
};

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

void golSeed(uint8_t mode) {
  memset(golCur, 0, sizeof(golCur));
  if (mode == MODE_FISH) {
    for (int r = 0; r < GOL_H; r++)
      for (int c = 0; c < GOL_W; c++)
        if (golInCircle(r, c) && random(100) < 45)
          golSet(golCur, r, c, true);
  } else if (mode == MODE_CONVENTIONAL) {
    for (int r = 0; r < GOL_H; r++)
      for (int c = 0; c < GOL_W; c++)
        if (golInCircle(r, c) && random(100) < 8)
          golSet(golCur, r, c, true);
  } else {
    int r0 = GOL_H / 2 - 6;
    int c0 = GOL_W / 2 - 6;
    for (uint8_t i = 0; i < 48; i++)
      golSet(golCur, r0 + pgm_read_byte(&PULSAR_R[i]),
                     c0 + pgm_read_byte(&PULSAR_C[i]), true);
  }
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

#endif  // GoL

// ============================================================
// Falling rain — Water Quality (screen type 2)
// ============================================================
#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 2

uint8_t rainActiveN    = RAIN_N;
float   rainShownValue = 50.0f;

uint8_t rainCountForMode() {
  if (activeMode == MODE_CONVENTIONAL) return 5;
  if (activeMode == MODE_FISH)         return 35;
  return 18;
}

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
  rainDrops[i].speed = random(2, 4);
}

void rainInit() {
  for (uint8_t i = 0; i < RAIN_N; i++)
    rainReset(i, true);
}

void rainStep() {
  long maxRSq = (long)MAX_RADIUS * MAX_RADIUS;
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
    uint8_t nh = (oh + 1) % TRAIL_LEN;

    int px = GX_START + rainDrops[i].cols[nh] * GRID;
    int py = GY_START + rainDrops[i].rows[nh] * GRID;
    int dx = px - CX, dy = py - CY;
    if ((long)dx*dx + (long)dy*dy <= maxRSq)
      tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, COLOR_BG);

    uint8_t nc = rainDrops[i].cols[oh] + 1;
    uint8_t nr = rainDrops[i].rows[oh] + rainDrops[i].speed;

    if (nr >= ROWS || nc >= COLS) { rainReset(i, false); continue; }

    rainDrops[i].cols[nh] = nc;
    rainDrops[i].rows[nh] = nr;
    rainDrops[i].head = nh;

    px = GX_START + nc * GRID;
    py = GY_START + nr * GRID;
    dx = px - CX; dy = py - CY;
    if ((long)dx*dx + (long)dy*dy <= maxRSq)
      tft.fillRect(px, py, DOT_SIZE, DOT_SIZE, COLOR_DOT);
  }
}

#endif  // Rain

// ============================================================
// Halftone grid — GHG (screen type 0)
// ============================================================
#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 0

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

#endif  // Halftone

// ============================================================
// Serial
// ============================================================
void processSerial() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (rxIdx == 0) {
      if (b == 0xAA) rxBuf[rxIdx++] = b;
    } else {
      rxBuf[rxIdx++] = b;
      if (rxIdx == 7) {
        if (rxBuf[6] == 0x55) {
          uint8_t mode = rxBuf[1];
          float val;
          memcpy(&val, &rxBuf[2], sizeof(float));
          if (mode <= 2 && val >= 0.0f && val <= 100.0f) {
            activeMode   = mode;
            displayValue = val;
          }
        }
        rxIdx = 0;
      }
    }
  }
}

// ---- Draw labels ----
void drawLabels() {
  const ScreenConfig &cfg = SCREENS[ACTIVE_SCREEN_TYPE];
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
  const ScreenConfig &cfg = SCREENS[ACTIVE_SCREEN_TYPE];
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
  if (COLOR_DOT == WHITE && COLOR_BG == BLACK) return;

  COLOR_DOT   = WHITE;
  COLOR_BG    = BLACK;
  COLOR_LABEL = WHITE;
  tft.fillScreen(COLOR_BG);
  drawLabels();
#if defined(ONE_SCREEN_MODE)
  if (ACTIVE_SCREEN_TYPE == 0) memset(dotState, 0, sizeof(dotState));
#elif SCREEN_TYPE == 0
  memset(dotState, 0, sizeof(dotState));
#endif
  prevValueStr[0] = '\0';
}

// ============================================================
// Per-screen frame functions (called each loop iteration)
// ============================================================

#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 1
void golFrame(unsigned long now) {
  if (now - lastFrameTime < 200) return;
  lastFrameTime = now;

#ifndef DEMO_MODE
  float delta = displayValue - prevDotValue;
  if      (delta >  0.1f) isIncreasing = true;
  else if (delta < -0.1f) isIncreasing = false;
  prevDotValue = displayValue;
#endif

  if (activeMode != golLastMode) {
    golLastMode = activeMode;
    golSeed(activeMode);
  }

  applyColorScheme();
  drawValue(activeMode == MODE_IDLE ? 50.0f : displayValue);

  uint16_t pop = golStep();
  drawGoL();
  uint8_t reseedAt = (activeMode == MODE_FISH) ? 15 : (activeMode == MODE_CONVENTIONAL) ? 3 : 0;
  if (pop <= reseedAt) golSeed(activeMode);
}
#endif

#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 2
void rainFrame(unsigned long now) {
  if (now - lastFrameTime < 35) return;
  lastFrameTime = now;

#ifndef DEMO_MODE
  float delta = displayValue - prevDotValue;
  if      (delta >  0.1f) isIncreasing = true;
  else if (delta < -0.1f) isIncreasing = false;
  prevDotValue = displayValue;
#endif

  applyColorScheme();
  rainShownValue = (activeMode == MODE_IDLE) ? 50.0f : displayValue;
  if (rainShownValue < 0.0f)   rainShownValue = 0.0f;
  if (rainShownValue > 100.0f) rainShownValue = 100.0f;
  drawValue(rainShownValue);
  rainStep();
}
#endif

#if defined(ONE_SCREEN_MODE) || SCREEN_TYPE == 0
void halftoneFrame(unsigned long now) {
  if (now - lastFrameTime < 50) return;
  lastFrameTime = now;

#ifndef DEMO_MODE
  float delta = displayValue - prevDotValue;
  if      (delta >  0.1f) isIncreasing = true;
  else if (delta < -0.1f) isIncreasing = false;
  prevDotValue = displayValue;
#endif

  float dispVal = (activeMode == MODE_IDLE) ? 50.0f : displayValue;
  applyColorScheme();
  drawValue(dispVal);

  float fill = dispVal / 100.0f;
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

// ============================================================
// ONE_SCREEN_MODE: switch to a new screen type cleanly
// Invalidates cached colors so applyColorScheme fires a full
// redraw on the first frame, then reinitializes animation state.
// ============================================================
#ifdef ONE_SCREEN_MODE
void initScreenType(uint8_t st) {
  COLOR_DOT = 0x0001;   // force applyColorScheme to redraw next frame
  COLOR_BG  = 0x0002;
  prevValueStr[0] = '\0';
  lastFrameTime = millis();

  if (st == 1) {
    golLastMode = 0xFF;  // force re-seed in golFrame
    memset(golCur, 0, sizeof(golCur));
  } else if (st == 2) {
    // Park all drops at random positions without drawing;
    // rainStep sizes rainActiveN correctly on the first call.
    rainActiveN = 0;
    for (uint8_t i = 0; i < RAIN_N; i++) {
      uint8_t sc = random(0, COLS);
      uint8_t sr = random(0, ROWS);
      for (uint8_t t = 0; t < TRAIL_LEN; t++) {
        rainDrops[i].cols[t] = sc;
        rainDrops[i].rows[t] = sr;
      }
      rainDrops[i].head  = 0;
      rainDrops[i].speed = random(2, 4);
    }
  } else {
    memset(dotState, 0, sizeof(dotState));
  }
}
#endif

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(5));

  uint16_t ID = tft.readID();
  if (ID == 0xD3D3) ID = 0x9481;
  tft.begin(ID);
  tft.setRotation(0);
  tft.fillScreen(COLOR_BG);
  drawLabels();

#ifdef ONE_SCREEN_MODE
  initScreenType(currentScreenType);
#else
#if SCREEN_TYPE == 1
  golLastMode = activeMode;
  golSeed(activeMode);
  drawGoL();
#elif SCREEN_TYPE == 2
  rainInit();
#else
  memset(dotState, 0, sizeof(dotState));
#endif
#endif
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  unsigned long now = millis();

#ifdef DEMO_MODE
  demoTick(now);
#else
  processSerial();
#endif

#ifdef ONE_SCREEN_MODE
  // idle→active: advance to the next screen type
  if (prevActiveMode == MODE_IDLE && activeMode != MODE_IDLE) {
    currentScreenType = (currentScreenType + 1) % 3;
    initScreenType(currentScreenType);
  }
  // active→idle: reset idle cycle timer so cycling resumes cleanly
  else if (prevActiveMode != MODE_IDLE && activeMode == MODE_IDLE) {
    lastIdleCycleTime = now;
  }
  prevActiveMode = activeMode;

  // Cycle screen type every 5 s while idle
  if (activeMode == MODE_IDLE && now - lastIdleCycleTime >= 5000UL) {
    lastIdleCycleTime = now;
    currentScreenType = (currentScreenType + 1) % 3;
    initScreenType(currentScreenType);
  }

  switch (currentScreenType) {
    case 1:  golFrame(now);       break;
    case 2:  rainFrame(now);      break;
    default: halftoneFrame(now);  break;
  }

#else
  // Compile-time screen type (original behavior, unchanged)
#if SCREEN_TYPE == 1
  golFrame(now);
#elif SCREEN_TYPE == 2
  rainFrame(now);
#else
  halftoneFrame(now);
#endif
#endif
}
