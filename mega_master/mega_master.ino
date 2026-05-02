// CO2 Halftone Display — Mega Master
// NFC tap switches modes, servo rotates on tap, pumps toggle per mode.
//
// Serial1 (TX pin 18) → Uno 1
// Serial2 (TX pin 16) → Uno 2
// Serial3 (TX pin 14) → Uno 3
// SoftwareSerial (TX pin 12) → Uno 4
//
// NFC reader: I2C (SDA pin 20, SCL pin 21)
// Servo: pin 9
// Pump A (conventional): pin 22
// Pump B (fish): pin 24

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Servo.h>
#include <SoftwareSerial.h>

SoftwareSerial Serial4(-1, 12);

// ---------- Modes (must match slave) ----------
#define MODE_IDLE         0
#define MODE_CONVENTIONAL 1
#define MODE_FISH         2

// ---------- NFC ----------
#define PN532_IRQ   -1
#define PN532_RESET -1
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);

const uint8_t UID_A[] = {0x04, 0x2B, 0x9C, 0xAA, 0x96, 0x20, 0x91};
const uint8_t UID_B[] = {0x04, 0x2B, 0x9D, 0xAA, 0x96, 0x20, 0x91};
const uint8_t UID_LEN = 7;

// ---------- Servo ----------
Servo myservo;
#define SERVO_PIN   26
#define ORIGIN      90
#define LEFT        30
#define RIGHT       150

unsigned long servoMoveTime = 0;
bool servoReturning = false;

// ---------- Pumps ----------
#define PUMP_A_PIN  22   // conventional pump
#define PUMP_B_PIN  24   // fish pump

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

uint8_t currentMode = MODE_IDLE;
unsigned long modeStartTime = 0;
#define MODE_DURATION 10000  // 10 seconds

// ---------- NFC cooldown ----------
unsigned long lastTapTime = 0;
#define TAP_COOLDOWN 10000

// ---------- Serial packet ----------
void sendValue(Stream &port, uint8_t mode, float val) {
  port.write(0xAA);
  port.write(mode);
  port.write((uint8_t*)&val, 4);
  port.write(0x55);
}

// ---------- Fake sensor data (replace with real sensors) ----------
float co2_value = 15.0;
float co2_velocity = 0.0;

unsigned long lastSendTime = 0;

void updateFakeCO2() {
  co2_value += co2_velocity;
  co2_velocity += ((float)random(-100, 101) / 100.0f) * 0.06f;
  co2_velocity *= 0.95f;
  co2_velocity += (15.0f - co2_value) * 0.002f;
  if (random(0, 100) < 3)
    co2_velocity += ((float)random(-100, 101) / 100.0f) * 0.4f;
  if (co2_value >= 20.0f) { co2_value = 20.0f; co2_velocity = -fabs(co2_velocity); }
  if (co2_value <= 10.0f) { co2_value = 10.0f; co2_velocity = fabs(co2_velocity); }
}

// ---------- UID matching ----------
bool matchUID(uint8_t *uid, uint8_t len, const uint8_t *target) {
  if (len != UID_LEN) return false;
  for (uint8_t i = 0; i < UID_LEN; i++) {
    if (uid[i] != target[i]) return false;
  }
  return true;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  Serial2.begin(9600);
  Serial3.begin(9600);
  Serial4.begin(9600);

  randomSeed(analogRead(5));

  // Pumps
  pinMode(PUMP_A_PIN, OUTPUT);
  pinMode(PUMP_B_PIN, OUTPUT);
  setPumps(MODE_IDLE);

  // NFC
  nfc.begin();
  delay(1000);
  uint32_t versiondata = nfc.getFirmwareVersion();

  if (!versiondata) {
    Serial.println("Didn't find PN532");
    while (1);
  }
  nfc.SAMConfig();
  Serial.println("Mega master started — waiting for NFC tap");
  Serial.println("MODE:IDLE");
}

// ---------- Main loop ----------
void loop() {
  unsigned long now = millis();
  

  // --- Non-blocking servo return ---
  if (servoReturning && now - servoMoveTime >= 1500) {
    myservo.write(ORIGIN);
    delay(500);
    myservo.detach();
    servoReturning = false;
  }

  // --- Mode timeout → back to idle ---
  if (currentMode != MODE_IDLE && now - modeStartTime >= MODE_DURATION) {
    currentMode = MODE_IDLE;
    setPumps(MODE_IDLE);
    Serial.println("Timeout — back to idle");
    Serial.println("MODE:IDLE");
  }

  // --- NFC read with cooldown ---
  if (now - lastTapTime >= TAP_COOLDOWN) {
    //Serial.println("NFC: SAMConfig...");
    nfc.SAMConfig();
    //Serial.println("NFC: reading...");
    uint8_t uid[7];
    uint8_t uidLength;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500)) {
      lastTapTime = now;
      if (matchUID(uid, uidLength, UID_A)) {
        Serial.print("Card A detected at ");
        Serial.println(now);
        if (currentMode != MODE_CONVENTIONAL) {
          Serial.println("Card A — Conventional — Left");
          currentMode = MODE_CONVENTIONAL;
          modeStartTime = now;
          setPumps(MODE_CONVENTIONAL);
          Serial.println("MODE:CONVENTIONAL");
          myservo.attach(SERVO_PIN);
          myservo.write(LEFT);
          servoMoveTime = now;
          servoReturning = true;
        }
      } else if (matchUID(uid, uidLength, UID_B)) {
        Serial.print("Card B detected at ");
        Serial.println(now);
        if (currentMode != MODE_FISH) {
          Serial.println("Card B — Fish — Right");
          currentMode = MODE_FISH;
          modeStartTime = now;
          setPumps(MODE_FISH);
          Serial.println("MODE:FISH");
          myservo.attach(SERVO_PIN);
          myservo.write(RIGHT);
          servoMoveTime = now;
          servoReturning = true;
        }
      } else {
        Serial.println("Unknown card");
        for (uint8_t i = 0; i < uidLength; i++) {
          if (uid[i] < 0x10) Serial.print("0");
          Serial.print(uid[i], HEX);
          if (i < uidLength - 1) Serial.print(":");
        }
        Serial.println();
      }
    }
  }

  // --- Send to all screens ---
  if (now - lastSendTime >= 50) {
    lastSendTime = now;

    updateFakeCO2();

    sendValue(Serial1, currentMode, co2_value);
    sendValue(Serial2, currentMode, co2_value);
    sendValue(Serial3, currentMode, co2_value);
    sendValue(Serial4, currentMode, co2_value);
  }
}   sendValue(Serial2, currentMode, co2_value);
    sendValue(Serial3, currentMode, co2_value);
    sendValue(Serial4, currentMode, co2_value);
  }
}