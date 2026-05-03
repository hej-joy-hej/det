// CO2 Halftone Display — Mega Master
//
// Serial1 (pin 18 TX) → Uno 1 (GHG)
// Serial2 (pin 16 TX) → Uno 2 (Biodiversity)
// Serial3 (pin 14 TX) → Uno 3 (Water Quality)
// SoftwareSerial (pin 12 TX) → Uno 4 (spare)
//
// NFC reader: I2C (SDA pin 20, SCL pin 21)
// Servo: pin 26
// Pump A (conventional): pin 22
// Pump B (fish): pin 24
//
// NOTE: pins 22/24 are not PWM-capable on the Mega.
// For light/heavy power levels, move pumps to pins 2–13 or 44–46
// and replace digitalWrite() with analogWrite().

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Servo.h>
#include <SoftwareSerial.h>

SoftwareSerial Serial4(-1, 12);

#define MODE_IDLE         0
#define MODE_CONVENTIONAL 1
#define MODE_FISH         2

// ---- Per-metric animation targets (0–100) ----
// Adjust idleVal, convVal, fishVal independently per metric.
// idleVal is the centre of the idle fluctuation range (walk stays within ±10 of it).
struct MetricConfig {
  float idleVal;
  float convVal;
  float fishVal;
};

const MetricConfig GHG   = { 50.0f, 100.0f,   0.0f };
const MetricConfig BIO   = { 50.0f,   0.0f, 100.0f };
const MetricConfig WATER = { 50.0f,   0.0f, 100.0f };

// ---- NFC ----
Adafruit_PN532 nfc(-1, -1, &Wire);

const uint8_t UID_LEN = 7;

const uint8_t CONV_UIDS[][7] = {
  {0x04, 0x2B, 0x9F, 0xAA, 0x96, 0x20, 0x91},
  {0x04, 0x2B, 0xA1, 0xAA, 0x96, 0x20, 0x91},
};
const uint8_t FISH_UIDS[][7] = {
  {0x04, 0x2B, 0xA2, 0xAA, 0x96, 0x20, 0x91},
  {0x04, 0x2B, 0xA0, 0xAA, 0x96, 0x20, 0x91},
};
#define CONV_COUNT 2
#define FISH_COUNT 2

// ---- Servo ----
Servo myservo;
#define SERVO_PIN  26
#define ORIGIN     90
#define LEFT       30
#define RIGHT      150

// ---- Pumps ----
#define PUMP_A_PIN  22
#define PUMP_B_PIN  24

void setPumps(uint8_t mode) {
  if (mode == MODE_CONVENTIONAL) {
    digitalWrite(PUMP_A_PIN, HIGH);
    digitalWrite(PUMP_B_PIN, LOW);
  } else if (mode == MODE_FISH) {
    digitalWrite(PUMP_A_PIN, LOW);
    digitalWrite(PUMP_B_PIN, HIGH);
  } else {
    digitalWrite(PUMP_A_PIN, LOW);
    digitalWrite(PUMP_B_PIN, LOW);
  }
}

// ---- Idle random walk (one per metric, stays within idleVal ± 10) ----
// Index: 0=GHG, 1=BIO, 2=WATER — matches Serial1/2/3 order
float idleWalk[3] = {50.0f, 50.0f, 50.0f};
float idleVel[3]  = {0.0f,  0.0f,  0.0f};

void updateIdleWalks(const MetricConfig configs[]) {
  for (int i = 0; i < 3; i++) {
    float centre = configs[i].idleVal;
    idleVel[i] += ((float)random(-100, 101) / 100.0f) * 0.05f;
    idleVel[i] *= 0.95f;
    idleVel[i] += (centre - idleWalk[i]) * 0.003f;  // gentle pull to centre
    idleWalk[i] += idleVel[i];
    if (idleWalk[i] > centre + 10.0f) { idleWalk[i] = centre + 10.0f; idleVel[i] = -fabs(idleVel[i]); }
    if (idleWalk[i] < centre - 10.0f) { idleWalk[i] = centre - 10.0f; idleVel[i] =  fabs(idleVel[i]); }
  }
}

// Value at the moment a mode started, used as the animation start point
float modeStartVal[3] = {50.0f, 50.0f, 50.0f};

// ---- State ----
uint8_t currentMode   = MODE_IDLE;
unsigned long modeStartTime = 0;
bool servoFired       = false;

#define MODE_DURATION  10000UL

unsigned long lastTapTime  = 0;
#define TAP_COOLDOWN   2000UL

unsigned long lastSendTime = 0;

