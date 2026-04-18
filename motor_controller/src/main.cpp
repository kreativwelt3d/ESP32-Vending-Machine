#include <Arduino.h>
#include <esp_task_wdt.h>

namespace {

constexpr uint32_t kDebugBaud = 115200;
constexpr uint32_t kUartBaud = 115200;

// Adjust these pins to match the wiring between the two ESP32 boards.
constexpr int kMotorUartRxPin = 15;
constexpr int kMotorUartTxPin = 16;

constexpr int kStepperMotorCount = 24;
constexpr int kDefaultSharedStepPin = 4;
constexpr uint8_t kAllowedStepPins[] = {1, 2, 3, 4};
constexpr int kEnablePins[kStepperMotorCount] = {
    5,   // Motor 1
    6,   // Motor 2
    7,   // Motor 3
    8,   // Motor 4
    9,   // Motor 5
    10,  // Motor 6
    11,  // Motor 7
    12,  // Motor 8
    13,  // Motor 9
    14,  // Motor 10
    17,  // Motor 11
    18,  // Motor 12
    1,   // Motor 13
    2,   // Motor 14
    38,  // Motor 15
    39,  // Motor 16
    40,  // Motor 17
    41,  // Motor 18
    42,  // Motor 19
    47,  // Motor 20
    48,  // Motor 21
    19,  // Motor 22
    20,  // Motor 23
    3    // Motor 24
};

constexpr int kDoorLockPin = 21;
constexpr uint8_t kDoorLockUnlockLevel = HIGH;
constexpr uint8_t kDoorLockLockLevel = LOW;
constexpr uint32_t kDefaultDoorLockPulseMs = 5000;

constexpr uint16_t kMinPulseUs = 100;
constexpr uint16_t kMaxPulseUs = 5000;
constexpr uint16_t kMaxSteps = 12000;
constexpr uint32_t kMaxDoorPulseMs = 15000;
constexpr uint32_t kTaskWatchdogTimeoutSec = 8;
constexpr uint16_t kWatchdogFeedStepInterval = 32;

HardwareSerial MotorBus(1);
String rxLine;
bool controllerBusy = false;
uint32_t controllerBusyUntilMs = 0;
bool doorPulseActive = false;
uint8_t activeStepPin = kDefaultSharedStepPin;

String taskId = "";

void serviceWatchdog() {
  esp_task_wdt_reset();
  yield();
}

bool parseUint32Token(const String& token, uint32_t& value) {
  if (token.length() == 0) {
    return false;
  }

  char* endPtr = nullptr;
  unsigned long parsed = strtoul(token.c_str(), &endPtr, 0);
  if (endPtr == nullptr || *endPtr != '\0') {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

String nextToken(String& line) {
  line.trim();
  int split = line.indexOf(' ');
  if (split < 0) {
    String token = line;
    line = "";
    return token;
  }

  String token = line.substring(0, split);
  line.remove(0, split + 1);
  line.trim();
  return token;
}

void sendReply(const String& id, const String& status, const String& payload = "") {
  String line = "@";
  line += id;
  line += " ";
  line += status;
  if (payload.length() > 0) {
    line += " ";
    line += payload;
  }
  MotorBus.println(line);
  Serial.println("UART> " + line);
}

void sendEvent(const String& event, const String& payload = "") {
  String line = "!";
  line += event;
  if (payload.length() > 0) {
    line += " ";
    line += payload;
  }
  MotorBus.println(line);
  Serial.println("UART> " + line);
}

void disableAllMotors() {
  for (int i = 0; i < kStepperMotorCount; i++) {
    digitalWrite(kEnablePins[i], HIGH);
  }
}

bool isValidMotorIndex(uint32_t index) {
  return index >= 1 && index <= static_cast<uint32_t>(kStepperMotorCount);
}

bool isValidStepPin(uint8_t pin) {
  bool allowed = false;
  const int allowedCount = sizeof(kAllowedStepPins) / sizeof(kAllowedStepPins[0]);
  for (int i = 0; i < allowedCount; i++) {
    if (kAllowedStepPins[i] == pin) {
      allowed = true;
      break;
    }
  }
  if (!allowed) {
    return false;
  }
  if (pin == static_cast<uint8_t>(kMotorUartRxPin) || pin == static_cast<uint8_t>(kMotorUartTxPin) || pin == static_cast<uint8_t>(kDoorLockPin)) {
    return false;
  }
  for (int i = 0; i < kStepperMotorCount; i++) {
    if (pin == static_cast<uint8_t>(kEnablePins[i])) {
      return false;
    }
  }
  return true;
}

void beginTask(const String& id, const String& description) {
  controllerBusy = true;
  taskId = id;
  Serial.println("TASK START " + id + " " + description);
  sendEvent("BUSY", id + " " + description);
}

void finishTaskOk(const String& payload = "") {
  controllerBusy = false;
  controllerBusyUntilMs = 0;
  String currentTask = taskId;
  taskId = "";
  sendReply(currentTask, "OK", payload);
  sendEvent("DONE", currentTask);
}

void finishTaskErr(const String& code, const String& message) {
  controllerBusy = false;
  controllerBusyUntilMs = 0;
  String currentTask = taskId;
  taskId = "";
  sendReply(currentTask, "ERR", code + " " + message);
  sendEvent("DONE", currentTask);
}

bool runMotorMask(uint32_t motorMask, uint16_t steps, uint16_t pulseUs, uint8_t stepPin, String& message) {
  if (motorMask == 0) {
    message = "EMPTY_MASK";
    return false;
  }
  if (steps == 0 || steps > kMaxSteps) {
    message = "BAD_STEPS";
    return false;
  }
  if (pulseUs < kMinPulseUs || pulseUs > kMaxPulseUs) {
    message = "BAD_PULSE";
    return false;
  }
  if (!isValidStepPin(stepPin)) {
    message = "BAD_STEP_PIN";
    return false;
  }

  disableAllMotors();
  int enabledCount = 0;
  String motors = "";

  for (int i = 0; i < kStepperMotorCount; i++) {
    if ((motorMask & (1UL << i)) == 0) {
      continue;
    }
    digitalWrite(kEnablePins[i], LOW);
    if (enabledCount > 0) {
      motors += ",";
    }
    motors += String(i + 1);
    enabledCount++;
  }

  if (enabledCount == 0) {
    message = "EMPTY_MASK";
    return false;
  }

  pinMode(stepPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  activeStepPin = stepPin;

  delay(2);
  for (uint16_t i = 0; i < steps; i++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(pulseUs);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(pulseUs);
    if ((i % kWatchdogFeedStepInterval) == 0) {
      serviceWatchdog();
    }
  }

  disableAllMotors();

  message = "motors=" + motors + " steps=" + String(steps) + " pulse_us=" + String(pulseUs) + " step_pin=" + String(stepPin);
  return true;
}

bool startDoorPulse(uint32_t durationMs) {
  if (durationMs == 0 || durationMs > kMaxDoorPulseMs) {
    return false;
  }

  digitalWrite(kDoorLockPin, kDoorLockUnlockLevel);
  controllerBusy = true;
  taskId = taskId.length() > 0 ? taskId : "0";
  controllerBusyUntilMs = millis() + durationMs;
  doorPulseActive = true;
  return true;
}

void completeDoorPulseIfDue() {
  if (!doorPulseActive) {
    return;
  }

  if (static_cast<int32_t>(millis() - controllerBusyUntilMs) < 0) {
    return;
  }

  digitalWrite(kDoorLockPin, kDoorLockLockLevel);
  doorPulseActive = false;
  finishTaskOk("lock=closed");
}

void handlePing(const String& id) {
  sendReply(id, "OK", "PONG");
}

void handleInfo(const String& id) {
  String payload = "fw=motor-esp-v2 motors=" + String(kStepperMotorCount) +
                   " step_pin=" + String(activeStepPin) +
                   " lock_pin=" + String(kDoorLockPin);
  sendReply(id, "OK", payload);
}

void handleStatus(const String& id) {
  String payload = "busy=" + String(controllerBusy ? 1 : 0) +
                   " door=" + String(doorPulseActive ? "open" : "closed");
  sendReply(id, "OK", payload);
}

void handleMotorsOff(const String& id) {
  disableAllMotors();
  sendReply(id, "OK", "motors=off");
}

void handleRun(const String& id, String args) {
  uint32_t mask = 0;
  uint32_t steps = 0;
  uint32_t pulseUs = 0;
  uint32_t stepPin = activeStepPin;
  if (!parseUint32Token(nextToken(args), mask) ||
      !parseUint32Token(nextToken(args), steps) ||
      !parseUint32Token(nextToken(args), pulseUs)) {
    sendReply(id, "ERR", "BAD_ARGS usage=RUN mask steps pulse_us [step_pin]");
    return;
  }
  String stepPinToken = nextToken(args);
  if (stepPinToken.length() > 0 && !parseUint32Token(stepPinToken, stepPin)) {
    sendReply(id, "ERR", "BAD_ARGS usage=RUN mask steps pulse_us [step_pin]");
    return;
  }

  beginTask(id, "RUN");
  String message;
  if (!runMotorMask(mask, static_cast<uint16_t>(steps), static_cast<uint16_t>(pulseUs), static_cast<uint8_t>(stepPin), message)) {
    finishTaskErr("RUN_FAIL", message);
    return;
  }
  finishTaskOk(message);
}

void handleTest(const String& id, String args) {
  uint32_t motor = 0;
  uint32_t steps = 0;
  uint32_t pulseUs = 0;
  uint32_t stepPin = activeStepPin;
  if (!parseUint32Token(nextToken(args), motor) ||
      !parseUint32Token(nextToken(args), steps) ||
      !parseUint32Token(nextToken(args), pulseUs)) {
    sendReply(id, "ERR", "BAD_ARGS usage=TEST motor steps pulse_us [step_pin]");
    return;
  }
  String stepPinToken = nextToken(args);
  if (stepPinToken.length() > 0 && !parseUint32Token(stepPinToken, stepPin)) {
    sendReply(id, "ERR", "BAD_ARGS usage=TEST motor steps pulse_us [step_pin]");
    return;
  }
  if (!isValidMotorIndex(motor)) {
    sendReply(id, "ERR", "BAD_MOTOR");
    return;
  }

  beginTask(id, "TEST");
  String message;
  uint32_t mask = 1UL << (motor - 1);
  if (!runMotorMask(mask, static_cast<uint16_t>(steps), static_cast<uint16_t>(pulseUs), static_cast<uint8_t>(stepPin), message)) {
    finishTaskErr("TEST_FAIL", message);
    return;
  }
  finishTaskOk(message);
}

void handleLock(const String& id, String args) {
  uint32_t durationMs = kDefaultDoorLockPulseMs;
  String first = nextToken(args);
  if (first.length() > 0 && !parseUint32Token(first, durationMs)) {
    sendReply(id, "ERR", "BAD_ARGS usage=LOCK [duration_ms]");
    return;
  }

  beginTask(id, "LOCK");
  if (!startDoorPulse(durationMs)) {
    finishTaskErr("LOCK_FAIL", "BAD_DURATION");
    return;
  }
  sendEvent("LOCK", "open duration_ms=" + String(durationMs));
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  Serial.println("UART< " + line);

  String frame = line;
  String first = nextToken(frame);
  if (!first.startsWith("@")) {
    MotorBus.println("@0 ERR BAD_FRAME expected=@id CMD");
    return;
  }

  String id = first.substring(1);
  String cmd = nextToken(frame);
  cmd.toUpperCase();

  if (id.length() == 0 || cmd.length() == 0) {
    sendReply(id.length() > 0 ? id : "0", "ERR", "BAD_FRAME");
    return;
  }

  if (controllerBusy) {
    sendReply(id, "ERR", "BUSY active=" + taskId);
    return;
  }

  if (cmd == "PING") {
    handlePing(id);
  } else if (cmd == "INFO") {
    handleInfo(id);
  } else if (cmd == "STATUS") {
    handleStatus(id);
  } else if (cmd == "OFF") {
    handleMotorsOff(id);
  } else if (cmd == "RUN") {
    handleRun(id, frame);
  } else if (cmd == "TEST") {
    handleTest(id, frame);
  } else if (cmd == "LOCK") {
    handleLock(id, frame);
  } else {
    sendReply(id, "ERR", "BAD_CMD");
  }
}

void pollMotorBus() {
  while (MotorBus.available() > 0) {
    char c = static_cast<char>(MotorBus.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(rxLine);
      rxLine = "";
      continue;
    }
    if (rxLine.length() < 180) {
      rxLine += c;
    } else {
      rxLine = "";
      MotorBus.println("@0 ERR FRAME_TOO_LONG");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(kDebugBaud);
  delay(200);
  Serial.println();
  Serial.println("Motor ESP boot");

  esp_task_wdt_init(kTaskWatchdogTimeoutSec, true);
  esp_task_wdt_add(nullptr);
  serviceWatchdog();

  MotorBus.begin(kUartBaud, SERIAL_8N1, kMotorUartRxPin, kMotorUartTxPin);
  Serial.println("Motor UART gestartet RX=" + String(kMotorUartRxPin) + " TX=" + String(kMotorUartTxPin));

  pinMode(activeStepPin, OUTPUT);
  digitalWrite(activeStepPin, LOW);

  for (int i = 0; i < kStepperMotorCount; i++) {
    pinMode(kEnablePins[i], OUTPUT);
    digitalWrite(kEnablePins[i], HIGH);
  }

  pinMode(kDoorLockPin, OUTPUT);
  digitalWrite(kDoorLockPin, kDoorLockLockLevel);

  sendEvent("READY", "fw=motor-esp-v2");
}

void loop() {
  serviceWatchdog();
  pollMotorBus();
  completeDoorPulseIfDue();
}








