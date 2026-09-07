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
#include <avr/interrupt.h>
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

// GPIO1=A2=PC2/PCINT10 and GPIO2=A3=PC3/PCINT11 on ATmega328P.
constexpr uint8_t RC_SPEED_PORT_BIT = PC2;
constexpr uint8_t RC_STEERING_PORT_BIT = PC3;

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

// Short buzzer patterns keep RC interruptions brief.
constexpr int BUZZER_POWER_NOTE_1 = NOTE_C5;
constexpr int BUZZER_POWER_NOTE_2 = NOTE_G5;
constexpr int BUZZER_FORWARD_NOTE = NOTE_G5;
constexpr int BUZZER_BACKWARD_NOTE = NOTE_C5;
constexpr int BUZZER_SAVE_NOTE_1 = NOTE_E5;
constexpr int BUZZER_SAVE_NOTE_2 = NOTE_G5;
constexpr int BUZZER_SAVE_NOTE_3 = NOTE_C6;

int8_t forwardTrim = 0;
int8_t backwardTrim = 0;

// Pulse data is captured by the Port C pin-change interrupt.
volatile uint32_t rcSpeedRiseAt = 0;
volatile uint32_t rcSteeringRiseAt = 0;
volatile uint32_t rcSpeedLastPulseAt = 0;
volatile uint32_t rcSteeringLastPulseAt = 0;
volatile uint16_t rcSpeedPulseWidth = 0;
volatile uint16_t rcSteeringPulseWidth = 0;
volatile uint8_t rcLastPortState = 0;

bool startWasPressed = false;
bool saveCompletedForThisPress = false;
unsigned long startPressedAt = 0;
unsigned long saveFlashStartedAt = 0;
uint8_t previousMode = 0xFF;

constexpr uint8_t SERIAL_BUFFER_SIZE = 40;
char serialBuffer[SERIAL_BUFFER_SIZE];
uint8_t serialBufferLength = 0;

void beginRcCapture();
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
void updateModeSound(uint8_t mode);
void playPowerOnSound();
void playSaveSound();

ISR(PCINT1_vect)
{
  uint8_t portState = PINC;
  uint8_t changedPins = portState ^ rcLastPortState;
  uint32_t now = micros();

  if (changedPins & _BV(RC_SPEED_PORT_BIT)) {
    if (portState & _BV(RC_SPEED_PORT_BIT)) {
      rcSpeedRiseAt = now;
    }
    else {
      uint32_t pulseWidth = now - rcSpeedRiseAt;
      if (pulseWidth <= UINT16_MAX) {
        rcSpeedPulseWidth = (uint16_t)pulseWidth;
        rcSpeedLastPulseAt = now;
      }
    }
  }

  if (changedPins & _BV(RC_STEERING_PORT_BIT)) {
    if (portState & _BV(RC_STEERING_PORT_BIT)) {
      rcSteeringRiseAt = now;
    }
    else {
      uint32_t pulseWidth = now - rcSteeringRiseAt;
      if (pulseWidth <= UINT16_MAX) {
        rcSteeringPulseWidth = (uint16_t)pulseWidth;
        rcSteeringLastPulseAt = now;
      }
    }
  }

  rcLastPortState = portState;
}

void setup()
{
  Serial.begin(115200);
  MakerSumo.begin();

  pinMode(RC_SPEED, INPUT_PULLUP);
  pinMode(RC_STEERING, INPUT_PULLUP);
  beginRcCapture();

  loadAlignment();
  MakerSumo.stop();
  digitalWrite(LED, LOW);
  playPowerOnSound();
}

