/*******************************************************************************
 * Maker Mini Sumo - RC Only
 *
 * Connections:
 * - RC throttle/speed  <-> GPIO1
 * - RC steering        <-> GPIO2
 *
 * DIP modes (SW1, SW2, SW3):
 * - LLL / OFF-OFF-OFF : Normal RC
 * - LHL / OFF-ON-OFF  : Forward alignment (live potentiometer trim)
 * - HLH / ON-OFF-ON   : Backward alignment (live potentiometer trim)
 ******************************************************************************/

#include <EEPROM.h>
#include "CytronMakerSumo.h"

// RC receiver pins.
constexpr uint8_t RC_SPEED = GPIO1;
constexpr uint8_t RC_STEERING = GPIO2;

// MakerSumo.readDipSwitch(): SW1 is MSB and SW3 is LSB.
constexpr uint8_t MODE_FORWARD_ALIGNMENT = 2;   // LHL / OFF-ON-OFF
constexpr uint8_t MODE_BACKWARD_ALIGNMENT = 5;  // HLH / ON-OFF-ON

// Typical RC pulse range in microseconds.
constexpr int RC_NEUTRAL_US = 1500;
constexpr int RC_RANGE_US = 1000;
constexpr int RC_VALID_MIN_US = 750;
constexpr int RC_VALID_MAX_US = 2250;
constexpr unsigned long RC_TIMEOUT_US = 30000UL;
constexpr float RC_DEADBAND = 0.1f;

constexpr int SPEED_MAX = 255;

// Potentiometer center deadband and maximum motor reduction.
constexpr int POT_LEFT_CENTER = 460;
constexpr int POT_RIGHT_CENTER = 563;
constexpr int MAX_TRIM_PERCENT = 25;

// EEPROM addresses 0-3 are used by the library for edge sensor calibration.
constexpr int EEPROM_MAGIC_ADDRESS = 16;
constexpr int EEPROM_VERSION_ADDRESS = 17;
constexpr int EEPROM_FORWARD_TRIM_ADDRESS = 18;
constexpr int EEPROM_BACKWARD_TRIM_ADDRESS = 19;
constexpr uint8_t EEPROM_MAGIC = 0xA7;
constexpr uint8_t EEPROM_VERSION = 1;

constexpr unsigned long SAVE_HOLD_MS = 2000UL;
constexpr unsigned long SAVE_FLASH_MS = 1000UL;

int8_t forwardTrim = 0;
int8_t backwardTrim = 0;

bool startWasPressed = false;
bool saveCompletedForThisPress = false;
unsigned long startPressedAt = 0;
unsigned long saveFlashStartedAt = 0;

constexpr uint8_t SERIAL_BUFFER_SIZE = 40;
char serialBuffer[SERIAL_BUFFER_SIZE];
uint8_t serialBufferLength = 0;

bool readRcChannel(uint8_t pin, float &value);
int8_t readPotTrim();
void applyTrim(int &leftSpeed, int &rightSpeed, int8_t trim);
void loadAlignment();
void saveAlignment(uint8_t mode, int8_t trim);
bool handleSaveButton(uint8_t mode, int8_t liveTrim);
void updateLed(uint8_t mode, bool motorsMoving);
void processSerial();
void handleSerialCommand(char *command);
void printConfig();
void saveTrim(char direction, int8_t trim);

void setup()
{
  Serial.begin(115200);
  MakerSumo.begin();

  pinMode(RC_SPEED, INPUT_PULLUP);
  pinMode(RC_STEERING, INPUT_PULLUP);

  loadAlignment();
  MakerSumo.stop();
  digitalWrite(LED, LOW);
}

