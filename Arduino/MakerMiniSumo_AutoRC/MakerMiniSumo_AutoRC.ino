// Maker Mini Sumo AutoRC 1.0.0. DIP 0-6 Auto, 7 RC. START/IR share D2.
#include <EEPROM.h>
#include <avr/interrupt.h>
#include "CytronMakerSumo.h"
#include <stdlib.h>
#include <string.h>
// RC receiver pins.
constexpr uint8_t RC_SPEED = GPIO1;
constexpr uint8_t RC_STEERING = GPIO2;

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

// Pulse data is captured by the Port C pin-change interrupt.
volatile uint32_t rcSpeedRiseAt = 0;
volatile uint32_t rcSteeringRiseAt = 0;
volatile uint32_t rcSpeedLastPulseAt = 0;
volatile uint32_t rcSteeringLastPulseAt = 0;
volatile uint16_t rcSpeedPulseWidth = 0;
volatile uint16_t rcSteeringPulseWidth = 0;
volatile uint8_t rcLastPortState = 0;


void beginRcCapture();
bool readRcChannel(uint8_t pin, float &value);
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


// All durations are milliseconds; speeds are signed percentages.
enum AutoField { SEARCH_L, SEARCH_R, ATTACK_INITIAL, ATTACK_MAX, ATTACK_MS,
  BACK_SPEED, BACK_MS, TURN_SPEED, TURN_MS, PAUSE_MS, EDGE_PERCENT, AUTO_FIELDS };
enum RunState { IDENTIFY, WAIT_START, COUNTDOWN, OPENING, DEF_WAIT, DEF_MOVE,
  FIGHT, BACK_REVERSE, BACK_TURN, BACK_PAUSE, STOPPED };
struct Settings {
  int16_t behaviour[AUTO_FIELDS];
  int16_t defense[5]; // repetitions, wait ms, left %, right %, move ms
  int16_t steps[7][20]; // five rows: enabled, left, right, duration
};
static_assert(sizeof(Settings) == 312, "EEPROM schema size changed; bump its version");
Settings config;
constexpr int CONFIG_ADDRESS = 32;
constexpr uint8_t CONFIG_VERSION = 1;
constexpr uint32_t BUTTON_COUNTDOWN_MS = 5000;
constexpr uint32_t INPUT_STABLE_MS = 50;
constexpr uint16_t MAX_DURATION_MS = 10000;
RunState state = IDENTIFY;
uint32_t stateAt = 0, inputAt = 0, attackAt = 0;
bool initialHigh = true, buttonStart = true, configSession = false, attacking = false;
uint8_t selectedMode = 0, stepIndex = 0, defenseCount = 0;
bool turnRight = true;
int edgeLeftThreshold = 0, edgeRightThreshold = 0;
int8_t forwardTrim = 0, backwardTrim = 0;
char serialLine[192];
uint8_t serialLength = 0;
bool serialOverflow = false;