void loop()
{
  processSerial();

  uint8_t mode = MakerSumo.readDipSwitch();
  updateModeSound(mode);
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

  // Direction is based on the rescaled throttle, before steering mixing.
  if (speedPercent > 0.0f) {
    int8_t trim = (mode == MODE_FORWARD_ALIGNMENT) ? liveTrim : forwardTrim;
    applyTrim(leftSpeed, rightSpeed, trim);
  }
  else if (speedPercent < 0.0f) {
    int8_t trim = (mode == MODE_BACKWARD_ALIGNMENT) ? liveTrim : backwardTrim;
    applyTrim(leftSpeed, rightSpeed, trim);
  }

  MakerSumo.setMotorSpeed(MOTOR_L, leftSpeed);
  MakerSumo.setMotorSpeed(MOTOR_R, rightSpeed);

  updateLed(mode, leftSpeed != 0 || rightSpeed != 0);
}

void beginRcCapture()
{
  uint32_t now = micros();

  noInterrupts();
  rcLastPortState = PINC;

  // If setup begins during a HIGH pulse, measure from this point only.
  if (rcLastPortState & _BV(RC_SPEED_PORT_BIT)) {
    rcSpeedRiseAt = now;
  }
  if (rcLastPortState & _BV(RC_STEERING_PORT_BIT)) {
    rcSteeringRiseAt = now;
  }

  PCIFR |= _BV(PCIF1);
  PCMSK1 |= _BV(PCINT10) | _BV(PCINT11);
  PCICR |= _BV(PCIE1);
  interrupts();
}

bool readRcChannel(uint8_t pin, float &value)
{
  uint16_t pulseWidth;
  uint32_t lastPulseAt;

  // A 32-bit value is not read atomically on the 8-bit ATmega328P.
  noInterrupts();
  if (pin == RC_SPEED) {
    pulseWidth = rcSpeedPulseWidth;
    lastPulseAt = rcSpeedLastPulseAt;
  }
  else {
    pulseWidth = rcSteeringPulseWidth;
    lastPulseAt = rcSteeringLastPulseAt;
  }
  interrupts();

  uint32_t now = micros();
  if (lastPulseAt == 0 || now - lastPulseAt > RC_TIMEOUT_US ||
      pulseWidth < RC_VALID_MIN_US || pulseWidth > RC_VALID_MAX_US) {
    return false;
  }

  value = (float)((long)pulseWidth - RC_NEUTRAL_US) /
          (float)(RC_RANGE_US / 2);
  value = constrain(value, -1.0f, 1.0f);

  if (value >= -RC_DEADBAND && value <= RC_DEADBAND) {
    value = 0.0f;
  }
  else if (value > 0.0f) {
    value = (value - RC_DEADBAND) / (1.0f - RC_DEADBAND);
  }
  else {
    value = (value + RC_DEADBAND) / (1.0f - RC_DEADBAND);
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
  playSaveSound();
}

void updateModeSound(uint8_t mode)
{
  if (mode == previousMode) {
    return;
  }

  previousMode = mode;

  if (mode == MODE_FORWARD_ALIGNMENT) {
    // Stop before sounding a mode change; the previous PWM may still be active.
    MakerSumo.stop();
    // One beep for forward alignment mode (LHL).
    MakerSumo.playTone(BUZZER_FORWARD_NOTE, 120);
  }
  else if (mode == MODE_BACKWARD_ALIGNMENT) {
    MakerSumo.stop();
    // Two beeps for backward alignment mode (HLH).
    MakerSumo.playTone(BUZZER_BACKWARD_NOTE, 90);
    delay(70);
    MakerSumo.playTone(BUZZER_BACKWARD_NOTE, 90);
  }
}

void playPowerOnSound()
{
  MakerSumo.playTone(BUZZER_POWER_NOTE_1, 80);
  delay(40);
  MakerSumo.playTone(BUZZER_POWER_NOTE_2, 110);
}

void playSaveSound()
{
  MakerSumo.playTone(BUZZER_SAVE_NOTE_1, 60);
  delay(30);
  MakerSumo.playTone(BUZZER_SAVE_NOTE_2, 60);
  delay(30);
  MakerSumo.playTone(BUZZER_SAVE_NOTE_3, 120);
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

    // Prevent the robot from moving while save confirmation is sounding.
    MakerSumo.stop();
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