void loop()
{
  processSerial();

  uint8_t mode = MakerSumo.readDipSwitch();
  int8_t liveTrim = readPotTrim();

  // Stop while START is held and while showing the save confirmation.
  if (handleSaveButton(mode, liveTrim)) {
    MakerSumo.stop();
    updateLed(mode, false);
    return;
  }

  float speedPercent;
  float steeringPercent;

  // Failsafe: stop when either RC channel has no valid pulse.
  if (!readRcChannel(RC_SPEED, speedPercent) ||
      !readRcChannel(RC_STEERING, steeringPercent)) {
    MakerSumo.stop();
    updateLed(mode, false);
    return;
  }

  int leftSpeed = (int)((speedPercent + steeringPercent) * SPEED_MAX);
  int rightSpeed = (int)((speedPercent - steeringPercent) * SPEED_MAX);

  leftSpeed = constrain(leftSpeed, -SPEED_MAX, SPEED_MAX);
  rightSpeed = constrain(rightSpeed, -SPEED_MAX, SPEED_MAX);

  // Direction is based on throttle, before steering is mixed into the motors.
  if (speedPercent > RC_DEADBAND) {
    int8_t trim = (mode == MODE_FORWARD_ALIGNMENT) ? liveTrim : forwardTrim;
    applyTrim(leftSpeed, rightSpeed, trim);
  }
  else if (speedPercent < -RC_DEADBAND) {
    int8_t trim = (mode == MODE_BACKWARD_ALIGNMENT) ? liveTrim : backwardTrim;
    applyTrim(leftSpeed, rightSpeed, trim);
  }

  MakerSumo.setMotorSpeed(MOTOR_L, leftSpeed);
  MakerSumo.setMotorSpeed(MOTOR_R, rightSpeed);

  updateLed(mode, leftSpeed != 0 || rightSpeed != 0);
}

bool readRcChannel(uint8_t pin, float &value)
{
  unsigned long pulseWidth = pulseIn(pin, HIGH, RC_TIMEOUT_US);

  if (pulseWidth < RC_VALID_MIN_US || pulseWidth > RC_VALID_MAX_US) {
    return false;
  }

  value = (float)((long)pulseWidth - RC_NEUTRAL_US) /
          (float)(RC_RANGE_US / 2);
  value = constrain(value, -1.0f, 1.0f);

  if (value > -RC_DEADBAND && value < RC_DEADBAND) {
    value = 0.0f;
  }

  return true;
}

int8_t readPotTrim()
{
  int potentiometer = analogRead(POT);

  if (potentiometer < POT_LEFT_CENTER) {
    return (int8_t)map(potentiometer, 0, POT_LEFT_CENTER,
                       -MAX_TRIM_PERCENT, 0);
  }

  if (potentiometer > POT_RIGHT_CENTER) {
    return (int8_t)map(potentiometer, POT_RIGHT_CENTER, 1023,
                       0, MAX_TRIM_PERCENT);
  }

  return 0;
}

void applyTrim(int &leftSpeed, int &rightSpeed, int8_t trim)
{
  int scalePercent = 100 - abs(trim);

  if (trim < 0) {
    leftSpeed = leftSpeed * scalePercent / 100;
  }
  else if (trim > 0) {
    rightSpeed = rightSpeed * scalePercent / 100;
  }
}

void loadAlignment()
{
  bool valid = EEPROM.read(EEPROM_MAGIC_ADDRESS) == EEPROM_MAGIC &&
               EEPROM.read(EEPROM_VERSION_ADDRESS) == EEPROM_VERSION;

  if (!valid) {
    forwardTrim = 0;
    backwardTrim = 0;
    return;
  }

  forwardTrim = (int8_t)EEPROM.read(EEPROM_FORWARD_TRIM_ADDRESS);
  backwardTrim = (int8_t)EEPROM.read(EEPROM_BACKWARD_TRIM_ADDRESS);

  if (forwardTrim < -MAX_TRIM_PERCENT || forwardTrim > MAX_TRIM_PERCENT) {
    forwardTrim = 0;
  }

  if (backwardTrim < -MAX_TRIM_PERCENT || backwardTrim > MAX_TRIM_PERCENT) {
    backwardTrim = 0;
  }
}

void saveAlignment(uint8_t mode, int8_t trim)
{
  if (mode == MODE_FORWARD_ALIGNMENT) {
    saveTrim('F', trim);
  }
  else if (mode == MODE_BACKWARD_ALIGNMENT) {
    saveTrim('B', trim);
  }
}