void enterState(RunState next);
void enterState(RunState next) { state = next; stateAt = millis(); }
void stopRobot() { MakerSumo.stop(); digitalWrite(LED, LOW); }
void drive(int left, int right) {
  // Apply each motor's directional trim, including counter-rotating turns.
  int8_t lt = left < 0 ? backwardTrim : forwardTrim;
  int8_t rt = right < 0 ? backwardTrim : forwardTrim;
  if (lt < 0) left = (long)left * (100 + lt) / 100;
  if (rt > 0) right = (long)right * (100 - rt) / 100;
  MakerSumo.setMotorSpeed(MOTOR_L, left);
  MakerSumo.setMotorSpeed(MOTOR_R, right);
  digitalWrite(LED, left || right);
}
void drivePercent(int left, int right) { drive(left * 255L / 100, right * 255L / 100); }
uint8_t opponents() {
  const uint8_t pins[] = {OPP_L, OPP_FL, OPP_FC, OPP_FR, OPP_R};
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 5; i++) if (!digitalRead(pins[i])) mask |= 1 << i;
  return mask;
}
bool validValues(const int16_t *v, uint8_t kind) {
  if (kind == 0) {
    for (uint8_t i = 0; i < AUTO_FIELDS; i++) {
      int lo = i < 2 ? -100 : 0;
      int hi = (i == ATTACK_MS || i == BACK_MS || i == TURN_MS || i == PAUSE_MS) ? MAX_DURATION_MS : 100;
      if (v[i] < lo || v[i] > hi) return false;
    }
    return v[EDGE_PERCENT] >= 1 && v[EDGE_PERCENT] <= 99;
  }
  if (kind == 1) return v[0] >= 0 && v[0] <= 100 && v[1] >= 1 && v[1] <= MAX_DURATION_MS &&
    v[2] >= 0 && v[2] <= 100 && v[3] >= 0 && v[3] <= 100 && v[4] >= 1 && v[4] <= MAX_DURATION_MS;
  for (uint8_t i = 0; i < 20; i += 4)
    if (v[i] < 0 || v[i] > 1 || abs(v[i+1]) > 100 || abs(v[i+2]) > 100 ||
        v[i+3] < 1 || v[i+3] > MAX_DURATION_MS) return false;
  return true;
}
uint16_t checksum() {
  uint16_t crc = 0xFFFF;
  const uint8_t *bytes = (const uint8_t *)&config;
  for (uint16_t i = 0; i < sizeof(config); i++) {
    crc ^= bytes[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
  }
  return crc;
}
void defaults() {
  memset(&config, 0, sizeof(config));
  const int16_t behaviour[] = {35,35,50,100,300,100,100,100,120,50,50};
  const int16_t defense[] = {3,2000,50,50,50};
  memcpy(config.behaviour, behaviour, sizeof(behaviour));
  memcpy(config.defense, defense, sizeof(defense));
  for (uint8_t s = 0; s < 7; s++) for (uint8_t r = 0; r < 5; r++) config.steps[s][r*4+3] = 100;
  const int16_t opening[6][12] = {
    {0,0,0,100,0,0,0,100,0,0,0,100},
    {1,100,-100,80,1,60,100,600,1,-100,100,120},
    {1,30,30,100,1,60,60,100,1,100,100,200},
    {1,100,-100,40,1,70,100,350,0,0,0,100},
    {1,-100,100,80,1,100,60,600,1,100,-100,120},
    {1,-100,100,40,1,100,70,350,0,0,0,100}
  };
  for (uint8_t s = 0; s < 6; s++) memcpy(config.steps[s == 5 ? 6 : s], opening[s], sizeof(opening[s]));
}
void saveSettings() {
  EEPROM.update(CONFIG_ADDRESS, 0); // An interrupted write is rejected on reboot.
  EEPROM.put(CONFIG_ADDRESS + 4, config);
  uint16_t crc = checksum();
  EEPROM.put(CONFIG_ADDRESS + 2, crc);
  EEPROM.update(CONFIG_ADDRESS + 1, CONFIG_VERSION);
  EEPROM.update(CONFIG_ADDRESS, 0xAC);
}
void loadSettings() {
  defaults();
  if (EEPROM.read(CONFIG_ADDRESS) == 0xAC && EEPROM.read(CONFIG_ADDRESS+1) == CONFIG_VERSION) {
    EEPROM.get(CONFIG_ADDRESS+4, config);
    uint16_t crc; EEPROM.get(CONFIG_ADDRESS+2, crc);
    bool valid = crc == checksum() && validValues(config.behaviour,0) && validValues(config.defense,1);
    for (uint8_t s = 0; s < 7; s++) valid &= validValues(config.steps[s],2);
    if (!valid) defaults();
  }
  if (EEPROM.read(16) == 0xA7 && EEPROM.read(17) == 1) {
    forwardTrim = (int8_t)EEPROM.read(18); backwardTrim = (int8_t)EEPROM.read(19);
    if (abs(forwardTrim) > 25) forwardTrim = 0;
    if (abs(backwardTrim) > 25) backwardTrim = 0;
  }
}
void startOpening() {
  edgeLeftThreshold = analogRead(EDGE_L) * (long)config.behaviour[EDGE_PERCENT] / 100;
  edgeRightThreshold = analogRead(EDGE_R) * (long)config.behaviour[EDGE_PERCENT] / 100;
  stepIndex = defenseCount = 0;
  enterState(selectedMode == 5 ? DEF_WAIT : OPENING);
}
void runRobot() {
  uint32_t now = millis();
  if (configSession) { stopRobot(); return; }
  if (selectedMode == 7) {
    float speed, steering;
    if (!readRcChannel(RC_SPEED,speed) || !readRcChannel(RC_STEERING,steering)) { stopRobot(); return; }
    int left = constrain((int)((speed+steering)*255),-255,255);
    int right = constrain((int)((speed-steering)*255),-255,255);
    // Preserve RC's throttle-based trim behaviour, including untrimmed neutral pivots.
    int8_t trim = speed > 0 ? forwardTrim : speed < 0 ? backwardTrim : 0;
    if (trim < 0) left = left * (100+trim) / 100;
    if (trim > 0) right = right * (100-trim) / 100;
    MakerSumo.setMotorSpeed(MOTOR_L,left); MakerSumo.setMotorSpeed(MOTOR_R,right);
    digitalWrite(LED,left || right); return;
  }
  bool high = digitalRead(START);
  if (state == IDENTIFY) {
    stopRobot();
    if (high != initialHigh) { initialHigh = high; inputAt = now; }
    if (now-inputAt >= INPUT_STABLE_MS) { buttonStart = initialHigh; enterState(WAIT_START); }
    return;
  }
  if (state == STOPPED) { stopRobot(); return; }
  if (state == WAIT_START) {
    stopRobot();
    bool active = buttonStart ? !high : high;
    if (!active) inputAt = now;
    else if (now-inputAt >= (buttonStart ? 25UL : 0UL)) {
      if (buttonStart) enterState(COUNTDOWN); else startOpening();
    }
    return;
  }
  // IR stop is evaluated before every motion state, including reverse and turns.
  if (!buttonStart && !high) { enterState(STOPPED); stopRobot(); return; }
  if (state == COUNTDOWN) {
    stopRobot(); digitalWrite(LED, (now-stateAt)%500 < 250);
    if (now-stateAt >= BUTTON_COUNTDOWN_MS) startOpening();
    return;
  }
  int16_t *a = config.behaviour;
  bool edgeL = analogRead(EDGE_L) < edgeLeftThreshold;
  bool edgeR = analogRead(EDGE_R) < edgeRightThreshold;
  if (state != BACK_REVERSE && state != BACK_TURN && state != BACK_PAUSE && (edgeL || edgeR)) {
    turnRight = edgeL; attacking = false; enterState(BACK_REVERSE); stateAt = now;
  }
  uint8_t opp = opponents();
  switch (state) {
    case BACK_REVERSE:
      drivePercent(-a[BACK_SPEED],-a[BACK_SPEED]);
      if (now-stateAt >= (uint16_t)a[BACK_MS]) enterState(BACK_TURN);
      break;
    case BACK_TURN:
      drivePercent(turnRight ? a[TURN_SPEED] : -a[TURN_SPEED], turnRight ? -a[TURN_SPEED] : a[TURN_SPEED]);
      if (now-stateAt >= (uint16_t)a[TURN_MS]) enterState(BACK_PAUSE);
      break;
    case BACK_PAUSE:
      stopRobot(); if (now-stateAt >= (uint16_t)a[PAUSE_MS]) enterState(FIGHT); break;
    case OPENING: {
      int16_t *rows = config.steps[selectedMode];
      while (stepIndex < 5 && !rows[stepIndex*4]) stepIndex++;
      if (stepIndex == 5) { stopRobot(); enterState(FIGHT); break; }
      int16_t *row = rows + stepIndex*4;
      drivePercent(row[1],row[2]);
      if (now-stateAt >= (uint16_t)row[3]) { stepIndex++; stateAt = now; }
      break;
    }
    case DEF_WAIT:
    case DEF_MOVE:
      if (opp || defenseCount >= config.defense[0]) { stopRobot(); enterState(FIGHT); break; }
      if (state == DEF_WAIT) {
        stopRobot(); if (now-stateAt >= (uint16_t)config.defense[1]) enterState(DEF_MOVE);
      } else {
        drivePercent(config.defense[2],config.defense[3]);
        if (now-stateAt >= (uint16_t)config.defense[4]) { stopRobot(); defenseCount++; enterState(DEF_WAIT); }
      }
      break;
    case FIGHT:
      if (!(opp & 4)) attacking = false;
      if (!opp) drivePercent(a[SEARCH_L],a[SEARCH_R]);
      else if (opp & 4) {
        if (!attacking) { attacking = true; attackAt = now; }
        int speed = now-attackAt >= (uint16_t)a[ATTACK_MS] ? a[ATTACK_MAX] : a[ATTACK_INITIAL];
        drivePercent(speed,speed);
      } else if (opp & 2) drivePercent(0,a[ATTACK_MAX]);
      else if (opp & 8) drivePercent(a[ATTACK_MAX],0);
      else if (opp & 1) drivePercent(-a[ATTACK_MAX],a[ATTACK_MAX]);
      else drivePercent(a[ATTACK_MAX],-a[ATTACK_MAX]);
      break;
    default: stopRobot(); break;
  }
}
void printValues(const int16_t *values, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) { Serial.print(' '); Serial.print(values[i]); }
  Serial.println();
}
bool parseValues(int16_t *values, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    char *token = strtok(NULL," "); if (!token) return false;
    char *end; long n = strtol(token,&end,10);
    if (*end || n < -32767 || n > 32767) return false;
    values[i] = n;
  }
  return strtok(NULL," ") == NULL;
}
void handleCommand() {
  if (!strcmp(serialLine,"HELLO")) {
    Serial.println(F("OK DEVICE=MAKER_MINI_SUMO FW=AutoRC VERSION=1.0.0 PROTOCOL=2")); return;
  }
  if (!strcmp(serialLine,"CONFIG ON")) {
    configSession = true; stopRobot(); Serial.println(F("OK CONFIG")); return;
  }
  if (!strcmp(serialLine,"GET SENSOR")) {
    Serial.print(F("SENSOR ")); Serial.print(opponents());
    Serial.print(' '); Serial.print(analogRead(EDGE_L)); Serial.print(' '); Serial.print(analogRead(EDGE_R));
    Serial.print(' '); Serial.print(digitalRead(START)); Serial.print(' '); Serial.print(MakerSumo.readDipSwitch());
    Serial.print(' '); Serial.print(configSession ? 99 : state); Serial.print(' '); Serial.println(MakerSumo.readBatteryVoltage(),2); return;
  }
  if (!strcmp(serialLine,"GET CONFIG")) {
    Serial.print(F("CONFIG F=")); Serial.print(forwardTrim); Serial.print(F(" B=")); Serial.println(backwardTrim); return;
  }
  if (!strcmp(serialLine,"GET AUTO")) { Serial.print(F("AUTO")); printValues(config.behaviour,AUTO_FIELDS); return; }
  if (!strcmp(serialLine,"GET DEF")) { Serial.print(F("DEF")); printValues(config.defense,5); return; }
  char *action = strtok(serialLine," "); char *group = strtok(NULL," ");
  if (!action || !group) { Serial.println(F("ERROR COMMAND")); return; }
  bool save = !strcmp(action,"SAVE");
  if (save && !configSession) { Serial.println(F("ERROR CONFIG_REQUIRED")); return; }
  int16_t values[20];
  if (save && (!strcmp(group,"F") || !strcmp(group,"B"))) {
    char direction = group[0];
    if (!parseValues(values,1) || abs(values[0]) > 25) { Serial.println(F("ERROR VALUE")); return; }
    if (direction == 'F') forwardTrim = values[0]; else backwardTrim = values[0];
    EEPROM.update(18,(uint8_t)forwardTrim); EEPROM.update(19,(uint8_t)backwardTrim);
    EEPROM.update(17,1); EEPROM.update(16,0xA7);
    Serial.print(F("OK SAVED ")); Serial.print(direction); Serial.print('='); Serial.println(values[0]); return;
  }
  int16_t *target = NULL; uint8_t count = 0, kind = 0; int strategy = -1;
  if (!strcmp(group,"AUTO")) { target=config.behaviour; count=AUTO_FIELDS; }
  if (!strcmp(group,"DEF")) { target=config.defense; count=5; kind=1; }
  if (!strcmp(group,"STR")) {
    char *token = strtok(NULL," ");
    if (!token || strlen(token)!=1 || token[0]<'0' || token[0]>'6' || token[0]=='5') { Serial.println(F("ERROR STRATEGY")); return; }
    strategy = token[0]-'0'; target=config.steps[strategy]; count=20; kind=2;
  }
  if (!target) { Serial.println(F("ERROR COMMAND")); return; }
  if (!strcmp(action,"GET") && strategy >= 0 && !strtok(NULL," ")) {
    Serial.print(F("STR ")); Serial.print(strategy); printValues(target,count); return;
  }
  if (!save || !parseValues(values,count) || !validValues(values,kind)) { Serial.println(F("ERROR VALUE")); return; }
  memcpy(target,values,count*sizeof(int16_t)); saveSettings();
  Serial.print(F("OK SAVED ")); Serial.print(group);
  if (strategy >= 0) { Serial.print(' '); Serial.print(strategy); }
  Serial.println();
}
void processSerial() {
  // Bound work per loop so an incoming stream cannot starve control checks.
  for (uint8_t budget = 0; budget < 24 && Serial.available(); budget++) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine[serialLength] = 0;
      if (serialOverflow) Serial.println(F("ERROR TOO_LONG"));
      else if (serialLength) handleCommand();
      serialLength = 0; serialOverflow = false; return;
    }
    if (serialLength < sizeof(serialLine)-1 && !serialOverflow) serialLine[serialLength++] = c;
    else serialOverflow = true;
  }
}
void setup() {
  Serial.begin(115200); MakerSumo.begin(); stopRobot();
  pinMode(START,INPUT_PULLUP); pinMode(RC_SPEED,INPUT_PULLUP); pinMode(RC_STEERING,INPUT_PULLUP);
  beginRcCapture(); loadSettings(); selectedMode = MakerSumo.readDipSwitch();
  initialHigh = digitalRead(START); inputAt = millis();
}
void loop() { runRobot(); processSerial(); }