// ---- Animation ----
float animatedVal(const MetricConfig &cfg, int idx) {
  if (currentMode == MODE_IDLE) return idleWalk[idx];
  float target = (currentMode == MODE_CONVENTIONAL) ? cfg.convVal : cfg.fishVal;
  float t = (float)(millis() - modeStartTime) / (float)MODE_DURATION;
  if (t > 1.0f) t = 1.0f;
  return modeStartVal[idx] + (target - modeStartVal[idx]) * t;
}

// ---- Serial packet ----
void sendValue(Stream &port, uint8_t mode, float val) {
  port.write(0xAA);
  port.write(mode);
  port.write((uint8_t*)&val, 4);
  port.write(0x55);
}

// ---- UID matching ----
bool matchUID(uint8_t *uid, uint8_t len, const uint8_t *target) {
  if (len != UID_LEN) return false;
  for (uint8_t i = 0; i < UID_LEN; i++)
    if (uid[i] != target[i]) return false;
  return true;
}

bool matchAnyUID(uint8_t *uid, uint8_t len, const uint8_t uids[][7], uint8_t count) {
  for (uint8_t i = 0; i < count; i++)
    if (matchUID(uid, len, uids[i])) return true;
  return false;
}

// ---- Setup ----
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  Serial2.begin(9600);
  Serial3.begin(9600);
  Serial4.begin(9600);

  randomSeed(analogRead(5));

  pinMode(PUMP_A_PIN, OUTPUT);
  pinMode(PUMP_B_PIN, OUTPUT);
  setPumps(MODE_IDLE);

  nfc.begin();
  delay(1000);
  if (!nfc.getFirmwareVersion()) {
    Serial.println("PN532 not found");
    while (1);
  }
  nfc.SAMConfig();
  Serial.println("Mega ready — waiting for NFC tap");
}

// ---- Main loop ----
void loop() {
  unsigned long now = millis();

  // Mode timeout: servo fires at the END of the 10s animation, then back to idle.
  // Blocking delays are intentional — displays freeze at final values during
  // the ~2s servo movement, then snap to idle values.
  if (currentMode != MODE_IDLE && now - modeStartTime >= MODE_DURATION && !servoFired) {
    servoFired = true;
    myservo.attach(SERVO_PIN);
    myservo.write(currentMode == MODE_CONVENTIONAL ? LEFT : RIGHT);
    delay(1500);
    myservo.write(ORIGIN);
    delay(500);
    myservo.detach();

    // Reset idle walks to centre so they drift back naturally from 50
    for (int i = 0; i < 3; i++) {
      idleWalk[i] = 50.0f;
      idleVel[i]  = 0.0f;
    }

    currentMode = MODE_IDLE;
    setPumps(MODE_IDLE);
    lastTapTime = millis();  // prevent NFC immediately re-triggering if card is still nearby
    Serial.println("Mode ended — back to idle");
  }

  // NFC read — only poll in idle so an active animation can't be interrupted
  if (currentMode == MODE_IDLE && now - lastTapTime >= TAP_COOLDOWN) {
    nfc.SAMConfig();
    uint8_t uid[7], uidLen;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
      lastTapTime = now;
      if (matchAnyUID(uid, uidLen, CONV_UIDS, CONV_COUNT)) {
        Serial.println("Conventional card — Conventional mode");
        // Capture current idle positions as animation start points
        for (int i = 0; i < 3; i++) modeStartVal[i] = idleWalk[i];
        currentMode   = MODE_CONVENTIONAL;
        modeStartTime = millis();
        servoFired    = false;
        setPumps(MODE_CONVENTIONAL);
      } else if (matchAnyUID(uid, uidLen, FISH_UIDS, FISH_COUNT)) {
        Serial.println("Fish card — Fish mode");
        for (int i = 0; i < 3; i++) modeStartVal[i] = idleWalk[i];
        currentMode   = MODE_FISH;
        modeStartTime = millis();
        servoFired    = false;
        setPumps(MODE_FISH);
      } else {
        Serial.print("Unknown card: ");
        for (uint8_t i = 0; i < uidLen; i++) {
          if (uid[i] < 0x10) Serial.print("0");
          Serial.print(uid[i], HEX);
          if (i < uidLen - 1) Serial.print(":");
        }
        Serial.println();
      }
    }
  }

  // Broadcast different animated values to each screen every 50ms
  if (now - lastSendTime >= 50) {
    lastSendTime = now;

    const MetricConfig configs[3] = {GHG, BIO, WATER};
    if (currentMode == MODE_IDLE) updateIdleWalks(configs);

    sendValue(Serial1, currentMode, animatedVal(GHG,   0));
    sendValue(Serial2, currentMode, animatedVal(BIO,   1));
    sendValue(Serial3, currentMode, animatedVal(WATER, 2));
    sendValue(Serial4, currentMode, animatedVal(GHG,   0));  // spare — same as GHG
  }
}