void saveTrim(char direction, int8_t trim)
{
  if (direction == 'F') {
    forwardTrim = trim;
  }
  else if (direction == 'B') {
    backwardTrim = trim;
  }
  else {
    return;
  }

  // Write both trims so the untouched direction has a known default value.
  EEPROM.update(EEPROM_FORWARD_TRIM_ADDRESS, (uint8_t)forwardTrim);
  EEPROM.update(EEPROM_BACKWARD_TRIM_ADDRESS, (uint8_t)backwardTrim);
  EEPROM.update(EEPROM_VERSION_ADDRESS, EEPROM_VERSION);
  EEPROM.update(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
}

bool handleSaveButton(uint8_t mode, int8_t liveTrim)
{
  bool alignmentMode = mode == MODE_FORWARD_ALIGNMENT ||
                       mode == MODE_BACKWARD_ALIGNMENT;
  bool pressed = digitalRead(START) == LOW;
  unsigned long now = millis();

  if (!alignmentMode) {
    startWasPressed = false;
    saveCompletedForThisPress = false;
    return false;
  }

  if (pressed) {
    if (!startWasPressed) {
      startWasPressed = true;
      saveCompletedForThisPress = false;
      startPressedAt = now;
    }

    if (!saveCompletedForThisPress && now - startPressedAt >= SAVE_HOLD_MS) {
      saveAlignment(mode, liveTrim);
      saveCompletedForThisPress = true;
      saveFlashStartedAt = now;
    }

    return true;
  }

  startWasPressed = false;
  saveCompletedForThisPress = false;

  return saveFlashStartedAt != 0 && now - saveFlashStartedAt < SAVE_FLASH_MS;
}

void updateLed(uint8_t mode, bool motorsMoving)
{
  unsigned long now = millis();

  // Fast flash after a successful EEPROM save.
  if (saveFlashStartedAt != 0 && now - saveFlashStartedAt < SAVE_FLASH_MS) {
    digitalWrite(LED, ((now - saveFlashStartedAt) / 80) % 2);
    return;
  }

  if (saveFlashStartedAt != 0) {
    saveFlashStartedAt = 0;
  }

  unsigned long phase = now % 1200UL;

  if (mode == MODE_FORWARD_ALIGNMENT) {
    // One short flash, repeated.
    digitalWrite(LED, phase < 150UL ? HIGH : LOW);
  }
  else if (mode == MODE_BACKWARD_ALIGNMENT) {
    // Two short flashes, repeated.
    bool firstFlash = phase < 150UL;
    bool secondFlash = phase >= 300UL && phase < 450UL;
    digitalWrite(LED, firstFlash || secondFlash ? HIGH : LOW);
  }
  else {
    digitalWrite(LED, motorsMoving ? HIGH : LOW);
  }
}

void processSerial()
{
  while (Serial.available() > 0) {
    char incoming = (char)Serial.read();

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      serialBuffer[serialBufferLength] = '\0';

      if (serialBufferLength > 0) {
        handleSerialCommand(serialBuffer);
      }

      serialBufferLength = 0;
      continue;
    }

    if (serialBufferLength < SERIAL_BUFFER_SIZE - 1) {
      serialBuffer[serialBufferLength++] = incoming;
    }
    else {
      serialBufferLength = 0;
      Serial.println(F("ERROR COMMAND_TOO_LONG"));
    }
  }
}

void handleSerialCommand(char *command)
{
  if (strcmp(command, "HELLO") == 0) {
    Serial.println(F("OK DEVICE=MAKER_MINI_SUMO VERSION=1"));
    return;
  }

  if (strcmp(command, "GET CONFIG") == 0) {
    printConfig();
    return;
  }

  if (strncmp(command, "SAVE ", 5) == 0 &&
      (command[5] == 'F' || command[5] == 'B') &&
      command[6] == ' ') {
    char *valueText = command + 7;
    char *endText;
    long value = strtol(valueText, &endText, 10);

    if (*valueText == '\0' || *endText != '\0' ||
        value < -MAX_TRIM_PERCENT || value > MAX_TRIM_PERCENT) {
      Serial.println(F("ERROR INVALID_VALUE"));
      return;
    }

    saveTrim(command[5], (int8_t)value);
    Serial.print(F("OK SAVED "));
    Serial.print(command[5]);
    Serial.print('=');
    Serial.println(value);
    return;
  }

  Serial.println(F("ERROR UNKNOWN_COMMAND"));
}

void printConfig()
{
  Serial.print(F("CONFIG F="));
  Serial.print(forwardTrim);
  Serial.print(F(" B="));
  Serial.println(backwardTrim);
}
