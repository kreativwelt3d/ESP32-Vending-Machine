#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <Keypad.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Update.h>
#include <nvs_flash.h>
#include <esp_err.h>
#include <esp_system.h>
#include "mbedtls/sha256.h"
#include <time.h>
#include <memory>

#ifndef VM_ENABLE_LCD
#define VM_ENABLE_LCD 1
#endif

#ifndef VM_ENABLE_SD
#define VM_ENABLE_SD 1
#endif

#ifndef VM_ENABLE_WIFI
#define VM_ENABLE_WIFI 1
#endif

#ifndef VM_RGB_LED_PIN
#define VM_RGB_LED_PIN 48
#endif

// =====================================================
// Projekt Beschreibung
// Dieses Projekt steuert eines ESP32 S3 für einen Verkaufsautomaten.
// Der Verkaufsautomat hat aktuell folgende funktionen
// -Service Menü mit Wifi und Admin Pin Einstellungen
// -Webserver mit einfacher login oberfläche
// -steuern eines 4x4 keypads mit den Tasten 1-9 und *#ABCD
// =====================================================

// =====================================================
// Grundeinstellungen
// =====================================================
#define FW_VERSION "1.7.0"

hd44780_I2Cexp lcd;
bool lcdAvailable = false;
Preferences prefs;
WebServer server(80);

// =====================================================
// Keypad
// =====================================================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '4', '7', '*'},
  {'2', '5', '8', '0'},
  {'3', '6', '9', '#'},
  {'A', 'B', 'C', 'D'}
};

// ESP32-S3 Pinbelegung
byte rowPins[ROWS] = {10, 11, 12, 13};
byte colPins[COLS] = {14, 15, 16, 17};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
const int keypadKeyCount = 16;
const char keypadRawKeys[keypadKeyCount] = {
  '1', '4', '7', '*',
  '2', '5', '8', '0',
  '3', '6', '9', '#',
  'A', 'B', 'C', 'D'
};
const char keypadSetupTargets[keypadKeyCount] = {
  '1', '2', '3', 'A',
  '4', '5', '6', 'B',
  '7', '8', '9', 'C',
  '*', '0', '#', 'D'
};
char keypadMappedKeys[keypadKeyCount + 1] = "147*2580369#ABCD";
bool keypadNeedsSetup = false;

// =====================================================
// App-Modi
// =====================================================
enum AppMode {
  MODE_NORMAL,
  MODE_SERVICE_PIN,
  MODE_SERVICE_MENU,
  MODE_INFO,
  MODE_WIFI_MENU,
  MODE_LANGUAGE_MENU,
  MODE_WIFI_EDIT_SSID,
  MODE_WIFI_EDIT_PASSWORD,
  MODE_WIFI_DHCP,
  MODE_WIFI_MANUAL_IP_MENU,
  MODE_WIFI_EDIT_IP,
  MODE_WIFI_EDIT_SUBNET,
  MODE_WIFI_EDIT_GATEWAY,
  MODE_WIFI_EDIT_DNS,
  MODE_ADMIN_PIN_MENU,
  MODE_ADMIN_PIN_ENTER_OLD,
  MODE_ADMIN_PIN_ENTER_NEW,
  MODE_ADMIN_PIN_CONFIRM_NEW
};

AppMode currentMode = MODE_NORMAL;

// =====================================================
// Texteingabe Zeichensätze
// =====================================================
enum TextCharSet {
  TEXTSET_UPPER,
  TEXTSET_LOWER,
  TEXTSET_NUM,
  TEXTSET_SYM
};

enum UiLanguage {
  LANG_DE,
  LANG_EN
};

// =====================================================
// Persistente Daten
// =====================================================
String adminPin = "12345678";

String wifiSSID = "";
String wifiPassword = "";
bool wifiDhcp = true;
String wifiManualIp = "192.168.1.50";
String wifiSubnet = "255.255.255.0";
String wifiGateway = "192.168.1.1";
String wifiDns = "8.8.8.8";
String wifiNtpServer = "pool.ntp.org";
bool emailNotifyEnabled = false;
String emailProtocol = "smtps";
String emailHost = "";
uint16_t emailPort = 465;
String emailUsername = "";
String emailPassword = "";
String emailFrom = "";
String emailTo = "";
uint8_t emailLowStockThreshold = 1;
UiLanguage currentLanguage = LANG_EN;
String currentCurrency = "EUR";

// =====================================================
// Laufzeitdaten
// =====================================================
String pinInput = "";
String tempNewPin = "";

String textBuffer = "";
String originalTextBuffer = "";

TextCharSet currentTextSet = TEXTSET_UPPER;
char activeMultiTapKey = '\0';
int activeMultiTapIndex = 0;
unsigned long lastMultiTapTime = 0;
const unsigned long multiTapTimeoutMs = 900;

// WiFi Status
bool wifiConnected = false;
String wifiStatusMessage = "Not connected";
unsigned long lastWifiAttemptMs = 0;
const unsigned long wifiRetryIntervalMs = 60000;
bool wifiStackInitialized = false;
bool timeSynced = false;
bool wifiSkipThisBoot = false;
bool wifiConnectPending = false;

// SD-Karte
const int sdCardSckPin = 39;
const int sdCardMosiPin = 40;
const int sdCardMisoPin = 41;
const int sdCardCsPin = 42;
SPIClass sdCardSpi(FSPI);
bool sdCardMounted = false;
String sdCardStatusMessage = "SD Karte nicht initialisiert";
const char* cashbookCsvPath = "/kassenbuch.csv";
const char* sumupLogPath = "/sumup.log";

// Webserver / Session
String webSessionToken = "";
bool webServerStarted = false;

// Firmware Update
struct FirmwareUpdateInfo {
  String version;
  String firmwareUrl;
  String sha256;
  String md5;
  String notes;
  uint32_t size;
  bool valid;
};

const char* firmwareUpdateManifestUrl = "https://sumup.kreativwelt3d.de/updates/main_esp/manifest.json";
const char* firmwareUpdateBaseUrl = "https://sumup.kreativwelt3d.de/updates/main_esp/";
FirmwareUpdateInfo cachedUpdateInfo = {"", "", "", "", "", 0, false};
String lastUpdateMessage = "";
String lastUpdateDetail = "";
unsigned long lastUpdateCheckMs = 0;
bool firmwareUpdateInProgress = false;

// Muenzpruefer
const int coinPulsePin = 18;
const uint8_t coinMappingMaxCount = 20;
const uint8_t coinDefaultMappingCount = 20;
uint8_t coinMappingCount = coinDefaultMappingCount;
uint8_t coinMappingPulses[coinMappingMaxCount] = {
  1, 2, 3, 4, 5,
  6, 7, 8, 9, 10,
  11, 12, 13, 14, 15,
  16, 17, 18, 19, 20
};
uint16_t coinMappingValuesCents[coinMappingMaxCount] = {
  100, 200, 300, 400, 500,
  600, 700, 800, 900, 1000,
  1100, 1200, 1300, 1400, 1500,
  1600, 1700, 1800, 1900, 2000
};
bool coinSignalLow = false;
unsigned long coinSignalLowStartedMs = 0;
unsigned long coinLastAcceptedPulseMs = 0;
unsigned long coinLastPulseIntervalUs = 0;
uint8_t coinPulseCount = 0;
bool coinBurstActive = false;
const unsigned long coinPulseMinLowMs = 10;
const unsigned long coinPulseMaxLowMs = 80;
const unsigned long coinBurstTimeoutMs = 700;
uint32_t creditCents = 0;
uint32_t lastDisplayedCreditCents = 0;
uint8_t lastCoinPulseCount = 0;
uint16_t lastCoinValueCents = 0;
bool pendingCashlessSelection = false;
int pendingCashlessShaftIndex = -1;
bool pendingSumupTopupSelection = false;
String pendingSumupTopupInput = "";
bool sumupEnabled = false;
const char* defaultSumupServerUrl = "https://sumup.kreativwelt3d.de/";
String sumupServerUrl = "";
String sumupApiToken = "";
String sumupMachineId = "";
String sumupCurrency = "EUR";
uint32_t sumupPollIntervalMs = 3000;
uint32_t sumupTimeoutMs = 120000;
String sumupLastMessage = "";
bool sumupPaymentPending = false;
String sumupPendingPaymentId = "";
uint32_t sumupPendingAmountCents = 0;
int sumupPendingVendShaftIndex = -1;
unsigned long sumupPendingStartedMs = 0;
unsigned long sumupNextPollMs = 0;

// Motor slots / UART motor controller
const int stepperMotorCount = 24;
const uint16_t stepperTestDefaultSteps = 200;
const uint16_t stepperTestMaxSteps = 6400;
const uint16_t stepperTestDefaultPulseUs = 800;
const uint16_t stepperTestMinPulseUs = 100;
const uint16_t stepperTestMaxPulseUs = 5000;
const uint8_t motorControllerStepPin = 4;
String lastMotorTestMessage = "";
const unsigned long doorLockSignalMs = 5000;
const int productShaftMaxCount = stepperMotorCount;
const uint8_t productShaftSlotsPerRow = 8;
const uint8_t productShaftMaxRowCount = 6;
const uint8_t productShaftDefaultCount = 10;
const uint8_t productShaftDefaultRowSlotCount[productShaftMaxRowCount] = {8, 2, 0, 0, 0, 0};
uint8_t productShaftRowSlotCount[productShaftMaxRowCount] = {8, 2, 0, 0, 0, 0};
uint8_t productShaftCount = productShaftDefaultCount;
const uint16_t productShaftManualEjectBaseSteps = 200;
const uint8_t productShaftMicrostepDefault = 16;
const uint8_t productShaftMicrostepsSupported[] = {1, 2, 4, 8, 16, 32};
uint8_t productShaftMicrosteps = productShaftMicrostepDefault;
const uint16_t productShaftManualEjectPulseUs = 800;
const uint8_t productShaftMinCapacity = 5;
const uint8_t productShaftMaxCapacity = 9;
uint8_t productShaftPrimaryMotor[productShaftMaxCount] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t productShaftSecondaryMotor[productShaftMaxCount] = {0, 0, 0, 0, 0, 0, 0, 0, 10, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint16_t productShaftPriceCents[productShaftMaxCount] = {0};
uint8_t productShaftCapacity[productShaftMaxCount] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
uint8_t productShaftQuantity[productShaftMaxCount] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
String productShaftName[productShaftMaxCount];
bool lowStockNotificationSent[productShaftMaxCount] = {false};
String lastCoinSettingsMessage = "";
String lastShaftActionMessage = "";
String lastDoorLockMessage = "";
String lastLanguageMessage = "";
String lastEmailSettingsMessage = "";
String lastEmailSendMessage = "";
char pendingProductRow = '\0';
unsigned long pendingProductSelectionMs = 0;
unsigned long pendingSumupTopupSelectionMs = 0;
const unsigned long productSelectionTimeoutMs = 5000;

// Motor controller UART
HardwareSerial motorControllerBus(1);
const int motorControllerRxPin = 5;
const int motorControllerTxPin = 4;
const uint32_t motorControllerBaud = 115200;
String motorControllerRxBuffer = "";
String lastMotorControllerMessage = "";
bool motorControllerReady = false;
uint16_t motorControllerNextRequestId = 1;

void setupWebServer();
String formatCentsToMoney(uint32_t cents);
String formatCentsForInput(uint32_t cents);
String getCurrencyLabel();
String renderCurrencySelect();
String lang(const String& de, const String& en);
bool initSdCard();
void syncClockWithNtp();
String getCurrentTimestamp();
String escapeCsvValue(const String& value);
bool ensureCashbookCsvExists();
bool appendCashbookEntry(const String& articleName, uint32_t priceCents, const String& paymentMethod);
bool ensureSumupLogExists();
bool appendSumupLog(const String& message, const String& detail = "");
String readSumupLogTail(size_t maxBytes = 4000);
String truncateForLog(const String& value, size_t maxLen = 240);
void setSumupStatus(const String& message, const String& detail = "");
String getCsvField(const String& line, int fieldIndex);
void saveEmailSettings();
void saveSumupSettings();
void saveAllSettings();
void saveProductShaftSettings();
String encodeBase64(const String& input);
bool smtpExpectResponse(Client& client, int expectedCode);
bool smtpSendCommand(Client& client, const String& command, int expectedCode);
bool sendEmailMessage(const String& subject, const String& body);
bool sendEmailWithAttachment(const String& subject, const String& body, const char* attachmentPath, const String& attachmentName, const String& mimeType);
void updateLowStockNotificationState(int shaftIndex);
bool isEmailConfigComplete();
void saveLanguageSetting();
void saveCurrencySetting();
void setupMotorControllerBus();
void processMotorControllerBus();
void clearMotorControllerInput();
bool readMotorControllerFrame(String& line, uint32_t timeoutMs);
void handleMotorControllerEvent(const String& line);
bool transactMotorController(const String& command, String& payloadOut, uint32_t timeoutMs = 20000);
uint32_t buildMotorMask(const int* motorIndexes, int motorCount);
bool pulseDoorLock();
bool runStepperMotorTest(int motorIndex, uint16_t steps, uint16_t pulseUs);
bool runProductShaftEject(int shaftIndex, uint16_t steps, uint16_t pulseUs);
bool isSupportedProductShaftMicrostep(uint8_t microsteps);
uint16_t getConfiguredProductShaftEjectSteps();
void syncProductShaftCountFromRows();
void normalizeProductShaftLayout();
uint8_t getProductShaftActiveRowCount();
int getProductShaftRowStartIndex(int rowIndex);
int getProductShaftIndexForRowSlot(int rowIndex, int slotIndex);
int getProductShaftRowForIndex(int shaftIndex);
int getProductShaftSlotForIndex(int shaftIndex);
void resetProductShaftAt(int shaftIndex);
bool insertProductShaftAt(int shaftIndex);
bool removeProductShaftAt(int shaftIndex);
String renderWebTabs(const String& activeTab);
String jsonEscape(const String& value);
String jsonGetString(const String& json, const String& key);
bool jsonGetBool(const String& json, const String& key, bool defaultValue);
uint32_t jsonGetUInt(const String& json, const String& key, uint32_t defaultValue);
bool isSumupConfigured();
void clearCachedUpdateInfo();
void setUpdateStatus(const String& message, const String& detail = "");
String normalizeHexString(String value);
String bytesToHexString(const uint8_t* bytes, size_t length);
int compareVersionStrings(const String& a, const String& b);
String resolveUpdateFirmwareUrl(const String& firmware);
bool canStartFirmwareUpdate(String& reasonOut);
bool fetchFirmwareUpdateManifest(FirmwareUpdateInfo& info);
bool isCachedFirmwareUpdateNewer();
bool installFirmwareUpdate(const FirmwareUpdateInfo& info);
void clearPendingCashlessSelection();
void showPendingCashlessSelection();
uint32_t getPendingCashlessAmountCents();
void clearPendingSumupTopupSelection();
void showPendingSumupTopupSelection();
bool startSumupTopup(uint32_t amountCents, int vendShaftIndex = -1);
void showSumupPaymentPendingScreen();
bool cancelSumupPayment();
void pollSumupPaymentStatus();
String getProductShaftCode(int shaftIndex);
String getProductShaftName(int shaftIndex);
String formatBytes(uint64_t bytes);
String renderMotorSelect(const String& fieldName, uint8_t selectedMotor, const String& inputId);
String escapeHtml(const String& value);
void clearPendingProductSelection();
void showPendingProductSelection();
bool vendProductAtIndex(int shaftIndex, const String& paymentMethod);
bool vendProductByCode(char row, char numberKey);
bool isProductShaftInStock(int shaftIndex);
bool rejectEmptyProductShaft(int shaftIndex, const String& displayName);
void handleTestsPage();
void reconnectWifiAfterSettingsSave(const char* source);
void saveWifiSettingsWithFeedback(const char* source);
const char* wifiStatusToText(wl_status_t status);
void handleCoinsPage();
void handleWifiPage();
void handleEmailPage();
void handleSumupPage();
void handleUpdatePage();
void handleUpdateCheckPost();
void handleUpdateInstallPost();
void handleShaftsPage();
void handleCashbookPage();
void handleMotorTestPost();
void handleDoorUnlockPost();
void handleLanguageTogglePost();
void handleWifiSettingsPost();
void handleEmailSettingsPost();
void handleSumupSettingsPost();
void handleEmailTestPost();
void handleEmailCashbookPost();
void handleShaftSettingsPost();
void handleShaftEjectPost();
void handleShaftMicrostepsPost();
void handleShaftAddPost();
void handleShaftAddRowPost();
void handleShaftRemovePost();
void handleShaftRemoveRowPost();
const char* getResetReasonText(esp_reset_reason_t reason);
void logBootStep(const char* step);
String getPrefStringOrDefault(const char* key, const String& defaultValue);
uint32_t getPrefUIntOrDefault(const char* key, uint32_t defaultValue);
uint8_t getPrefUCharOrDefault(const char* key, uint8_t defaultValue);
bool getPrefBoolOrDefault(const char* key, bool defaultValue);
String makeMaskString(size_t length);
void resetKeypadMappingToDefaults();
int findKeypadRawIndex(char rawKey);
char translateDetectedKey(char rawKey);
String getKeypadMappingAsString();
bool saveKeypadMapping();
void runInitialKeypadSetup();
void resetCoinMappingsToDefaults();
void sortCoinMappingsByPulseCount();
bool getCoinValueForPulseCount(uint8_t pulseCount, uint16_t& valueOut);
String renderCoinMappingRow(int index, uint8_t pulses, uint16_t cents);

String getServiceMenuItemLabel(int index);
String getWifiMenuItemLabel(int index);
String getManualIpMenuItemLabel(int index);
void showLanguageMenu();
void handleLanguageMenuKey(char key);

// =====================================================
// Menü-Indizes
// =====================================================
int serviceMenuIndex = 0;
const int serviceMenuCount = 6;

int wifiMenuIndex = 0;
const int wifiMenuCount = 4;

int manualIpMenuIndex = 0;
const int manualIpMenuCount = 4;

// =====================================================
// Kombi-Erkennung für Service-Menü
// =====================================================
char lastSpecialKey = '\0';
unsigned long lastSpecialKeyTime = 0;
const unsigned long comboWindowMs = 400;

void printBootBanner() {
  Serial.println();
  Serial.println("Starting Vending OS");
  Serial.println(" Version: " FW_VERSION);
  Serial.println();
}

void scanI2cBus() {
  uint8_t foundCount = 0;
  Serial.println("I2C scan start");
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("I2C device found at 0x%02X\n", address);
      foundCount++;
    }
  }
  if (foundCount == 0) {
    Serial.println("I2C scan: no devices found");
  } else {
    Serial.printf("I2C scan complete, %u device(s) found\n", foundCount);
  }
}

void setBootRgbColor(uint8_t r, uint8_t g, uint8_t b) {
#if defined(RGB_BUILTIN)
  neopixelWrite(RGB_BUILTIN, r, g, b);
#else
  neopixelWrite(VM_RGB_LED_PIN, r, g, b);
#endif
}

void blinkBootSuccessLed() {
  for (int i = 0; i < 2; i++) {
    setBootRgbColor(0, 48, 0);
    delay(140);
    setBootRgbColor(0, 0, 0);
    delay(140);
  }
}

// =====================================================
// Anzeige
// =====================================================
String lang(const String& de, const String& en) {
  return currentLanguage == LANG_DE ? de : en;
}

const char* getResetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "Unknown";
    case ESP_RST_POWERON:   return "Power-On";
    case ESP_RST_EXT:       return "External reset";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "Kernel Panic";
    case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
    case ESP_RST_TASK_WDT:  return "Task Watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unassigned";
  }
}

void logBootStep(const char* step) {
  Serial.print("[BOOT] ");
  Serial.println(step);
  Serial.flush();
  delay(1);
}

String getServiceMenuItemLabel(int index) {
  switch (index) {
    case 0: return "Info";
    case 1: return "WiFi";
    case 2: return lang("Admin PIN", "Admin PIN");
    case 3: return lang("Sprache", "Language");
    case 4: return lang("Keypad Setup", "Keypad Setup");
    case 5: return lang("Tuer oeffnen", "Open door");
  }
  return "";
}

String getWifiMenuItemLabel(int index) {
  switch (index) {
    case 0: return "SSID";
    case 1: return lang("Passwort", "Password");
    case 2: return "DHCP";
    case 3: return lang("Manuelle IP", "Manual IP");
  }
  return "";
}

String getManualIpMenuItemLabel(int index) {
  switch (index) {
    case 0: return "IP";
    case 1: return lang("Subnetz", "Subnet");
    case 2: return "Gateway";
    case 3: return lang("DNS Server", "DNS Server");
  }
  return "";
}

void lcdPrint2(const String& line1, const String& line2 = "") {
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

void showTemporaryMessage(const String& line1, const String& line2 = "", unsigned long delayMs = 1200) {
  lcdPrint2(line1, line2);
  delay(delayMs);
}

void showBootSplash() {
  if (!lcdAvailable) return;
  lcdPrint2("Vending OS", String("Version ") + FW_VERSION);
  delay(1400);
}

void showNormalScreen() {
  String line2 = lang("Guth ", "Cred ") + formatCentsToMoney(creditCents);
  lcdPrint2(lang("Automat bereit", "Machine ready"), line2);
  lastDisplayedCreditCents = creditCents;
}

void showServicePinScreen() {
  String stars = "";
  for (size_t i = 0; i < pinInput.length(); i++) {
    stars += "*";
  }
  lcdPrint2(lang("Service PIN:", "Service PIN:"), stars);
}

void showServiceMenu() {
  String line1 = ">" + getServiceMenuItemLabel(serviceMenuIndex);
  String line2 = "";
  int nextIndex = serviceMenuIndex + 1;
  if (nextIndex < serviceMenuCount) {
    line2 = " " + getServiceMenuItemLabel(nextIndex);
  }
  lcdPrint2(line1, line2);
}

void showInfoScreen() {
  String line1 = "FW: " + String(FW_VERSION);
  String line2;
  if (wifiSSID.length() == 0) {
    line2 = lang("WiFi aus", "WiFi off");
  } else if (wifiConnected) {
    line2 = WiFi.localIP().toString();
  } else {
    line2 = lang("WiFi offline", "WiFi offline");
  }
  lcdPrint2(line1, line2);
}

void showWifiMenu() {
  String line1 = ">" + getWifiMenuItemLabel(wifiMenuIndex);
  String line2 = "";
  int nextIndex = wifiMenuIndex + 1;
  if (nextIndex < wifiMenuCount) {
    line2 = " " + getWifiMenuItemLabel(nextIndex);
  }
  lcdPrint2(line1, line2);
}

void showManualIpMenu() {
  String line1 = ">" + getManualIpMenuItemLabel(manualIpMenuIndex);
  String line2 = "";
  int nextIndex = manualIpMenuIndex + 1;
  if (nextIndex < manualIpMenuCount) {
    line2 = " " + getManualIpMenuItemLabel(nextIndex);
  }
  lcdPrint2(line1, line2);
}

void showAdminPinMenu() {
  lcdPrint2(lang(">PIN aendern", ">Change PIN"), lang("C=Zurueck", "C=Back"));
}

void showLanguageMenu() {
  lcdPrint2(lang("Sprache:", "Language:"),
            currentLanguage == LANG_DE ? "Deutsch" : "English");
}

void showWifiDhcpEdit() {
  lcdPrint2("DHCP:", wifiDhcp ? lang("Ja", "Yes") : lang("Nein", "No"));
}

void showEnterOldPin() {
  String stars = "";
  for (size_t i = 0; i < pinInput.length(); i++) {
    stars += "*";
  }
  lcdPrint2(lang("Alter PIN:", "Old PIN:"), stars);
}

void showEnterNewPin() {
  String stars = "";
  for (size_t i = 0; i < pinInput.length(); i++) {
    stars += "*";
  }
  lcdPrint2(lang("Neuer PIN:", "New PIN:"), stars);
}

void showConfirmNewPin() {
  String stars = "";
  for (size_t i = 0; i < pinInput.length(); i++) {
    stars += "*";
  }
  lcdPrint2(lang("PIN bestaetigen", "Confirm PIN"), stars);
}

// =====================================================
// Preferences
// =====================================================
bool ensureNvsReady() {
  static bool nvsReady = false;
  if (nvsReady) return true;

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS: ungültig, wird neu initialisiert");
    esp_err_t eraseErr = nvs_flash_erase();
    if (eraseErr != ESP_OK) {
      Serial.printf("NVS: erase fehlgeschlagen (%s)\n", esp_err_to_name(eraseErr));
      return false;
    }
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    Serial.printf("NVS: init fehlgeschlagen (%s)\n", esp_err_to_name(err));
    return false;
  }

  nvsReady = true;
  return true;
}

String getPrefStringOrDefault(const char* key, const String& defaultValue) {
  if (!prefs.isKey(key)) return defaultValue;
  return prefs.getString(key, defaultValue);
}

uint32_t getPrefUIntOrDefault(const char* key, uint32_t defaultValue) {
  if (!prefs.isKey(key)) return defaultValue;
  return prefs.getUInt(key, defaultValue);
}

uint8_t getPrefUCharOrDefault(const char* key, uint8_t defaultValue) {
  if (!prefs.isKey(key)) return defaultValue;
  return prefs.getUChar(key, defaultValue);
}

bool getPrefBoolOrDefault(const char* key, bool defaultValue) {
  if (!prefs.isKey(key)) return defaultValue;
  return prefs.getBool(key, defaultValue);
}

String makeMaskString(size_t length) {
  String masked = "";
  for (size_t i = 0; i < length; i++) {
    masked += '*';
  }
  return masked;
}

void resetKeypadMappingToDefaults() {
  for (int i = 0; i < keypadKeyCount; i++) {
    keypadMappedKeys[i] = keypadRawKeys[i];
  }
  keypadMappedKeys[keypadKeyCount] = '\0';
}

int findKeypadRawIndex(char rawKey) {
  for (int i = 0; i < keypadKeyCount; i++) {
    if (keypadRawKeys[i] == rawKey) {
      return i;
    }
  }
  return -1;
}

char translateDetectedKey(char rawKey) {
  int index = findKeypadRawIndex(rawKey);
  if (index < 0) return rawKey;
  return keypadMappedKeys[index];
}

String getKeypadMappingAsString() {
  return String(keypadMappedKeys);
}

bool saveKeypadMapping() {
  if (!ensureNvsReady()) {
    Serial.println("NVS nicht bereit, Keypad-Zuordnung nicht gespeichert");
    return false;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open fehlgeschlagen (Keypad)");
    return false;
  }

  String mapping = getKeypadMappingAsString();
  size_t written = prefs.putString("keyMap", mapping);
  prefs.end();

  if (written != mapping.length()) {
    Serial.println("Preferences: Keypad-Zuordnung konnte nicht vollstaendig geschrieben werden");
    return false;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open fehlgeschlagen (Keypad)");
    return false;
  }

  bool verifyOk = prefs.getString("keyMap", "") == mapping;
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: Keypad-Zuordnung gespeichert und verifiziert");
  } else {
    Serial.println("Preferences: Keypad-Zuordnung Verifikation fehlgeschlagen");
  }

  return verifyOk;
}

void runInitialKeypadSetup() {
  bool rawAssigned[keypadKeyCount] = {false};

  lcdPrint2(lang("Keypad Setup", "Keypad setup"),
            lang("Starte...", "Starting..."));
  delay(900);

  for (int i = 0; i < keypadKeyCount; i++) {
    char expectedKey = keypadSetupTargets[i];

    while (true) {
      lcdPrint2(lang("Taste druecken", "Press key"),
                String("-> ") + expectedKey);

      char rawKey = keypad.getKey();
      if (!rawKey) {
        delay(20);
        continue;
      }

      int rawIndex = findKeypadRawIndex(rawKey);
      if (rawIndex < 0) {
        showTemporaryMessage(lang("Unbekannte Taste", "Unknown key"),
                             String(rawKey), 800);
        continue;
      }

      if (rawAssigned[rawIndex]) {
        showTemporaryMessage(lang("Schon belegt", "Already used"),
                             String(rawKey), 800);
        continue;
      }

      rawAssigned[rawIndex] = true;
      keypadMappedKeys[rawIndex] = expectedKey;
      Serial.printf("Keypad Setup: raw=%c -> logical=%c\n", rawKey, expectedKey);
      showTemporaryMessage(lang("Gespeichert", "Saved"),
                           String(rawKey) + " -> " + expectedKey, 700);
      delay(250);
      break;
    }
  }

  keypadMappedKeys[keypadKeyCount] = '\0';
  keypadNeedsSetup = false;

  bool saved = saveKeypadMapping();
  showTemporaryMessage(saved ? lang("Setup fertig", "Setup done")
                             : lang("Setup ohne Save", "Setup unsaved"),
                       saved ? lang("Neustart bereit", "Ready to start")
                             : lang("Prefs Fehler", "Prefs error"),
                       1400);
}

void resetCoinMappingsToDefaults() {
  coinMappingCount = coinDefaultMappingCount;
  for (int i = 0; i < coinMappingMaxCount; i++) {
    coinMappingPulses[i] = (uint8_t)(i + 1);
    coinMappingValuesCents[i] = (uint16_t)((i + 1) * 100);
  }
}

void sortCoinMappingsByPulseCount() {
  for (int i = 1; i < coinMappingCount; i++) {
    uint8_t currentPulse = coinMappingPulses[i];
    uint16_t currentValue = coinMappingValuesCents[i];
    int j = i - 1;

    while (j >= 0 && coinMappingPulses[j] > currentPulse) {
      coinMappingPulses[j + 1] = coinMappingPulses[j];
      coinMappingValuesCents[j + 1] = coinMappingValuesCents[j];
      j--;
    }

    coinMappingPulses[j + 1] = currentPulse;
    coinMappingValuesCents[j + 1] = currentValue;
  }
}

bool getCoinValueForPulseCount(uint8_t pulseCount, uint16_t& valueOut) {
  for (int i = 0; i < coinMappingCount; i++) {
    if (coinMappingPulses[i] == pulseCount) {
      valueOut = coinMappingValuesCents[i];
      return true;
    }
  }
  return false;
}

void loadSettings() {
  bool namespaceCreated = false;
  resetKeypadMappingToDefaults();
  keypadNeedsSetup = false;

  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, using defaults");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: read-only open failed");
    if (prefs.begin("vending", false)) {
      Serial.println("Preferences namespace created, using defaults");
      namespaceCreated = true;
      prefs.end();
    }
    if (!namespaceCreated) {
      return;
    }
  } else {
    adminPin     = getPrefStringOrDefault("adminPin", "12345678");
    wifiSSID     = getPrefStringOrDefault("wifiSSID", "");
    wifiPassword = getPrefStringOrDefault("wifiPass", "");
    wifiDhcp     = getPrefBoolOrDefault("wifiDhcp", true);
    wifiManualIp = getPrefStringOrDefault("wifiIP", "192.168.1.50");
    wifiSubnet   = getPrefStringOrDefault("wifiSub", "255.255.255.0");
    wifiGateway  = getPrefStringOrDefault("wifiGw", "192.168.1.1");
    wifiDns      = getPrefStringOrDefault("wifiDns", "8.8.8.8");
    wifiNtpServer = getPrefStringOrDefault("wifiNtp", "pool.ntp.org");
    emailNotifyEnabled = getPrefBoolOrDefault("mailEn", false);
    emailProtocol = getPrefStringOrDefault("mailProto", "smtps");
    emailHost = getPrefStringOrDefault("mailHost", "");
    emailPort = (uint16_t)getPrefUIntOrDefault("mailPort", 465);
    emailUsername = getPrefStringOrDefault("mailUser", "");
    emailPassword = getPrefStringOrDefault("mailPass", "");
    emailFrom = getPrefStringOrDefault("mailFrom", "");
    emailTo = getPrefStringOrDefault("mailTo", "");
    emailLowStockThreshold = getPrefUCharOrDefault("mailThres", 1);
    sumupEnabled = getPrefBoolOrDefault("sumEn", false);
    sumupServerUrl = getPrefStringOrDefault("sumUrl", "");
    sumupApiToken = getPrefStringOrDefault("sumTok", "");
    sumupMachineId = getPrefStringOrDefault("sumMach", "");
    sumupCurrency = getPrefStringOrDefault("sumCur", "EUR");
    uint32_t pollSeconds = getPrefUIntOrDefault("sumPoll", 3);
    uint32_t timeoutSeconds = getPrefUIntOrDefault("sumTout", 120);
    if (pollSeconds < 1) pollSeconds = 1;
    if (pollSeconds > 30) pollSeconds = 30;
    if (timeoutSeconds < 10) timeoutSeconds = 10;
    if (timeoutSeconds > 300) timeoutSeconds = 300;
    sumupPollIntervalMs = pollSeconds * 1000UL;
    sumupTimeoutMs = timeoutSeconds * 1000UL;
    if (emailLowStockThreshold > productShaftMaxCapacity) {
      emailLowStockThreshold = productShaftMaxCapacity;
    }
    currentLanguage = getPrefStringOrDefault("lang", "en") == "en" ? LANG_EN : LANG_DE;
    currentCurrency = getPrefStringOrDefault("currency", "EUR");
    String storedKeyMap = getPrefStringOrDefault("keyMap", "");
    if (storedKeyMap.length() == keypadKeyCount) {
      for (int i = 0; i < keypadKeyCount; i++) {
        keypadMappedKeys[i] = storedKeyMap[i];
      }
      keypadMappedKeys[keypadKeyCount] = '\0';
    } else {
      keypadNeedsSetup = true;
    }
    resetCoinMappingsToDefaults();
    if (prefs.isKey("coinCnt")) {
      uint8_t storedCount = getPrefUCharOrDefault("coinCnt", coinDefaultMappingCount);
      if (storedCount >= 1 && storedCount <= coinMappingMaxCount) {
        coinMappingCount = storedCount;
      }

      for (int i = 0; i < coinMappingCount; i++) {
        String pulseKey = "coinP" + String(i + 1);
        String valueKey = "coinV" + String(i + 1);
        uint32_t storedPulse = getPrefUIntOrDefault(pulseKey.c_str(), (uint32_t)(i + 1));
        uint32_t storedValue = getPrefUIntOrDefault(valueKey.c_str(), (uint32_t)((i + 1) * 100));

        if (storedPulse < 1 || storedPulse > 255) {
          storedPulse = (uint32_t)(i + 1);
        }
        if (storedValue > 65535) {
          storedValue = (uint32_t)((i + 1) * 100);
        }

        coinMappingPulses[i] = (uint8_t)storedPulse;
        coinMappingValuesCents[i] = (uint16_t)storedValue;
      }
      sortCoinMappingsByPulseCount();
    } else {
      for (int i = 0; i < coinMappingMaxCount; i++) {
        String legacyKey = "coin" + String(i + 1);
        if (prefs.isKey(legacyKey.c_str())) {
          coinMappingValuesCents[i] = (uint16_t)getPrefUIntOrDefault(legacyKey.c_str(), (uint32_t)((i + 1) * 100));
        }
      }
    }
    for (int i = 0; i < productShaftMaxCount; i++) {
      String keyPrimary = "shaftP" + String(i + 1);
      String keySecondary = "shaftS" + String(i + 1);
      String keyPrice = "shaftC" + String(i + 1);
      String keyCapacity = "shaftM" + String(i + 1);
      String keyQuantity = "shaftQ" + String(i + 1);
      String keyName = "shaftN" + String(i + 1);
      uint8_t defaultPrimary = (uint8_t)productShaftPrimaryMotor[i];
      uint8_t defaultSecondary = (uint8_t)productShaftSecondaryMotor[i];
      productShaftPrimaryMotor[i] = getPrefUCharOrDefault(keyPrimary.c_str(), defaultPrimary);
      productShaftSecondaryMotor[i] = getPrefUCharOrDefault(keySecondary.c_str(), defaultSecondary);
      productShaftPriceCents[i] = (uint16_t)getPrefUIntOrDefault(keyPrice.c_str(), productShaftPriceCents[i]);
      productShaftCapacity[i] = getPrefUCharOrDefault(keyCapacity.c_str(), productShaftCapacity[i]);
      if (productShaftCapacity[i] < productShaftMinCapacity || productShaftCapacity[i] > productShaftMaxCapacity) {
        productShaftCapacity[i] = productShaftMinCapacity;
      }
      productShaftQuantity[i] = getPrefUCharOrDefault(keyQuantity.c_str(), productShaftQuantity[i]);
      if (productShaftQuantity[i] > productShaftCapacity[i]) {
        productShaftQuantity[i] = productShaftCapacity[i];
      }
      productShaftName[i] = getPrefStringOrDefault(keyName.c_str(), "");
    }
    uint8_t legacyShaftCount = getPrefUCharOrDefault("shaftCnt", productShaftDefaultCount);
    if (legacyShaftCount < 1 || legacyShaftCount > productShaftMaxCount) {
      legacyShaftCount = productShaftDefaultCount;
    }
    if (prefs.isKey("shaftR1")) {
      for (int row = 0; row < productShaftMaxRowCount; row++) {
        String rowKey = "shaftR" + String(row + 1);
        productShaftRowSlotCount[row] = getPrefUCharOrDefault(rowKey.c_str(), 0);
      }
    } else {
      uint8_t remaining = legacyShaftCount;
      for (int row = 0; row < productShaftMaxRowCount; row++) {
        uint8_t rowCount = remaining > productShaftSlotsPerRow ? productShaftSlotsPerRow : remaining;
        productShaftRowSlotCount[row] = rowCount;
        remaining = remaining > rowCount ? (uint8_t)(remaining - rowCount) : 0;
      }
    }
    normalizeProductShaftLayout();
    productShaftMicrosteps = getPrefUCharOrDefault("shaftMicro", productShaftMicrostepDefault);
    if (!isSupportedProductShaftMicrostep(productShaftMicrosteps)) {
      productShaftMicrosteps = productShaftMicrostepDefault;
    }
    prefs.end();
  }

  if (namespaceCreated) {
    keypadNeedsSetup = true;
    saveAllSettings();
  }

  for (int i = 0; i < productShaftMaxCount; i++) {
    lowStockNotificationSent[i] = productShaftQuantity[i] <= emailLowStockThreshold;
  }
}

void saveAllSettings() {
  saveAdminPin();
  saveWifiSettings();
  saveLanguageSetting();
  saveCurrencySetting();
  saveCoinSettings();
  saveEmailSettings();
  saveSumupSettings();
  saveProductShaftSettings();
}

void saveAdminPin() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, PIN could not be saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (PIN)");
    return;
  }

  size_t written = prefs.putString("adminPin", adminPin);
  prefs.end();

  if (written == 0) {
    Serial.println("Preferences: adminPin was not written");
  }
}

void saveWifiSettings() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, WiFi settings not saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (WiFi)");
    return;
  }

  size_t ssidWritten = prefs.putString("wifiSSID", wifiSSID);
  size_t passWritten = prefs.putString("wifiPass", wifiPassword);
  size_t dhcpWritten = prefs.putBool("wifiDhcp", wifiDhcp);
  size_t ipWritten   = prefs.putString("wifiIP", wifiManualIp);
  size_t subWritten  = prefs.putString("wifiSub", wifiSubnet);
  size_t gwWritten   = prefs.putString("wifiGw", wifiGateway);
  size_t dnsWritten  = prefs.putString("wifiDns", wifiDns);
  size_t ntpWritten  = prefs.putString("wifiNtp", wifiNtpServer);
  prefs.end();

  bool allWritten =
    (ssidWritten == wifiSSID.length()) &&
    (passWritten == wifiPassword.length()) &&
    (dhcpWritten == sizeof(bool)) &&
    (ipWritten == wifiManualIp.length()) &&
    (subWritten == wifiSubnet.length()) &&
    (gwWritten == wifiGateway.length()) &&
    (dnsWritten == wifiDns.length()) &&
    (ntpWritten == wifiNtpServer.length());

  if (!allWritten) {
    Serial.println("Preferences: WiFi values could not be written completely");
    Serial.printf("  SSID=%u/%u PASS=%u/%u DHCP=%u/%u IP=%u/%u SUB=%u/%u GW=%u/%u DNS=%u/%u NTP=%u/%u\n",
                  (unsigned int)ssidWritten, (unsigned int)wifiSSID.length(),
                  (unsigned int)passWritten, (unsigned int)wifiPassword.length(),
                  (unsigned int)dhcpWritten, (unsigned int)sizeof(bool),
                  (unsigned int)ipWritten, (unsigned int)wifiManualIp.length(),
                  (unsigned int)subWritten, (unsigned int)wifiSubnet.length(),
                  (unsigned int)gwWritten, (unsigned int)wifiGateway.length(),
                  (unsigned int)dnsWritten, (unsigned int)wifiDns.length(),
                  (unsigned int)ntpWritten, (unsigned int)wifiNtpServer.length());
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (WiFi)");
    return;
  }

  bool verifyOk =
    prefs.getString("wifiSSID", "") == wifiSSID &&
    prefs.getString("wifiPass", "") == wifiPassword &&
    prefs.getBool("wifiDhcp", !wifiDhcp) == wifiDhcp &&
    prefs.getString("wifiIP", "") == wifiManualIp &&
    prefs.getString("wifiSub", "") == wifiSubnet &&
    prefs.getString("wifiGw", "") == wifiGateway &&
    prefs.getString("wifiDns", "") == wifiDns &&
    prefs.getString("wifiNtp", "") == wifiNtpServer;
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: WiFi settings saved and verified");
  } else {
    Serial.println("Preferences: WiFi verification failed");
  }
}

void saveCoinSettings() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, coin settings not saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (coins)");
    return;
  }

  bool allWritten = prefs.putUChar("coinCnt", coinMappingCount) == sizeof(uint8_t);
  for (int i = 0; i < coinMappingCount; i++) {
    String pulseKey = "coinP" + String(i + 1);
    String valueKey = "coinV" + String(i + 1);
    size_t pulseWritten = prefs.putUInt(pulseKey.c_str(), coinMappingPulses[i]);
    size_t valueWritten = prefs.putUInt(valueKey.c_str(), coinMappingValuesCents[i]);
    if (pulseWritten != sizeof(uint32_t) || valueWritten != sizeof(uint32_t)) {
      allWritten = false;
    }
  }
  for (int i = coinMappingCount; i < coinMappingMaxCount; i++) {
    String pulseKey = "coinP" + String(i + 1);
    String valueKey = "coinV" + String(i + 1);
    prefs.remove(pulseKey.c_str());
    prefs.remove(valueKey.c_str());
  }
  prefs.end();

  if (!allWritten) {
    Serial.println("Preferences: coin values could not be written completely");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (coins)");
    return;
  }

  bool verifyOk = prefs.getUChar("coinCnt", 0) == coinMappingCount;
  for (int i = 0; i < coinMappingCount && verifyOk; i++) {
    String pulseKey = "coinP" + String(i + 1);
    String valueKey = "coinV" + String(i + 1);
    if ((uint8_t)prefs.getUInt(pulseKey.c_str(), 0) != coinMappingPulses[i] ||
        (uint16_t)prefs.getUInt(valueKey.c_str(), 0) != coinMappingValuesCents[i]) {
      verifyOk = false;
    }
  }
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: coin values saved and verified");
  } else {
    Serial.println("Preferences: coin values verification failed");
  }
}

void saveEmailSettings() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, email settings not saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (email)");
    return;
  }

  size_t enabledWritten = prefs.putBool("mailEn", emailNotifyEnabled);
  size_t protoWritten = prefs.putString("mailProto", emailProtocol);
  size_t hostWritten = prefs.putString("mailHost", emailHost);
  size_t portWritten = prefs.putUInt("mailPort", emailPort);
  size_t userWritten = prefs.putString("mailUser", emailUsername);
  size_t passWritten = prefs.putString("mailPass", emailPassword);
  size_t fromWritten = prefs.putString("mailFrom", emailFrom);
  size_t toWritten = prefs.putString("mailTo", emailTo);
  size_t thresholdWritten = prefs.putUChar("mailThres", emailLowStockThreshold);
  prefs.end();

  bool allWritten =
    (enabledWritten == sizeof(bool)) &&
    (protoWritten == emailProtocol.length()) &&
    (hostWritten == emailHost.length()) &&
    (portWritten == sizeof(uint32_t)) &&
    (userWritten == emailUsername.length()) &&
    (passWritten == emailPassword.length()) &&
    (fromWritten == emailFrom.length()) &&
    (toWritten == emailTo.length()) &&
    (thresholdWritten == sizeof(uint8_t));

  if (!allWritten) {
    Serial.println("Preferences: email values could not be written completely");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (email)");
    return;
  }

  bool verifyOk =
    prefs.getBool("mailEn", !emailNotifyEnabled) == emailNotifyEnabled &&
    prefs.getString("mailProto", "") == emailProtocol &&
    prefs.getString("mailHost", "") == emailHost &&
    (uint16_t)prefs.getUInt("mailPort", 0) == emailPort &&
    prefs.getString("mailUser", "") == emailUsername &&
    prefs.getString("mailPass", "") == emailPassword &&
    prefs.getString("mailFrom", "") == emailFrom &&
    prefs.getString("mailTo", "") == emailTo &&
    prefs.getUChar("mailThres", 255) == emailLowStockThreshold;
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: email settings saved and verified");
  } else {
    Serial.println("Preferences: email verification failed");
  }
}

void saveSumupSettings() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, SumUp settings not saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (SumUp)");
    return;
  }

  size_t enabledWritten = prefs.putBool("sumEn", sumupEnabled);
  size_t urlWritten = prefs.putString("sumUrl", sumupServerUrl);
  size_t tokenWritten = prefs.putString("sumTok", sumupApiToken);
  size_t machineWritten = prefs.putString("sumMach", sumupMachineId);
  size_t currencyWritten = prefs.putString("sumCur", sumupCurrency);
  size_t pollWritten = prefs.putUInt("sumPoll", sumupPollIntervalMs / 1000UL);
  size_t timeoutWritten = prefs.putUInt("sumTout", sumupTimeoutMs / 1000UL);
  prefs.end();

  bool allWritten =
    (enabledWritten == sizeof(bool)) &&
    (urlWritten == sumupServerUrl.length()) &&
    (tokenWritten == sumupApiToken.length()) &&
    (machineWritten == sumupMachineId.length()) &&
    (currencyWritten == sumupCurrency.length()) &&
    (pollWritten == sizeof(uint32_t)) &&
    (timeoutWritten == sizeof(uint32_t));

  if (!allWritten) {
    Serial.println("Preferences: SumUp values could not be written completely");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (SumUp)");
    return;
  }

  bool verifyOk =
    prefs.getBool("sumEn", !sumupEnabled) == sumupEnabled &&
    prefs.getString("sumUrl", "") == sumupServerUrl &&
    prefs.getString("sumTok", "") == sumupApiToken &&
    prefs.getString("sumMach", "") == sumupMachineId &&
    prefs.getString("sumCur", "") == sumupCurrency &&
    prefs.getUInt("sumPoll", 0) == (sumupPollIntervalMs / 1000UL) &&
    prefs.getUInt("sumTout", 0) == (sumupTimeoutMs / 1000UL);
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: SumUp settings saved and verified");
  } else {
    Serial.println("Preferences: SumUp verification failed");
  }
}

void saveLanguageSetting() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, language could not be saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (language)");
    return;
  }

  String langCode = currentLanguage == LANG_DE ? "de" : "en";
  size_t written = prefs.putString("lang", langCode);
  prefs.end();

  if (written != langCode.length()) {
    Serial.println("Preferences: language could not be saved");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (language)");
    return;
  }

  bool verifyOk = prefs.getString("lang", "en") == langCode;
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: language saved and verified");
  } else {
    Serial.println("Preferences: language verification failed");
  }
}

void saveCurrencySetting() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, currency could not be saved");
    return;
  }

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (currency)");
    return;
  }

  size_t written = prefs.putString("currency", currentCurrency);
  prefs.end();

  if (written != currentCurrency.length()) {
    Serial.println("Preferences: currency could not be saved");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (currency)");
    return;
  }

  bool verifyOk = prefs.getString("currency", "EUR") == currentCurrency;
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: currency saved and verified");
  } else {
    Serial.println("Preferences: currency verification failed");
  }
}

void saveProductShaftSettings() {
  if (!ensureNvsReady()) {
    Serial.println("NVS not ready, shaft settings not saved");
    return;
  }

  normalizeProductShaftLayout();

  if (!prefs.begin("vending", false)) {
    Serial.println("Preferences: write open failed (shafts)");
    return;
  }

  bool allWritten = true;
  size_t countWritten = prefs.putUChar("shaftCnt", productShaftCount);
  size_t microstepsWritten = prefs.putUChar("shaftMicro", productShaftMicrosteps);
  if (countWritten != sizeof(uint8_t) || microstepsWritten != sizeof(uint8_t)) {
    allWritten = false;
  }
  for (int row = 0; row < productShaftMaxRowCount; row++) {
    String rowKey = "shaftR" + String(row + 1);
    if (prefs.putUChar(rowKey.c_str(), productShaftRowSlotCount[row]) != sizeof(uint8_t)) {
      allWritten = false;
    }
  }
  for (int i = 0; i < productShaftMaxCount; i++) {
    String keyPrimary = "shaftP" + String(i + 1);
    String keySecondary = "shaftS" + String(i + 1);
    String keyPrice = "shaftC" + String(i + 1);
    String keyCapacity = "shaftM" + String(i + 1);
    String keyQuantity = "shaftQ" + String(i + 1);
    String keyName = "shaftN" + String(i + 1);
    size_t primaryWritten = prefs.putUChar(keyPrimary.c_str(), productShaftPrimaryMotor[i]);
    size_t secondaryWritten = prefs.putUChar(keySecondary.c_str(), productShaftSecondaryMotor[i]);
    size_t priceWritten = prefs.putUInt(keyPrice.c_str(), productShaftPriceCents[i]);
    size_t capacityWritten = prefs.putUChar(keyCapacity.c_str(), productShaftCapacity[i]);
    size_t quantityWritten = prefs.putUChar(keyQuantity.c_str(), productShaftQuantity[i]);
    size_t nameWritten = prefs.putString(keyName.c_str(), productShaftName[i]);
    if (primaryWritten != sizeof(uint8_t) || secondaryWritten != sizeof(uint8_t) || priceWritten != sizeof(uint32_t) ||
        capacityWritten != sizeof(uint8_t) || quantityWritten != sizeof(uint8_t) || nameWritten != productShaftName[i].length()) {
      allWritten = false;
    }
  }
  prefs.end();

  if (!allWritten) {
    Serial.println("Preferences: shaft values could not be written completely");
    return;
  }

  if (!prefs.begin("vending", true)) {
    Serial.println("Preferences: verify open failed (shafts)");
    return;
  }

  bool verifyOk = true;
  if (prefs.getUChar("shaftCnt", 0) != productShaftCount) {
    verifyOk = false;
  }
  if (prefs.getUChar("shaftMicro", 0) != productShaftMicrosteps) {
    verifyOk = false;
  }
  for (int row = 0; row < productShaftMaxRowCount; row++) {
    String rowKey = "shaftR" + String(row + 1);
    if (prefs.getUChar(rowKey.c_str(), 255) != productShaftRowSlotCount[row]) {
      verifyOk = false;
    }
  }
  for (int i = 0; i < productShaftMaxCount; i++) {
    String keyPrimary = "shaftP" + String(i + 1);
    String keySecondary = "shaftS" + String(i + 1);
    String keyPrice = "shaftC" + String(i + 1);
    String keyCapacity = "shaftM" + String(i + 1);
    String keyQuantity = "shaftQ" + String(i + 1);
    String keyName = "shaftN" + String(i + 1);
    if (prefs.getUChar(keyPrimary.c_str(), 0) != productShaftPrimaryMotor[i] ||
        prefs.getUChar(keySecondary.c_str(), 0) != productShaftSecondaryMotor[i] ||
        (uint16_t)prefs.getUInt(keyPrice.c_str(), 0) != productShaftPriceCents[i] ||
        prefs.getUChar(keyCapacity.c_str(), 0) != productShaftCapacity[i] ||
        prefs.getUChar(keyQuantity.c_str(), 0) != productShaftQuantity[i] ||
        prefs.getString(keyName.c_str(), "") != productShaftName[i]) {
      verifyOk = false;
      break;
    }
  }
  prefs.end();

  if (verifyOk) {
    Serial.println("Preferences: shaft values saved and verified");
  } else {
    Serial.println("Preferences: shaft values verification failed");
  }
}

// =====================================================
// Hilfsfunktionen
// =====================================================
bool isDigitKey(char key) {
  return (key >= '0' && key <= '9');
}

bool isValidIPv4(const String& ip) {
  if (ip.length() < 7 || ip.length() > 15) return false;

  int parts = 0;
  int start = 0;

  while (start < ip.length()) {
    int dotPos = ip.indexOf('.', start);
    String part;

    if (dotPos == -1) {
      part = ip.substring(start);
      start = ip.length();
    } else {
      part = ip.substring(start, dotPos);
      start = dotPos + 1;
    }

    if (part.length() == 0 || part.length() > 3) return false;

    for (size_t i = 0; i < part.length(); i++) {
      if (!isDigit(part[i])) return false;
    }

    if (part.length() > 1 && part[0] == '0') return false;

    int value = part.toInt();
    if (value < 0 || value > 255) return false;

    parts++;
  }

  return (parts == 4);
}

String getCurrencyLabel() {
  if (currentCurrency.length() == 0) return "EUR";
  return currentCurrency;
}

String formatCentsForInput(uint32_t cents) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu.%02lu", (unsigned long)(cents / 100), (unsigned long)(cents % 100));
  return String(buffer);
}

String formatCentsToMoney(uint32_t cents) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu.%02lu", (unsigned long)(cents / 100), (unsigned long)(cents % 100));
  return String(buffer) + " " + getCurrencyLabel();
}

String renderCurrencySelect() {
  const char* currencies[] = {"EUR", "USD", "CHF", "GBP", "JPY", "SEK", "NOK", "DKK", "PLN"};
  const int currencyCount = sizeof(currencies) / sizeof(currencies[0]);

  String html = "<select id='currency' name='currency'>";
  for (int i = 0; i < currencyCount; i++) {
    html += "<option value='" + String(currencies[i]) + "'";
    if (currentCurrency == currencies[i]) html += " selected";
    html += ">" + String(currencies[i]) + "</option>";
  }
  html += "</select>";
  return html;
}

String renderCoinMappingRow(int index, uint8_t pulses, uint16_t cents) {
  String row = "<div class='coin-map-row'>";
  row += "<div class='row'><label class='label' data-role='pulse-label' for='pulse" + String(index) + "'>" + lang("Pulse", "Pulses") + "</label>";
  row += "<input data-role='pulse-input' id='pulse" + String(index) + "' name='pulse" + String(index) + "' inputmode='numeric' value='" + String(pulses) + "'></div>";
  row += "<div class='row'><label class='label' data-role='value-label' for='value" + String(index) + "'>" + lang("Wert", "Value") + " (" + getCurrencyLabel() + ")</label>";
  row += "<input data-role='value-input' id='value" + String(index) + "' name='value" + String(index) + "' value='" + formatCentsForInput(cents) + "'></div>";
  row += "<div class='row'><button type='button' class='button-link coin-remove' onclick='removeCoinRow(this)'>" + lang("Eintrag entfernen", "Remove entry") + "</button></div>";
  row += "</div>";
  return row;
}

bool parseEuroToCents(const String& input, uint16_t& outCents) {
  String value = input;
  value.trim();
  value.replace(',', '.');
  if (value.length() == 0) return false;

  int dot = value.indexOf('.');
  String eurosPart = (dot >= 0) ? value.substring(0, dot) : value;
  String centsPart = (dot >= 0) ? value.substring(dot + 1) : "0";

  if (eurosPart.length() == 0) eurosPart = "0";
  if (centsPart.length() == 0) centsPart = "0";
  if (centsPart.length() > 2) return false;
  if (centsPart.length() == 1) centsPart += "0";

  for (size_t i = 0; i < eurosPart.length(); i++) {
    if (!isDigit(eurosPart[i])) return false;
  }
  for (size_t i = 0; i < centsPart.length(); i++) {
    if (!isDigit(centsPart[i])) return false;
  }

  uint32_t euros = (uint32_t)eurosPart.toInt();
  uint32_t cents = (uint32_t)centsPart.toInt();
  uint32_t total = euros * 100 + cents;
  if (total > 65535) return false;

  outCents = (uint16_t)total;
  return true;
}

bool parseUnsigned16(const String& input, uint16_t minValue, uint16_t maxValue, uint16_t& outValue) {
  if (input.length() == 0) return false;

  for (size_t i = 0; i < input.length(); i++) {
    if (!isDigit(input[i])) return false;
  }

  uint32_t value = (uint32_t)input.toInt();
  if (value < minValue || value > maxValue) return false;

  outValue = (uint16_t)value;
  return true;
}

bool parseMotorSelection(const String& input, uint8_t& outMotor) {
  uint16_t value = 0;
  if (!parseUnsigned16(input, 0, stepperMotorCount, value)) return false;
  outMotor = (uint8_t)value;
  return true;
}

bool isSupportedProductShaftMicrostep(uint8_t microsteps) {
  const int supportedCount = sizeof(productShaftMicrostepsSupported) / sizeof(productShaftMicrostepsSupported[0]);
  for (int i = 0; i < supportedCount; i++) {
    if (productShaftMicrostepsSupported[i] == microsteps) return true;
  }
  return false;
}

uint16_t getConfiguredProductShaftEjectSteps() {
  uint32_t steps = (uint32_t)productShaftManualEjectBaseSteps * (uint32_t)productShaftMicrosteps;
  if (steps > 65535UL) {
    return 65535;
  }
  return (uint16_t)steps;
}

void syncProductShaftCountFromRows() {
  uint16_t total = 0;
  for (int i = 0; i < productShaftMaxRowCount; i++) {
    total += productShaftRowSlotCount[i];
  }
  if (total < 1) {
    total = 1;
  }
  if (total > productShaftMaxCount) {
    total = productShaftMaxCount;
  }
  productShaftCount = (uint8_t)total;
}

void normalizeProductShaftLayout() {
  uint8_t compact[productShaftMaxRowCount] = {0};
  int compactIndex = 0;
  uint16_t total = 0;

  for (int i = 0; i < productShaftMaxRowCount; i++) {
    uint8_t count = productShaftRowSlotCount[i];
    if (count > productShaftSlotsPerRow) {
      count = productShaftSlotsPerRow;
    }
    if (count > 0 && compactIndex < productShaftMaxRowCount) {
      compact[compactIndex++] = count;
      total += count;
    }
  }

  if (total == 0) {
    compact[0] = 1;
    compactIndex = 1;
    total = 1;
  }

  while (total > productShaftMaxCount && compactIndex > 0) {
    int last = compactIndex - 1;
    if (compact[last] > 0) {
      compact[last]--;
      total--;
    }
    if (compact[last] == 0) {
      compactIndex--;
    }
  }

  if (total == 0) {
    compact[0] = 1;
    compactIndex = 1;
    total = 1;
  }

  for (int i = 0; i < productShaftMaxRowCount; i++) {
    productShaftRowSlotCount[i] = (i < compactIndex) ? compact[i] : 0;
  }

  syncProductShaftCountFromRows();
}

uint8_t getProductShaftActiveRowCount() {
  int lastActiveRow = 0;
  for (int i = 0; i < productShaftMaxRowCount; i++) {
    if (productShaftRowSlotCount[i] > 0) {
      lastActiveRow = i + 1;
    }
  }
  return lastActiveRow > 0 ? (uint8_t)lastActiveRow : (uint8_t)1;
}

int getProductShaftRowStartIndex(int rowIndex) {
  if (rowIndex < 0 || rowIndex >= productShaftMaxRowCount) return -1;
  int start = 0;
  for (int i = 0; i < rowIndex; i++) {
    start += productShaftRowSlotCount[i];
  }
  return start;
}

int getProductShaftIndexForRowSlot(int rowIndex, int slotIndex) {
  if (rowIndex < 0 || rowIndex >= productShaftMaxRowCount) return -1;
  if (slotIndex < 0 || slotIndex >= productShaftRowSlotCount[rowIndex]) return -1;
  int shaftIndex = getProductShaftRowStartIndex(rowIndex) + slotIndex;
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return -1;
  return shaftIndex;
}

int getProductShaftRowForIndex(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return -1;
  int running = 0;
  for (int row = 0; row < productShaftMaxRowCount; row++) {
    int next = running + productShaftRowSlotCount[row];
    if (shaftIndex < next) {
      return row;
    }
    running = next;
  }
  return -1;
}

int getProductShaftSlotForIndex(int shaftIndex) {
  int row = getProductShaftRowForIndex(shaftIndex);
  if (row < 0) return -1;
  return shaftIndex - getProductShaftRowStartIndex(row);
}

void resetProductShaftAt(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftMaxCount) return;
  productShaftPrimaryMotor[shaftIndex] = 0;
  productShaftSecondaryMotor[shaftIndex] = 0;
  productShaftPriceCents[shaftIndex] = 0;
  productShaftCapacity[shaftIndex] = productShaftMinCapacity;
  productShaftQuantity[shaftIndex] = productShaftMinCapacity;
  productShaftName[shaftIndex] = "";
  lowStockNotificationSent[shaftIndex] = false;
}

bool insertProductShaftAt(int shaftIndex) {
  if (productShaftCount >= productShaftMaxCount) return false;
  if (shaftIndex < 0 || shaftIndex > productShaftCount) return false;

  for (int i = productShaftCount; i > shaftIndex; i--) {
    productShaftPrimaryMotor[i] = productShaftPrimaryMotor[i - 1];
    productShaftSecondaryMotor[i] = productShaftSecondaryMotor[i - 1];
    productShaftPriceCents[i] = productShaftPriceCents[i - 1];
    productShaftCapacity[i] = productShaftCapacity[i - 1];
    productShaftQuantity[i] = productShaftQuantity[i - 1];
    productShaftName[i] = productShaftName[i - 1];
    lowStockNotificationSent[i] = lowStockNotificationSent[i - 1];
  }

  resetProductShaftAt(shaftIndex);
  productShaftCount++;
  return true;
}

bool removeProductShaftAt(int shaftIndex) {
  if (productShaftCount <= 1) return false;
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return false;

  for (int i = shaftIndex; i < productShaftCount - 1; i++) {
    productShaftPrimaryMotor[i] = productShaftPrimaryMotor[i + 1];
    productShaftSecondaryMotor[i] = productShaftSecondaryMotor[i + 1];
    productShaftPriceCents[i] = productShaftPriceCents[i + 1];
    productShaftCapacity[i] = productShaftCapacity[i + 1];
    productShaftQuantity[i] = productShaftQuantity[i + 1];
    productShaftName[i] = productShaftName[i + 1];
    lowStockNotificationSent[i] = lowStockNotificationSent[i + 1];
  }

  productShaftCount--;
  resetProductShaftAt(productShaftCount);
  return true;
}

String getProductShaftCode(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return "--";
  int row = getProductShaftRowForIndex(shaftIndex);
  int slot = getProductShaftSlotForIndex(shaftIndex);
  if (row < 0 || slot < 0) return "--";
  return String(row + 1) + String(slot + 1);
}

String getProductShaftLabel(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return "--";
  int row = getProductShaftRowForIndex(shaftIndex);
  int slot = getProductShaftSlotForIndex(shaftIndex);
  if (row < 0 || slot < 0) return "--";
  return lang("Reihe ", "Row ") + String(row + 1) + " - " + lang("Schacht ", "Slot ") + String(slot + 1);
}

String getProductShaftName(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return "";
  return productShaftName[shaftIndex];
}

String formatBytes(uint64_t bytes) {
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
    return String((double)bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
  }
  if (bytes >= 1024ULL * 1024ULL) {
    return String((double)bytes / (1024.0 * 1024.0), 2) + " MB";
  }
  if (bytes >= 1024ULL) {
    return String((double)bytes / 1024.0, 1) + " KB";
  }
  return String((unsigned long)bytes) + " B";
}

String escapeHtml(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

bool initSdCard() {
#if !VM_ENABLE_SD
  sdCardMounted = false;
  sdCardStatusMessage = "SD per Build-Flag deaktiviert";
  return false;
#endif

  pinMode(sdCardCsPin, OUTPUT);
  digitalWrite(sdCardCsPin, HIGH);
  sdCardSpi.begin(sdCardSckPin, sdCardMisoPin, sdCardMosiPin, sdCardCsPin);
  const uint32_t sdFrequencies[] = {400000, 1000000, 4000000};
  const int sdFrequencyCount = sizeof(sdFrequencies) / sizeof(sdFrequencies[0]);
  sdCardMounted = false;

  for (int i = 0; i < sdFrequencyCount; i++) {
    uint32_t frequency = sdFrequencies[i];
    Serial.printf("SD: initializing over SPI at %lu Hz (SCK=%d MISO=%d MOSI=%d CS=%d)\n",
                  (unsigned long)frequency, sdCardSckPin, sdCardMisoPin, sdCardMosiPin, sdCardCsPin);

    if (SD.begin(sdCardCsPin, sdCardSpi, frequency)) {
      sdCardMounted = true;
      break;
    }

    Serial.println("SD: init failed, trying next frequency");
    SD.end();
    delay(30);
  }

  if (!sdCardMounted) {
    sdCardStatusMessage = lang("SD Init fehlgeschlagen. Verdrahtung/Pins pruefen.",
                               "SD init failed. Check wiring/pins.");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    sdCardMounted = false;
    sdCardStatusMessage = lang("Keine SD Karte eingelegt.", "No SD card inserted.");
    SD.end();
    return false;
  }

  uint64_t cardSizeMb = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("SD: card detected, type=%u, size=%llu MB\n",
                (unsigned int)cardType,
                (unsigned long long)cardSizeMb);
  sdCardStatusMessage = lang("SD Karte bereit.", "SD card ready.") + " " + String((unsigned long)cardSizeMb) + " MB";
  ensureCashbookCsvExists();
  ensureSumupLogExists();
  return true;
}

void syncClockWithNtp() {
  if (!wifiConnected) {
    timeSynced = false;
    return;
  }

  const char* primaryServer = wifiNtpServer.length() > 0 ? wifiNtpServer.c_str() : "pool.ntp.org";
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", primaryServer, "time.nist.gov");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    timeSynced = true;
    Serial.println("Time synchronized via NTP");
  } else {
    timeSynced = false;
    Serial.println("NTP time synchronization failed");
  }
}

String getCurrentTimestamp() {
  if (!timeSynced) {
    return "Time unknown";
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    return "Time unknown";
  }

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

String escapeCsvValue(const String& value) {
  String escaped = value;
  escaped.replace("\"", "\"\"");
  return "\"" + escaped + "\"";
}

bool ensureCashbookCsvExists() {
  if (!sdCardMounted) return false;
  if (SD.exists(cashbookCsvPath)) return true;

  File file = SD.open(cashbookCsvPath, FILE_WRITE);
  if (!file) return false;

  file.println("Uhrzeit,Artikelname,Kosten,Bezahlart");
  file.close();
  return true;
}

bool appendCashbookEntry(const String& articleName, uint32_t priceCents, const String& paymentMethod) {
  if (!sdCardMounted) return false;
  if (!ensureCashbookCsvExists()) return false;

  File file = SD.open(cashbookCsvPath, FILE_APPEND);
  if (!file) return false;

  String line = escapeCsvValue(getCurrentTimestamp()) + "," +
                escapeCsvValue(articleName) + "," +
                escapeCsvValue(formatCentsToMoney(priceCents)) + "," +
                escapeCsvValue(paymentMethod);
  file.println(line);
  file.close();
  return true;
}

bool ensureSumupLogExists() {
  if (!sdCardMounted) return false;
  if (SD.exists(sumupLogPath)) return true;

  File file = SD.open(sumupLogPath, FILE_WRITE);
  if (!file) return false;

  file.println("# SumUp Log");
  file.println("# Zeit | Meldung | Detail");
  file.close();
  return true;
}

String truncateForLog(const String& value, size_t maxLen) {
  if (value.length() <= maxLen) return value;
  if (maxLen < 4) return value.substring(0, maxLen);
  return value.substring(0, maxLen - 3) + "...";
}

bool appendSumupLog(const String& message, const String& detail) {
  String serialLine = "[SUMUP] " + message;
  if (detail.length() > 0) {
    serialLine += " | " + detail;
  }
  Serial.println(serialLine);

  if (!sdCardMounted) return false;
  if (!ensureSumupLogExists()) return false;

  File file = SD.open(sumupLogPath, FILE_APPEND);
  if (!file) return false;

  String line = getCurrentTimestamp() + " | " + message;
  if (detail.length() > 0) {
    line += " | " + truncateForLog(detail, 300);
  }
  file.println(line);
  file.close();
  return true;
}

String readSumupLogTail(size_t maxBytes) {
  if (!sdCardMounted) {
    return lang("SD Karte nicht verfuegbar.", "SD card not available.");
  }
  if (!ensureSumupLogExists()) {
    return lang("SumUp Log konnte nicht erstellt werden.", "SumUp log could not be created.");
  }

  File file = SD.open(sumupLogPath, FILE_READ);
  if (!file) {
    return lang("SumUp Log konnte nicht geoeffnet werden.", "SumUp log could not be opened.");
  }

  size_t fileSize = file.size();
  if (fileSize > maxBytes) {
    file.seek(fileSize - maxBytes);
    file.readStringUntil('\n');
  }

  String content = file.readString();
  file.close();
  content.trim();

  if (content.length() == 0) {
    return lang("Noch keine Logeintraege vorhanden.", "No log entries yet.");
  }

  return content;
}

void setSumupStatus(const String& message, const String& detail) {
  sumupLastMessage = message;
  appendSumupLog(message, detail);
}

String getCsvField(const String& line, int fieldIndex) {
  String field = "";
  int currentField = 0;
  bool inQuotes = false;

  for (int i = 0; i < line.length(); i++) {
    char c = line.charAt(i);

    if (c == '"') {
      if (inQuotes && i + 1 < line.length() && line.charAt(i + 1) == '"') {
        if (currentField == fieldIndex) field += '"';
        i++;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (c == ',' && !inQuotes) {
      if (currentField == fieldIndex) {
        return field;
      }
      currentField++;
      field = "";
    } else {
      if (currentField == fieldIndex) field += c;
    }
  }

  if (currentField == fieldIndex) {
    return field;
  }
  return "";
}

String encodeBase64(const String& input) {
  static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String output = "";
  int inputLength = input.length();

  for (int i = 0; i < inputLength; i += 3) {
    uint32_t octetA = (uint8_t)input.charAt(i);
    uint32_t octetB = (i + 1 < inputLength) ? (uint8_t)input.charAt(i + 1) : 0;
    uint32_t octetC = (i + 2 < inputLength) ? (uint8_t)input.charAt(i + 2) : 0;
    uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

    output += alphabet[(triple >> 18) & 0x3F];
    output += alphabet[(triple >> 12) & 0x3F];
    output += (i + 1 < inputLength) ? alphabet[(triple >> 6) & 0x3F] : '=';
    output += (i + 2 < inputLength) ? alphabet[triple & 0x3F] : '=';
  }

  return output;
}

bool smtpExpectResponse(Client& client, int expectedCode) {
  unsigned long startMs = millis();
  String response = "";

  while (millis() - startMs < 10000) {
    while (client.available()) {
      char c = (char)client.read();
      response += c;
      if (c == '\n') {
        Serial.print("SMTP: ");
        Serial.print(response);
        if (response.length() >= 3) {
          int code = response.substring(0, 3).toInt();
          bool finalLine = response.length() < 4 || response.charAt(3) != '-';
          if (code == expectedCode && finalLine) return true;
          if (code >= 400 && finalLine) return false;
        }
        response = "";
      }
    }
    if (!client.connected()) break;
    delay(10);
  }

  return false;
}

bool smtpSendCommand(Client& client, const String& command, int expectedCode) {
  client.print(command + "\r\n");
  Serial.println("SMTP CMD: " + command);
  return smtpExpectResponse(client, expectedCode);
}

bool sendEmailMessage(const String& subject, const String& body) {
  if (!emailNotifyEnabled || !wifiConnected || !isEmailConfigComplete()) {
    lastEmailSendMessage = lang("E-Mail Versand nicht konfiguriert.", "Email sending not configured.");
    return false;
  }

  std::unique_ptr<Client> client;
  if (emailProtocol == "smtps") {
    WiFiClientSecure* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }

  if (!client || !client->connect(emailHost.c_str(), emailPort)) {
    lastEmailSendMessage = lang("SMTP Verbindung fehlgeschlagen.", "SMTP connection failed.");
    return false;
  }

  if (!smtpExpectResponse(*client, 220)) {
    lastEmailSendMessage = lang("SMTP Begruessung fehlgeschlagen.", "SMTP greeting failed.");
    client->stop();
    return false;
  }

  if (!smtpSendCommand(*client, "EHLO esp32-s3.local", 250)) {
    lastEmailSendMessage = lang("SMTP EHLO fehlgeschlagen.", "SMTP EHLO failed.");
    client->stop();
    return false;
  }

  if (emailUsername.length() > 0) {
    if (!smtpSendCommand(*client, "AUTH LOGIN", 334) ||
        !smtpSendCommand(*client, encodeBase64(emailUsername), 334) ||
        !smtpSendCommand(*client, encodeBase64(emailPassword), 235)) {
      lastEmailSendMessage = lang("SMTP Anmeldung fehlgeschlagen.", "SMTP authentication failed.");
      client->stop();
      return false;
    }
  }

  if (!smtpSendCommand(*client, "MAIL FROM:<" + emailFrom + ">", 250) ||
      !smtpSendCommand(*client, "RCPT TO:<" + emailTo + ">", 250) ||
      !smtpSendCommand(*client, "DATA", 354)) {
    lastEmailSendMessage = lang("SMTP Versandvorbereitung fehlgeschlagen.", "SMTP send preparation failed.");
    client->stop();
    return false;
  }

  String data;
  data.reserve(512);
  data += "From: <" + emailFrom + ">\r\n";
  data += "To: <" + emailTo + ">\r\n";
  data += "Subject: " + subject + "\r\n";
  data += "Content-Type: text/plain; charset=UTF-8\r\n";
  data += "\r\n";
  data += body + "\r\n.\r\n";
  client->print(data);

  if (!smtpExpectResponse(*client, 250)) {
    lastEmailSendMessage = lang("SMTP Nachricht wurde nicht akzeptiert.", "SMTP message was not accepted.");
    client->stop();
    return false;
  }

  smtpSendCommand(*client, "QUIT", 221);
  client->stop();
  lastEmailSendMessage = lang("E-Mail erfolgreich gesendet.", "Email sent successfully.");
  return true;
}

bool sendEmailWithAttachment(const String& subject, const String& body, const char* attachmentPath, const String& attachmentName, const String& mimeType) {
  if (!emailNotifyEnabled || !wifiConnected || !isEmailConfigComplete()) {
    lastEmailSendMessage = lang("E-Mail Versand nicht konfiguriert.", "Email sending not configured.");
    return false;
  }

  if (!sdCardMounted || !SD.exists(attachmentPath)) {
    lastEmailSendMessage = lang("Anhang nicht verfuegbar.", "Attachment not available.");
    return false;
  }

  File attachment = SD.open(attachmentPath, FILE_READ);
  if (!attachment) {
    lastEmailSendMessage = lang("Anhang konnte nicht geoeffnet werden.", "Attachment could not be opened.");
    return false;
  }

  std::unique_ptr<Client> client;
  if (emailProtocol == "smtps") {
    WiFiClientSecure* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
  }

  if (!client || !client->connect(emailHost.c_str(), emailPort)) {
    attachment.close();
    lastEmailSendMessage = lang("SMTP Verbindung fehlgeschlagen.", "SMTP connection failed.");
    return false;
  }

  if (!smtpExpectResponse(*client, 220)) {
    attachment.close();
    lastEmailSendMessage = lang("SMTP Begruessung fehlgeschlagen.", "SMTP greeting failed.");
    client->stop();
    return false;
  }

  if (!smtpSendCommand(*client, "EHLO esp32-s3.local", 250)) {
    attachment.close();
    lastEmailSendMessage = lang("SMTP EHLO fehlgeschlagen.", "SMTP EHLO failed.");
    client->stop();
    return false;
  }

  if (emailUsername.length() > 0) {
    if (!smtpSendCommand(*client, "AUTH LOGIN", 334) ||
        !smtpSendCommand(*client, encodeBase64(emailUsername), 334) ||
        !smtpSendCommand(*client, encodeBase64(emailPassword), 235)) {
      attachment.close();
      lastEmailSendMessage = lang("SMTP Anmeldung fehlgeschlagen.", "SMTP authentication failed.");
      client->stop();
      return false;
    }
  }

  if (!smtpSendCommand(*client, "MAIL FROM:<" + emailFrom + ">", 250) ||
      !smtpSendCommand(*client, "RCPT TO:<" + emailTo + ">", 250) ||
      !smtpSendCommand(*client, "DATA", 354)) {
    attachment.close();
    lastEmailSendMessage = lang("SMTP Versandvorbereitung fehlgeschlagen.", "SMTP send preparation failed.");
    client->stop();
    return false;
  }

  String boundary = "----ESP32Boundary7d9f3a";
  client->print("From: <" + emailFrom + ">\r\n");
  client->print("To: <" + emailTo + ">\r\n");
  client->print("Subject: " + subject + "\r\n");
  client->print("MIME-Version: 1.0\r\n");
  client->print("Content-Type: multipart/mixed; boundary=" + boundary + "\r\n");
  client->print("\r\n");
  client->print("--" + boundary + "\r\n");
  client->print("Content-Type: text/plain; charset=UTF-8\r\n");
  client->print("\r\n");
  client->print(body + "\r\n");
  client->print("--" + boundary + "\r\n");
  client->print("Content-Type: " + mimeType + "; name=\"" + attachmentName + "\"\r\n");
  client->print("Content-Transfer-Encoding: base64\r\n");
  client->print("Content-Disposition: attachment; filename=\"" + attachmentName + "\"\r\n");
  client->print("\r\n");

  char buffer[57];
  while (attachment.available()) {
    int bytesRead = attachment.read((uint8_t*)buffer, sizeof(buffer));
    if (bytesRead <= 0) break;

    String chunk = "";
    chunk.reserve(bytesRead);
    for (int i = 0; i < bytesRead; i++) {
      chunk += buffer[i];
    }
    client->print(encodeBase64(chunk) + "\r\n");
  }
  attachment.close();

  client->print("--" + boundary + "--\r\n");
  client->print("\r\n.\r\n");

  if (!smtpExpectResponse(*client, 250)) {
    lastEmailSendMessage = lang("SMTP Nachricht wurde nicht akzeptiert.", "SMTP message was not accepted.");
    client->stop();
    return false;
  }

  smtpSendCommand(*client, "QUIT", 221);
  client->stop();
  lastEmailSendMessage = lang("E-Mail mit Anhang erfolgreich gesendet.", "Email with attachment sent successfully.");
  return true;
}

void updateLowStockNotificationState(int shaftIndex) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) return;

  if (productShaftQuantity[shaftIndex] > emailLowStockThreshold) {
    lowStockNotificationSent[shaftIndex] = false;
    return;
  }

  if (!emailNotifyEnabled || lowStockNotificationSent[shaftIndex]) return;

  String articleName = getProductShaftName(shaftIndex);
  if (articleName.length() == 0) articleName = getProductShaftCode(shaftIndex);

  String subject = lang("Bestandswarnung: ", "Low stock alert: ") + articleName;
  String body = lang("Der Bestand hat den Schwellwert erreicht.\n\n", "The stock has reached the configured threshold.\n\n");
  body += lang("Artikel: ", "Article: ") + articleName + "\n";
  body += lang("Schacht: ", "Slot: ") + getProductShaftCode(shaftIndex) + "\n";
  body += lang("Aktueller Bestand: ", "Current stock: ") + String(productShaftQuantity[shaftIndex]) + "\n";
  body += lang("Schwellwert: ", "Threshold: ") + String(emailLowStockThreshold) + "\n";

  if (sendEmailMessage(subject, body)) {
    lowStockNotificationSent[shaftIndex] = true;
  }
}

bool isEmailConfigComplete() {
  return emailHost.length() > 0 &&
         emailPort > 0 &&
         emailFrom.length() > 0 &&
         emailTo.length() > 0;
}

// =====================================================
// Texteingabe
// =====================================================
String getTextSetName() {
  switch (currentTextSet) {
    case TEXTSET_UPPER: return "ABC";
    case TEXTSET_LOWER: return "abc";
    case TEXTSET_NUM:   return "123";
    case TEXTSET_SYM:   return "Sym";
  }
  return "ABC";
}

void nextTextSet() {
  switch (currentTextSet) {
    case TEXTSET_UPPER: currentTextSet = TEXTSET_LOWER; break;
    case TEXTSET_LOWER: currentTextSet = TEXTSET_NUM;   break;
    case TEXTSET_NUM:   currentTextSet = TEXTSET_SYM;   break;
    case TEXTSET_SYM:   currentTextSet = TEXTSET_UPPER; break;
  }
}

void prevTextSet() {
  switch (currentTextSet) {
    case TEXTSET_UPPER: currentTextSet = TEXTSET_SYM;   break;
    case TEXTSET_LOWER: currentTextSet = TEXTSET_UPPER; break;
    case TEXTSET_NUM:   currentTextSet = TEXTSET_LOWER; break;
    case TEXTSET_SYM:   currentTextSet = TEXTSET_NUM;   break;
  }
}

String keyMapFor(char key, TextCharSet setMode) {
  switch (setMode) {
    case TEXTSET_UPPER:
      switch (key) {
        case '0': return " 0";
        case '1': return "1.-_";
        case '2': return "ABC2";
        case '3': return "DEF3";
        case '4': return "GHI4";
        case '5': return "JKL5";
        case '6': return "MNO6";
        case '7': return "PQRS7";
        case '8': return "TUV8";
        case '9': return "WXYZ9";
        case '#': return "#";
      }
      break;

    case TEXTSET_LOWER:
      switch (key) {
        case '0': return " 0";
        case '1': return "1.-_";
        case '2': return "abc2";
        case '3': return "def3";
        case '4': return "ghi4";
        case '5': return "jkl5";
        case '6': return "mno6";
        case '7': return "pqrs7";
        case '8': return "tuv8";
        case '9': return "wxyz9";
        case '#': return "#";
      }
      break;

    case TEXTSET_NUM:
      switch (key) {
        case '0': return "0";
        case '1': return "1.";
        case '2': return "2";
        case '3': return "3";
        case '4': return "4";
        case '5': return "5";
        case '6': return "6";
        case '7': return "7";
        case '8': return "8";
        case '9': return "9";
        case '#': return "#";
      }
      break;

    case TEXTSET_SYM:
      switch (key) {
        case '0': return " 0@";
        case '1': return ".-_/";
        case '2': return "#$%&";
        case '3': return "*+=\"";
        case '4': return "(),;";
        case '5': return ":!?\'";
        case '6': return "[]{}";
        case '7': return "<>|\\";
        case '8': return "^~`";
        case '9': return "@";
        case '#': return "#";
      }
      break;
  }

  return "";
}

bool isTextInputKey(char key) {
  return (key >= '0' && key <= '9') || key == '#';
}

void commitPendingMultiTap() {
  if (activeMultiTapKey == '\0') return;

  String map = keyMapFor(activeMultiTapKey, currentTextSet);
  if (map.length() > 0) {
    textBuffer += map[activeMultiTapIndex];
  }

  activeMultiTapKey = '\0';
  activeMultiTapIndex = 0;
}

String getVisibleTextInputLine(bool mask = false) {
  String visible = mask ? makeMaskString(textBuffer.length()) : textBuffer;

  if (activeMultiTapKey != '\0') {
    String map = keyMapFor(activeMultiTapKey, currentTextSet);
    if (map.length() > 0) {
      visible += "[";
      visible += map[activeMultiTapIndex];
      visible += "]";
    }
  } else {
    visible += "_";
  }

  if (visible.length() <= 16) return visible;
  return visible.substring(visible.length() - 16);
}

void showTextInputScreen(const String& title, bool mask = false) {
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  String header = title + " " + getTextSetName();
  lcd.print(header.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(getVisibleTextInputLine(mask));
}

void beginTextInput(const String& initialValue, AppMode mode, TextCharSet setMode = TEXTSET_UPPER) {
  textBuffer = initialValue;
  originalTextBuffer = initialValue;
  activeMultiTapKey = '\0';
  activeMultiTapIndex = 0;
  lastMultiTapTime = 0;
  currentTextSet = setMode;
  currentMode = mode;
}

bool handleMultiTapTextInput(char key, const String& title, bool mask = false) {
  unsigned long now = millis();

  if (activeMultiTapKey != '\0' && (now - lastMultiTapTime) > multiTapTimeoutMs) {
    commitPendingMultiTap();
    showTextInputScreen(title, mask);
  }

  if (key == 'A') {
    commitPendingMultiTap();
    prevTextSet();
    showTextInputScreen(title, mask);
    return true;
  }

  if (key == 'B') {
    commitPendingMultiTap();
    nextTextSet();
    showTextInputScreen(title, mask);
    return true;
  }

  if (isTextInputKey(key)) {
    String map = keyMapFor(key, currentTextSet);
    if (map.length() == 0) return true;

    if (activeMultiTapKey == key && (now - lastMultiTapTime) <= multiTapTimeoutMs) {
      activeMultiTapIndex++;
      if (activeMultiTapIndex >= (int)map.length()) {
        activeMultiTapIndex = 0;
      }
    } else {
      commitPendingMultiTap();
      activeMultiTapKey = key;
      activeMultiTapIndex = 0;
    }

    lastMultiTapTime = now;
    showTextInputScreen(title, mask);
    return true;
  }

  if (key == '*') {
    if (activeMultiTapKey != '\0') {
      activeMultiTapKey = '\0';
      activeMultiTapIndex = 0;
    } else if (textBuffer.length() > 0) {
      textBuffer.remove(textBuffer.length() - 1);
    }
    showTextInputScreen(title, mask);
    return true;
  }

  return false;
}

// =====================================================
// Menü-Navigation
// =====================================================
void enterServicePinMode() {
  currentMode = MODE_SERVICE_PIN;
  clearPendingProductSelection();
  pinInput = "";
  showServicePinScreen();
}

void enterServiceMenu() {
  currentMode = MODE_SERVICE_MENU;
  clearPendingProductSelection();
  serviceMenuIndex = 0;
  showServiceMenu();
}

void enterWifiMenu() {
  currentMode = MODE_WIFI_MENU;
  wifiMenuIndex = 0;
  showWifiMenu();
}

void enterManualIpMenu() {
  currentMode = MODE_WIFI_MANUAL_IP_MENU;
  manualIpMenuIndex = 0;
  showManualIpMenu();
}

void enterAdminPinMenu() {
  currentMode = MODE_ADMIN_PIN_MENU;
  showAdminPinMenu();
}

void enterLanguageMenu() {
  currentMode = MODE_LANGUAGE_MENU;
  showLanguageMenu();
}

void handleComboDetection(char key) {
  if (key != '*' && key != '#') {
    lastSpecialKey = '\0';
    return;
  }

  unsigned long now = millis();

  if (lastSpecialKey == '\0') {
    lastSpecialKey = key;
    lastSpecialKeyTime = now;
    return;
  }

  if (lastSpecialKey != key && (now - lastSpecialKeyTime) <= comboWindowMs) {
    lastSpecialKey = '\0';
    enterServicePinMode();
    return;
  }

  lastSpecialKey = key;
  lastSpecialKeyTime = now;
}

// =====================================================
// WiFi
// =====================================================
void stopWifiCompletely() {
#if !VM_ENABLE_WIFI
  wifiStackInitialized = false;
  wifiConnected = false;
  wifiStatusMessage = "WiFi per Build aus";
  timeSynced = false;
  return;
#else
  if (!wifiStackInitialized) {
    wifiConnected = false;
    wifiStatusMessage = "WiFi deaktiviert";
    timeSynced = false;
    return;
  }

  WiFi.disconnect(false, false);
  delay(50);
  WiFi.mode(WIFI_OFF);
  wifiStackInitialized = false;
  wifiConnected = false;
  wifiStatusMessage = "WiFi deaktiviert";
  timeSynced = false;
#endif
}

void reconnectWifiAfterSettingsSave(const char* source) {
  wifiSkipThisBoot = false;
  Serial.println(String("WiFi: settings saved via ") + source);

  if (wifiSSID.length() == 0) {
    Serial.println("WiFi: SSID empty, WiFi will be disabled");
    stopWifiCompletely();
    return;
  }

  lastWifiAttemptMs = millis() - wifiRetryIntervalMs;
  wifiStatusMessage = "WiFi reconnecting";
  Serial.println("WiFi: starting reconnect after saving settings");
  applyWifiConfig();
}

void saveWifiSettingsWithFeedback(const char* source) {
  lcdPrint2(lang("Speichere...", "Saving..."),
            wifiSSID.length() > 0 ? lang("Verbinde WLAN", "Connecting WiFi")
                                  : lang("WiFi wird aus", "WiFi disabling"));

  saveWifiSettings();
  reconnectWifiAfterSettingsSave(source);

  if (wifiSSID.length() == 0) {
    showTemporaryMessage(lang("WiFi gespeichert", "WiFi saved"),
                         lang("WiFi aus", "WiFi off"), 1600);
  } else if (wifiConnected) {
    showTemporaryMessage(lang("WiFi verbunden", "WiFi connected"),
                         WiFi.localIP().toString(), 1800);
  } else {
    showTemporaryMessage(lang("WiFi Fehler", "WiFi failed"),
                         lang("Daten pruefen", "Check settings"), 2200);
  }
}

const char* wifiStatusToText(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
    default: return "WL_UNKNOWN";
  }
}

void applyWifiConfig() {
#if !VM_ENABLE_WIFI
  wifiConnected = false;
  wifiStackInitialized = false;
  wifiStatusMessage = "WiFi disabled by build";
  timeSynced = false;
  lastWifiAttemptMs = millis();
  Serial.println("WiFi: disabled by build flag");
  return;
#else
  if (wifiSkipThisBoot) {
    wifiConnected = false;
    wifiStatusMessage = "WiFi paused";
    timeSynced = false;
    lastWifiAttemptMs = millis();
    Serial.println("WiFi: temporarily paused after reset");
    return;
  }

  if (wifiSSID.length() == 0) {
    stopWifiCompletely();
    Serial.println("WiFi: no SSID configured, not attempting connection");
    return;
  }

  Serial.println("WiFi: preparing stack");
  WiFi.persistent(false);
  Serial.println("WiFi: persistent(false) ok");
  wifiStackInitialized = true;
  WiFi.setSleep(false);
  Serial.println("WiFi: sleep disabled");
  delay(50);
  Serial.println("WiFi: disconnecting previous connection");
  WiFi.disconnect(true, false);
  Serial.println("WiFi: previous connection closed");
  delay(150);

  if (wifiDhcp) {
    Serial.println("WiFi: DHCP enabled");
  } else {
    IPAddress ip, gateway, subnet, dns;
    bool ok =
      ip.fromString(wifiManualIp) &&
      gateway.fromString(wifiGateway) &&
      subnet.fromString(wifiSubnet) &&
      dns.fromString(wifiDns);

    if (!ok) {
      wifiConnected = false;
      wifiStatusMessage = "Manual IP invalid";
      lastWifiAttemptMs = millis();
      Serial.println("WiFi: invalid manual network settings");
      return;
    }

    if (!WiFi.config(ip, gateway, subnet, dns)) {
      wifiConnected = false;
      wifiStatusMessage = "Config failed";
      lastWifiAttemptMs = millis();
      Serial.println("WiFi: WiFi.config failed");
      return;
    }
  }

  Serial.println("WiFi: connecting to SSID: " + wifiSSID);
  Serial.println("WiFi: mode " + String(wifiDhcp ? "DHCP" : "static IP"));
  if (!wifiDhcp) {
    Serial.println("WiFi: static IP " + wifiManualIp);
    Serial.println("WiFi: Gateway " + wifiGateway);
    Serial.println("WiFi: subnet " + wifiSubnet);
    Serial.println("WiFi: DNS " + wifiDns);
  }

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  unsigned long startMs = millis();
  wl_status_t lastStatus = WiFi.status();
  Serial.println(String("WiFi: initial status ") + wifiStatusToText(lastStatus));

  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
    delay(250);
    Serial.print(".");
    wl_status_t currentStatus = WiFi.status();
    if (currentStatus != lastStatus) {
      Serial.println();
      Serial.println(String("WiFi: status change -> ") + wifiStatusToText(currentStatus));
      lastStatus = currentStatus;
    }
  }
  Serial.println();

  wl_status_t finalStatus = WiFi.status();
  if (finalStatus == WL_CONNECTED) {
    wifiConnected = true;
    wifiStatusMessage = WiFi.localIP().toString();
    Serial.println("WiFi connected");
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("Active gateway: " + WiFi.gatewayIP().toString());
    Serial.println("Active subnet: " + WiFi.subnetMask().toString());
    Serial.println("Active DNS: " + WiFi.dnsIP().toString());
    syncClockWithNtp();
    if (!webServerStarted) {
      setupWebServer();
    }
  } else {
    wifiConnected = false;
    wifiStatusMessage = "Connect failed";
    timeSynced = false;
    Serial.println(String("WiFi connection failed, status: ") + wifiStatusToText(finalStatus));
    Serial.println("WiFi: please check SSID, password, and 2.4 GHz router settings");
  }

  lastWifiAttemptMs = millis();
#endif
}

void ensureWifiConnection() {
  if (wifiSkipThisBoot) {
    return;
  }

  if (wifiSSID.length() == 0) {
    if (wifiStackInitialized) {
      stopWifiCompletely();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiStatusMessage = WiFi.localIP().toString();
    return;
  }

  wifiConnected = false;

  if (millis() - lastWifiAttemptMs >= wifiRetryIntervalMs) {
    applyWifiConfig();
  }
}

void processCoinSignal() {
  bool isLow = digitalRead(coinPulsePin) == LOW;
  unsigned long nowMs = millis();

  if (isLow && !coinSignalLow) {
    coinSignalLow = true;
    coinSignalLowStartedMs = nowMs;
    return;
  }

  if (!isLow && coinSignalLow) {
    unsigned long pulseWidthMs = nowMs - coinSignalLowStartedMs;
    coinSignalLow = false;

    if (pulseWidthMs >= coinPulseMinLowMs && pulseWidthMs <= coinPulseMaxLowMs) {
      coinLastPulseIntervalUs = coinLastAcceptedPulseMs == 0 ? 0 : ((nowMs - coinLastAcceptedPulseMs) * 1000UL);
      coinLastAcceptedPulseMs = nowMs;
      coinPulseCount++;
      coinBurstActive = true;
      Serial.printf("Coin pulse accepted: width=%lu ms count=%u\n",
                    pulseWidthMs, coinPulseCount);
    } else {
      Serial.printf("Coin pulse rejected: width=%lu ms\n", pulseWidthMs);
    }
  }
}

void processCoinPulseBurst() {
  if (!coinBurstActive) return;

  unsigned long nowMs = millis();
  if ((nowMs - coinLastAcceptedPulseMs) < coinBurstTimeoutMs) return;

  uint8_t pulses = coinPulseCount;
  coinPulseCount = 0;
  coinBurstActive = false;

  if (pulses == 0) return;

  uint16_t slotValue = 0;
  if (getCoinValueForPulseCount(pulses, slotValue)) {
    creditCents += slotValue;
    lastCoinPulseCount = pulses;
    lastCoinValueCents = slotValue;
    Serial.printf("Coin detected: %u pulses, value %u cents, credit %lu cents, last interval %lu us\n",
                  pulses, slotValue, (unsigned long)creditCents, (unsigned long)coinLastPulseIntervalUs);
  } else {
    Serial.printf("Unknown pulse sequence: %u pulses, last interval %lu us\n",
                  pulses, (unsigned long)coinLastPulseIntervalUs);
  }

}

void refreshNormalScreenIfNeeded() {
  if (currentMode != MODE_NORMAL) return;
  if (creditCents == lastDisplayedCreditCents) return;

  showNormalScreen();
  Serial.println("LCD refreshed after credit change");
}

// =====================================================
// Motor controller UART
// =====================================================
void setupMotorControllerBus() {
  motorControllerBus.begin(motorControllerBaud, SERIAL_8N1, motorControllerRxPin, motorControllerTxPin);
  Serial.println("Motor ESP UART started");
  Serial.println("Motor-ESP UART RX: GPIO " + String(motorControllerRxPin));
  Serial.println("Motor-ESP UART TX: GPIO " + String(motorControllerTxPin));
  delay(50);
  clearMotorControllerInput();
}

void clearMotorControllerInput() {
  motorControllerRxBuffer = "";
  while (motorControllerBus.available() > 0) {
    motorControllerBus.read();
  }
}

bool readMotorControllerFrame(String& line, uint32_t timeoutMs) {
  unsigned long startMs = millis();
  while (millis() - startMs < timeoutMs) {
    while (motorControllerBus.available() > 0) {
      char c = static_cast<char>(motorControllerBus.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line = motorControllerRxBuffer;
        motorControllerRxBuffer = "";
        line.trim();
        if (line.length() > 0) {
          return true;
        }
        continue;
      }
      if (motorControllerRxBuffer.length() < 180) {
        motorControllerRxBuffer += c;
      } else {
        motorControllerRxBuffer = "";
      }
    }
    delay(1);
  }
  return false;
}

void handleMotorControllerEvent(const String& line) {
  if (line.startsWith("!READY")) {
    motorControllerReady = true;
  }
  lastMotorControllerMessage = line;
  Serial.println("Motor ESP event: " + line);
}

bool transactMotorController(const String& command, String& payloadOut, uint32_t timeoutMs) {
  for (int attempt = 0; attempt < 2; attempt++) {
    clearMotorControllerInput();

    String requestId = String(motorControllerNextRequestId++);
    String frame = "@" + requestId + " " + command;
    Serial.println("Motor ESP TX: " + frame);
    motorControllerBus.println(frame);

    unsigned long startMs = millis();
    while (millis() - startMs < timeoutMs) {
      String line;
      uint32_t remaining = timeoutMs - (millis() - startMs);
      if (!readMotorControllerFrame(line, remaining)) {
        break;
      }

      if (line.startsWith("!")) {
        handleMotorControllerEvent(line);
        continue;
      }

      if (!line.startsWith("@")) {
        Serial.println("Motor ESP RX ignored: " + line);
        continue;
      }

      int firstSpace = line.indexOf(' ');
      if (firstSpace < 0) {
        continue;
      }

      String responseId = line.substring(1, firstSpace);
      if (responseId != requestId) {
        Serial.println("Motor ESP RX for other request: " + line);
        continue;
      }

      String rest = line.substring(firstSpace + 1);
      int statusSplit = rest.indexOf(' ');
      String status = statusSplit >= 0 ? rest.substring(0, statusSplit) : rest;
      String payload = statusSplit >= 0 ? rest.substring(statusSplit + 1) : "";
      payload.trim();

      Serial.println("Motor ESP RX: " + line);
      payloadOut = payload;
      lastMotorControllerMessage = line;
      if (status == "OK") {
        return true;
      }
      if (payloadOut.length() == 0) {
        payloadOut = "Motor ESP error";
      }
      return false;
    }

    if (attempt == 0) {
      Serial.println("Motor ESP timeout, retrying once");
      delay(100);
    }
  }

  payloadOut = "Motor ESP timeout";
  lastMotorControllerMessage = payloadOut;
  Serial.println(payloadOut);
  return false;
}

void processMotorControllerBus() {
  while (motorControllerBus.available() > 0) {
    String line;
    if (!readMotorControllerFrame(line, 1)) {
      return;
    }
    if (line.startsWith("!")) {
      handleMotorControllerEvent(line);
    } else if (line.length() > 0) {
      Serial.println("Motor ESP RX async: " + line);
      lastMotorControllerMessage = line;
    }
  }
}

uint32_t buildMotorMask(const int* motorIndexes, int motorCount) {
  uint32_t mask = 0;
  for (int i = 0; i < motorCount; i++) {
    if (motorIndexes[i] >= 0 && motorIndexes[i] < stepperMotorCount) {
      mask |= (1UL << motorIndexes[i]);
    }
  }
  return mask;
}

bool pulseDoorLock() {
  String payload;
  if (transactMotorController("LOCK " + String(doorLockSignalMs), payload, doorLockSignalMs + 5000)) {
    lastDoorLockMessage = lang("Magnetschloss war 5 Sekunden aktiv.", "Magnet lock was active for 5 seconds.");
    return true;
  } else {
    lastDoorLockMessage = lang("Magnetschloss Fehler: ", "Door lock error: ") + payload;
    return false;
  }
}

bool runStepperMotorTest(int motorIndex, uint16_t steps, uint16_t pulseUs) {
  if (motorIndex < 0 || motorIndex >= stepperMotorCount) {
    lastMotorTestMessage = "Ungueltiger Motorindex.";
    return false;
  }

  String payload;
  String command = "TEST " + String(motorIndex + 1) + " " + String(steps) + " " + String(pulseUs) + " " + String(motorControllerStepPin);
  bool ok = transactMotorController(command, payload, 20000);
  lastMotorTestMessage = ok ? ("Motortest: " + payload) : payload;
  return ok;
}

bool runProductShaftEject(int shaftIndex, uint16_t steps, uint16_t pulseUs) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) {
    lastShaftActionMessage = "Ungueltiger Schacht.";
    return false;
  }

  int motors[2];
  int motorCount = 0;

  if (productShaftPrimaryMotor[shaftIndex] > 0) {
    motors[motorCount++] = productShaftPrimaryMotor[shaftIndex] - 1;
  }
  if (productShaftSecondaryMotor[shaftIndex] > 0) {
    motors[motorCount++] = productShaftSecondaryMotor[shaftIndex] - 1;
  }

  if (motorCount <= 0) {
    lastShaftActionMessage = getProductShaftLabel(shaftIndex) + ": " + lang("Keine Motoren zugewiesen.", "No motors assigned.");
    return false;
  }

  uint32_t mask = buildMotorMask(motors, motorCount);
  String message;
  String command = "RUN " + String(mask) + " " + String(steps) + " " + String(pulseUs) + " " + String(motorControllerStepPin);
  bool ok = transactMotorController(command, message, 30000);
  if (ok) {
    lastShaftActionMessage = getProductShaftLabel(shaftIndex) + ": " + lang("manueller Auswurf ausgefuehrt. ", "manual eject executed. ") + message;
  } else {
    lastShaftActionMessage = getProductShaftLabel(shaftIndex) + ": " + message;
  }
  return ok;
}

bool isProductShaftInStock(int shaftIndex) {
  return shaftIndex >= 0 &&
         shaftIndex < productShaftCount &&
         productShaftQuantity[shaftIndex] > 0;
}

bool rejectEmptyProductShaft(int shaftIndex, const String& displayName) {
  if (isProductShaftInStock(shaftIndex)) {
    return false;
  }

  String shaftCode = getProductShaftCode(shaftIndex);
  String shaftLabel = getProductShaftLabel(shaftIndex);
  uint8_t quantity = 0;
  if (shaftIndex >= 0 && shaftIndex < productShaftCount) {
    quantity = productShaftQuantity[shaftIndex];
  }

  Serial.printf("Vend blocked: shaftIndex=%d code=%s qty=%u\n",
                shaftIndex,
                shaftCode.c_str(),
                (unsigned int)quantity);

  lastShaftActionMessage = shaftLabel + ": " +
                           displayName + " " +
                           lang("ist leer und nicht verfuegbar.", "is empty and unavailable.");
  showTemporaryMessage(lang("Nicht verfuegbar", "Unavailable"), displayName, 2500);
  showNormalScreen();
  return true;
}

bool vendProductByCode(char row, char numberKey) {
  int shaftIndex = -1;

  if (row >= '1' && row < ('1' + productShaftMaxRowCount) && numberKey >= '1' && numberKey <= '8') {
    shaftIndex = getProductShaftIndexForRowSlot(row - '1', numberKey - '1');
  }

  if (shaftIndex < 0 || shaftIndex >= productShaftCount) {
    showTemporaryMessage(lang("Code ungueltig", "Invalid code"), String(row) + String(numberKey), 1500);
    showNormalScreen();
    return false;
  }

  String shaftName = getProductShaftName(shaftIndex);
  String displayName = shaftName.length() > 0 ? shaftName : getProductShaftCode(shaftIndex);

  if (rejectEmptyProductShaft(shaftIndex, displayName)) {
    return false;
  }

  uint16_t priceCents = productShaftPriceCents[shaftIndex];
  if (creditCents < priceCents) {
    uint32_t missingCents = priceCents - creditCents;
    if (isSumupConfigured() && wifiConnected) {
      pendingCashlessSelection = true;
      pendingCashlessShaftIndex = shaftIndex;
      pendingProductSelectionMs = millis();
      showPendingCashlessSelection();
    } else {
      showTemporaryMessage(lang("Fehlt:", "Missing:"), formatCentsToMoney(missingCents), 5000);
      showNormalScreen();
    }
    return false;
  }

  return vendProductAtIndex(shaftIndex, lang("Muenzen", "Coins"));
}

bool vendProductAtIndex(int shaftIndex, const String& paymentMethod) {
  if (shaftIndex < 0 || shaftIndex >= productShaftCount) {
    showTemporaryMessage(lang("Code ungueltig", "Invalid code"), "--", 1500);
    showNormalScreen();
    return false;
  }

  String shaftName = getProductShaftName(shaftIndex);
  String displayName = shaftName.length() > 0 ? shaftName : getProductShaftCode(shaftIndex);

  if (rejectEmptyProductShaft(shaftIndex, displayName)) {
    return false;
  }

  Serial.printf("Vend start: shaftIndex=%d code=%s qty=%u payment=%s\n",
                shaftIndex,
                getProductShaftCode(shaftIndex).c_str(),
                (unsigned int)productShaftQuantity[shaftIndex],
                paymentMethod.c_str());

  uint16_t priceCents = productShaftPriceCents[shaftIndex];
  if (creditCents < priceCents) {
    uint32_t missingCents = priceCents - creditCents;
    showTemporaryMessage(lang("Fehlt:", "Missing:"), formatCentsToMoney(missingCents), 3000);
    showNormalScreen();
    return false;
  }

  lcdPrint2(lang("Ausgabe ", "Dispense ") + getProductShaftCode(shaftIndex),
            formatCentsToMoney(priceCents));
  delay(300);

  if (!runProductShaftEject(shaftIndex, getConfiguredProductShaftEjectSteps(), productShaftManualEjectPulseUs)) {
    showTemporaryMessage(lang("Ausgabe Fehler", "Dispense error"), getProductShaftCode(shaftIndex), 2000);
    showNormalScreen();
    return false;
  }

  if (productShaftQuantity[shaftIndex] > 0) {
    productShaftQuantity[shaftIndex]--;
  }
  saveProductShaftSettings();
  updateLowStockNotificationState(shaftIndex);
  if (!appendCashbookEntry(displayName, priceCents, paymentMethod)) {
    Serial.println("Kassenbuch: Verkauf konnte nicht auf SD protokolliert werden");
  }
  creditCents -= priceCents;
  lastShaftActionMessage = getProductShaftLabel(shaftIndex) + ": " +
                           displayName + ", " +
                           lang("Bestand jetzt ", "stock now ") +
                           String(productShaftQuantity[shaftIndex]) + "/" + String(productShaftCapacity[shaftIndex]) + ".";
  showTemporaryMessage(lang("Ausgabe fertig", "Dispense done"), getProductShaftCode(shaftIndex), 1200);
  showNormalScreen();
  return true;
}

// =====================================================
// Webserver
// =====================================================
String htmlHeader(const String& title) {
  String s;
  s += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>" + title + "</title>";
  s += "<style>";
  s += ":root{color-scheme:light;--bg:#edf2f7;--panel:#ffffff;--line:#d8e1eb;--text:#102a43;--muted:#52667a;--accent:#136f63;--accent-soft:#dff4ef;--warn:#b42318;}";
  s += "body{font-family:Arial,sans-serif;background:linear-gradient(180deg,#f6f9fc 0%,#e7eef5 100%);margin:0;padding:20px;color:var(--text);}";
  s += ".card{max-width:980px;margin:0 auto;background:var(--panel);padding:24px;border-radius:16px;box-shadow:0 14px 40px rgba(16,42,67,0.12);}";
  s += "input,button,select{font-size:16px;padding:10px;margin:6px 0;width:100%;box-sizing:border-box;border-radius:10px;border:1px solid var(--line);}";
  s += "button{cursor:pointer;background:var(--accent);border-color:var(--accent);color:#fff;font-weight:bold;}";
  s += ".row{margin-bottom:10px;}";
  s += ".label{font-weight:bold;}";
  s += ".ok{color:#137333;}";
  s += ".err{color:var(--warn);}";
  s += ".muted{color:var(--muted);}";
  s += ".tabs{display:flex;gap:10px;flex-wrap:wrap;margin:18px 0 22px 0;}";
  s += ".tab{display:inline-block;padding:10px 14px;background:#e8eef5;text-decoration:none;color:var(--text);border-radius:999px;border:1px solid transparent;font-weight:bold;}";
  s += ".tab.active{background:var(--accent-soft);border-color:#7dd3c7;color:#0b534b;}";
  s += ".actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:10px;}";
  s += ".button-link{display:inline-block;padding:10px 14px;background:#e8eef5;text-decoration:none;color:var(--text);border-radius:10px;}";
  s += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px;}";
  s += ".motor-card{border:1px solid var(--line);border-radius:14px;padding:14px;background:#fbfdff;}";
  s += ".section-card{border:1px solid var(--line);border-radius:14px;padding:16px;background:#fbfdff;margin-bottom:16px;}";
  s += "details.section-card{padding:0;overflow:hidden;}";
  s += "details.section-card>summary{cursor:pointer;padding:16px;font-weight:bold;list-style:none;}";
  s += "details.section-card>summary::-webkit-details-marker{display:none;}";
  s += "details.section-card[open]>summary{border-bottom:1px solid var(--line);background:#f4f8fb;}";
  s += ".details-body{padding:16px;}";
  s += ".coin-map-list{display:grid;gap:12px;margin:14px 0;}";
  s += ".coin-map-row{border:1px solid var(--line);border-radius:14px;padding:14px;background:#f8fbfe;}";
  s += ".coin-remove{background:#fff;color:var(--text);border-color:var(--line);font-weight:normal;}";
  s += ".hint{font-size:14px;color:var(--muted);}";
  s += ".inline-note{padding:10px 12px;border-radius:10px;background:#f8fafc;border:1px solid var(--line);margin:12px 0;}";
  s += ".footer{margin-top:28px;padding-top:16px;border-top:1px solid var(--line);text-align:center;color:var(--muted);font-size:14px;}";
  s += "table{width:100%;border-collapse:collapse;margin-top:10px;}";
  s += "th,td{text-align:left;padding:10px;border-bottom:1px solid var(--line);vertical-align:top;}";
  s += "th{background:#f4f8fb;}";
  s += "@media (max-width:640px){body{padding:12px;}.card{padding:16px;}}";
  s += "</style></head><body><div class='card'>";
  return s;
}

String htmlFooter() {
  return "<div class='footer'>VendingOS " + String(FW_VERSION) + " by Kreativ Welt 3D</div></div></body></html>";
}

String renderWebTabs(const String& activeTab) {
  String html = "<div class='tabs'>";
  html += "<a class='tab" + String(activeTab == "overview" ? " active" : "") + "' href='/'>" + lang("Uebersicht", "Overview") + "</a>";
  html += "<a class='tab" + String(activeTab == "wifi" ? " active" : "") + "' href='/wifi'>" + "WiFi" + "</a>";
  html += "<a class='tab" + String(activeTab == "email" ? " active" : "") + "' href='/email'>" + "E-Mail" + "</a>";
  html += "<a class='tab" + String(activeTab == "sumup" ? " active" : "") + "' href='/sumup'>SumUp</a>";
  html += "<a class='tab" + String(activeTab == "coins" ? " active" : "") + "' href='/coins'>" + lang("Muenzen", "Coins") + "</a>";
  html += "<a class='tab" + String(activeTab == "shafts" ? " active" : "") + "' href='/shafts'>" + lang("Schaechte", "Slots") + "</a>";
  html += "<a class='tab" + String(activeTab == "cashbook" ? " active" : "") + "' href='/cashbook'>" + lang("Kassenbuch", "Cashbook") + "</a>";
  html += "<a class='tab" + String(activeTab == "tests" ? " active" : "") + "' href='/tests'>" + lang("Tests", "Tests") + "</a>";
  html += "<a class='tab" + String(activeTab == "update" ? " active" : "") + "' href='/update'>Update</a>";
  html += "</div>";
  return html;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

String jsonGetString(const String& json, const String& key) {
  String pattern = "\"" + key + "\":";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return "";
  int valueStart = json.indexOf('"', keyPos + pattern.length());
  if (valueStart < 0) return "";
  valueStart++;
  String value = "";
  bool escaped = false;
  for (int i = valueStart; i < json.length(); i++) {
    char c = json[i];
    if (escaped) {
      value += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return value;
    } else {
      value += c;
    }
  }
  return "";
}

bool jsonGetBool(const String& json, const String& key, bool defaultValue) {
  String pattern = "\"" + key + "\":";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return defaultValue;
  String tail = json.substring(keyPos + pattern.length());
  tail.trim();
  if (tail.startsWith("true")) return true;
  if (tail.startsWith("false")) return false;
  return defaultValue;
}

uint32_t jsonGetUInt(const String& json, const String& key, uint32_t defaultValue) {
  String pattern = "\"" + key + "\":";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return defaultValue;
  int start = keyPos + pattern.length();
  while (start < json.length() && (json[start] == ' ' || json[start] == '"')) start++;
  int end = start;
  while (end < json.length() && isDigit(json[end])) end++;
  if (end <= start) return defaultValue;
  return (uint32_t)json.substring(start, end).toInt();
}

bool isSumupConfigured() {
  return sumupEnabled &&
         sumupServerUrl.length() > 0 &&
         sumupApiToken.length() > 0 &&
         sumupMachineId.length() > 0;
}

void clearCachedUpdateInfo() {
  cachedUpdateInfo.version = "";
  cachedUpdateInfo.firmwareUrl = "";
  cachedUpdateInfo.sha256 = "";
  cachedUpdateInfo.md5 = "";
  cachedUpdateInfo.notes = "";
  cachedUpdateInfo.size = 0;
  cachedUpdateInfo.valid = false;
}

void setUpdateStatus(const String& message, const String& detail) {
  lastUpdateMessage = message;
  lastUpdateDetail = detail;
  Serial.println("[UPDATE] " + message + (detail.length() > 0 ? " | " + detail : ""));
}

String normalizeHexString(String value) {
  value.trim();
  value.toLowerCase();
  value.replace(" ", "");
  value.replace(":", "");
  value.replace("-", "");
  return value;
}

String bytesToHexString(const uint8_t* bytes, size_t length) {
  const char* hex = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    out += hex[(bytes[i] >> 4) & 0x0F];
    out += hex[bytes[i] & 0x0F];
  }
  return out;
}

bool isHexStringValue(const String& value) {
  if (value.length() == 0) return false;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    bool isHex = (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F');
    if (!isHex) return false;
  }
  return true;
}

int readVersionPart(const String& value, int& index) {
  while (index < value.length() && !isDigit(value[index])) {
    index++;
  }
  if (index >= value.length()) return -1;

  int part = 0;
  while (index < value.length() && isDigit(value[index])) {
    part = (part * 10) + (value[index] - '0');
    index++;
  }
  return part;
}

int compareVersionStrings(const String& a, const String& b) {
  int indexA = 0;
  int indexB = 0;

  while (indexA < a.length() || indexB < b.length()) {
    int partA = readVersionPart(a, indexA);
    int partB = readVersionPart(b, indexB);

    if (partA < 0 && partB < 0) return 0;
    if (partA < 0) partA = 0;
    if (partB < 0) partB = 0;

    if (partA > partB) return 1;
    if (partA < partB) return -1;
  }

  return 0;
}

String resolveUpdateFirmwareUrl(const String& firmware) {
  String trimmed = firmware;
  trimmed.trim();

  if (trimmed.startsWith("https://") || trimmed.startsWith("http://")) {
    return trimmed;
  }

  if (trimmed.startsWith("/")) {
    return String("https://sumup.kreativwelt3d.de") + trimmed;
  }

  String base = firmwareUpdateBaseUrl;
  if (!base.endsWith("/")) {
    base += "/";
  }
  return base + trimmed;
}

bool canStartFirmwareUpdate(String& reasonOut) {
  if (firmwareUpdateInProgress) {
    reasonOut = lang("Ein Update laeuft bereits.", "An update is already running.");
    return false;
  }
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    reasonOut = lang("WLAN ist nicht verbunden.", "WiFi is not connected.");
    return false;
  }
  if (sumupPaymentPending) {
    reasonOut = lang("Eine SumUp Zahlung ist noch offen.", "A SumUp payment is still pending.");
    return false;
  }
  if (creditCents > 0) {
    reasonOut = lang("Update blockiert: Guthaben ist noch vorhanden.", "Update blocked: credit is still available.");
    return false;
  }
  reasonOut = "";
  return true;
}

bool fetchFirmwareUpdateManifest(FirmwareUpdateInfo& info) {
  info.version = "";
  info.firmwareUrl = "";
  info.sha256 = "";
  info.md5 = "";
  info.notes = "";
  info.size = 0;
  info.valid = false;

  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    setUpdateStatus(lang("Update-Pruefung fehlgeschlagen.", "Update check failed."),
                    lang("WLAN ist nicht verbunden.", "WiFi is not connected."));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  String manifestUrl = firmwareUpdateManifestUrl;
  if (!http.begin(client, manifestUrl)) {
    setUpdateStatus(lang("Update-Manifest URL ungueltig.", "Invalid update manifest URL."), manifestUrl);
    return false;
  }

  int statusCode = http.GET();
  String response = http.getString();
  http.end();

  if (statusCode < 200 || statusCode >= 300) {
    setUpdateStatus(lang("Update-Manifest konnte nicht geladen werden: ", "Update manifest could not be loaded: ") + String(statusCode),
                    truncateForLog(response.length() > 0 ? response : HTTPClient::errorToString(statusCode), 220));
    return false;
  }

  String firmware = jsonGetString(response, "firmware_url");
  if (firmware.length() == 0) firmware = jsonGetString(response, "firmware");
  if (firmware.length() == 0) firmware = jsonGetString(response, "url");

  info.version = jsonGetString(response, "version");
  info.firmwareUrl = resolveUpdateFirmwareUrl(firmware);
  info.sha256 = normalizeHexString(jsonGetString(response, "sha256"));
  info.md5 = normalizeHexString(jsonGetString(response, "md5"));
  info.notes = jsonGetString(response, "notes");
  info.size = jsonGetUInt(response, "size", 0);

  if (info.version.length() == 0 || firmware.length() == 0) {
    setUpdateStatus(lang("Update-Manifest ist unvollstaendig.", "Update manifest is incomplete."),
                    lang("version und firmware fehlen oder sind leer.", "version and firmware are missing or empty."));
    return false;
  }

  if (info.sha256.length() > 0 && (info.sha256.length() != 64 || !isHexStringValue(info.sha256))) {
    setUpdateStatus(lang("Update-Manifest SHA-256 ungueltig.", "Update manifest SHA-256 invalid."), info.sha256);
    return false;
  }

  if (info.md5.length() > 0 && (info.md5.length() != 32 || !isHexStringValue(info.md5))) {
    setUpdateStatus(lang("Update-Manifest MD5 ungueltig.", "Update manifest MD5 invalid."), info.md5);
    return false;
  }

  info.valid = true;
  cachedUpdateInfo = info;
  lastUpdateCheckMs = millis();
  setUpdateStatus(lang("Update-Manifest geladen.", "Update manifest loaded."),
                  "version=" + info.version + " url=" + info.firmwareUrl);
  return true;
}

bool isCachedFirmwareUpdateNewer() {
  return cachedUpdateInfo.valid && compareVersionStrings(cachedUpdateInfo.version, FW_VERSION) > 0;
}

bool installFirmwareUpdate(const FirmwareUpdateInfo& info) {
  String reason;
  if (!canStartFirmwareUpdate(reason)) {
    setUpdateStatus(lang("Update kann nicht gestartet werden.", "Update cannot be started."), reason);
    return false;
  }
  if (!info.valid) {
    setUpdateStatus(lang("Update kann nicht gestartet werden.", "Update cannot be started."),
                    lang("Keine gueltige Update-Information vorhanden.", "No valid update information available."));
    return false;
  }
  if (compareVersionStrings(info.version, FW_VERSION) <= 0) {
    setUpdateStatus(lang("Kein neueres Update verfuegbar.", "No newer update available."),
                    "installed=" + String(FW_VERSION) + " available=" + info.version);
    return false;
  }
  if (!info.firmwareUrl.startsWith("https://")) {
    setUpdateStatus(lang("Update abgebrochen.", "Update aborted."),
                    lang("Firmware-URL muss HTTPS verwenden.", "Firmware URL must use HTTPS."));
    return false;
  }

  firmwareUpdateInProgress = true;
  setUpdateStatus(lang("Firmware-Update startet.", "Firmware update starting."),
                  "version=" + info.version + " url=" + info.firmwareUrl);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  bool shaStarted = false;
  bool ok = false;
  bool shouldRestart = false;
  String failureMessage = "";
  String failureDetail = "";

  do {
    if (!http.begin(client, info.firmwareUrl)) {
      failureMessage = lang("Firmware-URL ungueltig.", "Invalid firmware URL.");
      failureDetail = info.firmwareUrl;
      break;
    }

    int statusCode = http.GET();
    if (statusCode < 200 || statusCode >= 300) {
      String response = http.getString();
      failureMessage = lang("Firmware konnte nicht geladen werden: ", "Firmware could not be loaded: ") + String(statusCode);
      failureDetail = truncateForLog(response.length() > 0 ? response : HTTPClient::errorToString(statusCode), 220);
      break;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
      failureMessage = lang("Firmware-Groesse fehlt.", "Firmware size missing.");
      failureDetail = lang("Der Server muss Content-Length senden.", "The server must send Content-Length.");
      break;
    }

    if (info.size > 0 && info.size != (uint32_t)contentLength) {
      failureMessage = lang("Firmware-Groesse passt nicht zum Manifest.", "Firmware size does not match manifest.");
      failureDetail = "manifest=" + String(info.size) + " http=" + String(contentLength);
      break;
    }

    if (info.sha256.length() > 0) {
      if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
        failureMessage = lang("SHA-256 Pruefung konnte nicht gestartet werden.", "SHA-256 check could not be started.");
        break;
      }
      shaStarted = true;
    }

    if (info.md5.length() == 32 && !Update.setMD5(info.md5.c_str())) {
      failureMessage = lang("MD5 Pruefsumme ungueltig.", "MD5 checksum invalid.");
      failureDetail = info.md5;
      break;
    }

    if (!Update.begin((size_t)contentLength, U_FLASH)) {
      failureMessage = lang("OTA-Speicher reicht nicht aus.", "OTA storage is not sufficient.");
      failureDetail = Update.errorString();
      break;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t written = 0;
    unsigned long lastDataMs = millis();
    unsigned long lastProgressMs = millis();

    while (written < (size_t)contentLength) {
      size_t available = stream->available();
      if (available > 0) {
        size_t remaining = (size_t)contentLength - written;
        size_t toRead = available;
        if (toRead > sizeof(buffer)) toRead = sizeof(buffer);
        if (toRead > remaining) toRead = remaining;

        int readBytes = stream->readBytes(buffer, toRead);
        if (readBytes <= 0) {
          failureMessage = lang("Firmware-Download unterbrochen.", "Firmware download interrupted.");
          failureDetail = lang("Keine Daten vom Server gelesen.", "No data read from server.");
          break;
        }

        if (shaStarted && mbedtls_sha256_update_ret(&shaContext, buffer, readBytes) != 0) {
          failureMessage = lang("SHA-256 Pruefung fehlgeschlagen.", "SHA-256 check failed.");
          failureDetail = lang("Hash konnte nicht berechnet werden.", "Hash could not be calculated.");
          break;
        }

        size_t updateWritten = Update.write(buffer, (size_t)readBytes);
        if (updateWritten != (size_t)readBytes) {
          failureMessage = lang("Firmware konnte nicht geschrieben werden.", "Firmware could not be written.");
          failureDetail = Update.errorString();
          break;
        }

        written += updateWritten;
        lastDataMs = millis();
        if (millis() - lastProgressMs > 1000) {
          lastProgressMs = millis();
          setUpdateStatus(lang("Firmware wird geschrieben.", "Writing firmware."),
                          formatBytes(written) + " / " + formatBytes((uint32_t)contentLength));
        }
        delay(1);
      } else {
        if (millis() - lastDataMs > 20000) {
          failureMessage = lang("Firmware-Download Timeout.", "Firmware download timeout.");
          failureDetail = formatBytes(written) + " / " + formatBytes((uint32_t)contentLength);
          break;
        }
        delay(1);
      }
    }

    if (failureMessage.length() > 0) {
      break;
    }

    if (written != (size_t)contentLength) {
      failureMessage = lang("Firmware unvollstaendig geladen.", "Firmware downloaded incompletely.");
      failureDetail = formatBytes(written) + " / " + formatBytes((uint32_t)contentLength);
      break;
    }

    if (shaStarted) {
      uint8_t shaResult[32];
      if (mbedtls_sha256_finish_ret(&shaContext, shaResult) != 0) {
        failureMessage = lang("SHA-256 Pruefung fehlgeschlagen.", "SHA-256 check failed.");
        failureDetail = lang("Hash konnte nicht beendet werden.", "Hash could not be finished.");
        break;
      }

      String actualSha = bytesToHexString(shaResult, sizeof(shaResult));
      if (actualSha != info.sha256) {
        failureMessage = lang("SHA-256 Pruefsumme passt nicht.", "SHA-256 checksum mismatch.");
        failureDetail = "expected=" + info.sha256 + " actual=" + actualSha;
        break;
      }
    }

    if (!Update.end(false)) {
      failureMessage = lang("Update konnte nicht aktiviert werden.", "Update could not be activated.");
      failureDetail = Update.errorString();
      break;
    }

    ok = true;
    shouldRestart = true;
  } while (false);

  if (!ok && Update.isRunning()) {
    Update.abort();
  }
  mbedtls_sha256_free(&shaContext);
  http.end();
  firmwareUpdateInProgress = false;

  if (!ok) {
    setUpdateStatus(failureMessage.length() > 0 ? failureMessage : lang("Update fehlgeschlagen.", "Update failed."),
                    failureDetail);
    return false;
  }

  setUpdateStatus(lang("Update erfolgreich installiert. Neustart...", "Update installed successfully. Restarting..."),
                  "version=" + info.version);

  if (shouldRestart) {
    delay(1200);
    ESP.restart();
  }
  return true;
}

void clearPendingCashlessSelection() {
  pendingCashlessSelection = false;
  pendingCashlessShaftIndex = -1;
}

void showPendingCashlessSelection() {
  if (pendingCashlessShaftIndex < 0 || pendingCashlessShaftIndex >= productShaftCount) {
    clearPendingCashlessSelection();
    showNormalScreen();
    return;
  }

  String code = getProductShaftCode(pendingCashlessShaftIndex);
  uint32_t amountCents = getPendingCashlessAmountCents();
  lcdPrint2(lang("Kartenzahlung->A", "Card payment ->A"),
            formatCentsToMoney(amountCents));
}

uint32_t getPendingCashlessAmountCents() {
  if (pendingCashlessShaftIndex < 0 || pendingCashlessShaftIndex >= productShaftCount) return 0;
  uint32_t priceCents = productShaftPriceCents[pendingCashlessShaftIndex];
  if (creditCents >= priceCents) return 0;
  return priceCents - creditCents;
}

void clearPendingSumupTopupSelection() {
  pendingSumupTopupSelection = false;
  pendingSumupTopupInput = "";
}

void showPendingSumupTopupSelection() {
  pendingSumupTopupSelection = true;
  pendingSumupTopupSelectionMs = millis();
  pendingSumupTopupInput = "";
  lcdPrint2(lang("Aufladung EUR?", "Top-up EUR?"),
            lang("0-9,C=loe,D=ab", "0-9,C=del,D=can"));
}

void showPendingSumupTopupAmount() {
  String amountLine = pendingSumupTopupInput.length() > 0
    ? pendingSumupTopupInput + " EUR"
    : lang("Bitte Betrag", "Enter amount");
  lcdPrint2(lang("A bestaetigt", "A confirms"),
            amountLine);
}

bool startSumupTopup(uint32_t amountCents, int vendShaftIndex) {
  if (!wifiConnected) {
    setSumupStatus(lang("SumUp Fehler: WLAN nicht verbunden.", "SumUp error: WiFi not connected."));
    return false;
  }
  if (!isSumupConfigured()) {
    setSumupStatus(lang("SumUp nicht konfiguriert.", "SumUp not configured."));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = sumupServerUrl;
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  url += "/start";
  if (!http.begin(client, url)) {
    setSumupStatus(lang("SumUp Server URL ungueltig.", "Invalid SumUp server URL."), url);
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + sumupApiToken);

  String body = "{\"machine_id\":\"" + jsonEscape(sumupMachineId) +
                "\",\"amount_cents\":" + String(amountCents) +
                ",\"currency\":\"" + jsonEscape(sumupCurrency) +
                "\",\"source\":\"vending-machine\"}";
  appendSumupLog(lang("Starte SumUp Zahlung", "Starting SumUp payment"),
                 "POST " + url + " | machine_id=" + sumupMachineId +
                 " | amount_cents=" + String(amountCents) +
                 " | currency=" + sumupCurrency);
  int statusCode = http.POST(body);
  String response = http.getString();
  http.end();

  if (statusCode < 200 || statusCode >= 300) {
    setSumupStatus(lang("SumUp Start fehlgeschlagen: ", "SumUp start failed: ") + String(statusCode),
                   truncateForLog(response, 240));
    return false;
  }

  bool ok = jsonGetBool(response, "ok", false);
  String paymentId = jsonGetString(response, "payment_id");
  String message = jsonGetString(response, "message");
  if (!ok || paymentId.length() == 0) {
    setSumupStatus(message.length() > 0 ? message : lang("SumUp Antwort ungueltig.", "Invalid SumUp response."),
                   truncateForLog(response, 240));
    return false;
  }

  sumupPaymentPending = true;
  sumupPendingPaymentId = paymentId;
  sumupPendingAmountCents = amountCents;
  sumupPendingVendShaftIndex = vendShaftIndex;
  sumupPendingStartedMs = millis();
  sumupNextPollMs = millis() + sumupPollIntervalMs;
  setSumupStatus(message.length() > 0 ? message : (lang("Zahlung gestartet: ", "Payment started: ") + formatCentsToMoney(amountCents)),
                 "payment_id=" + paymentId);
  showSumupPaymentPendingScreen();
  return true;
}

void showSumupPaymentPendingScreen() {
  lcdPrint2(lang("Terminal beachten", "Use terminal"),
            lang("Abbruch => C", "Cancel => C"));
}

bool cancelSumupPayment() {
  if (!sumupPaymentPending) {
    showNormalScreen();
    return true;
  }
  if (!wifiConnected) {
    setSumupStatus(lang("SumUp Abbruch nicht moeglich: WLAN nicht verbunden.",
                        "SumUp cancel unavailable: WiFi not connected."),
                   "payment_id=" + sumupPendingPaymentId);
    showTemporaryMessage(lang("Abbruch Fehler", "Cancel error"),
                         lang("WLAN offline", "WiFi offline"), 2000);
    showSumupPaymentPendingScreen();
    return false;
  }
  if (!isSumupConfigured()) {
    setSumupStatus(lang("SumUp Abbruch nicht moeglich: nicht konfiguriert.",
                        "SumUp cancel unavailable: not configured."),
                   "payment_id=" + sumupPendingPaymentId);
    showTemporaryMessage(lang("Abbruch Fehler", "Cancel error"),
                         lang("Config fehlt", "Config missing"), 2000);
    showSumupPaymentPendingScreen();
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = sumupServerUrl;
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  url += "/cancel";
  if (!http.begin(client, url)) {
    setSumupStatus(lang("SumUp Abbruch URL ungueltig.", "Invalid SumUp cancel URL."), url);
    showTemporaryMessage(lang("Abbruch Fehler", "Cancel error"),
                         lang("URL ungueltig", "Invalid URL"), 2000);
    showSumupPaymentPendingScreen();
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + sumupApiToken);

  String paymentId = sumupPendingPaymentId;
  String body = "{\"machine_id\":\"" + jsonEscape(sumupMachineId) +
                "\",\"payment_id\":\"" + jsonEscape(paymentId) +
                "\",\"source\":\"vending-machine\"}";
  appendSumupLog(lang("Breche SumUp Zahlung ab", "Cancelling SumUp payment"),
                 "POST " + url + " | payment_id=" + paymentId);
  int statusCode = http.POST(body);
  String response = http.getString();
  http.end();

  if (statusCode < 200 || statusCode >= 300) {
    setSumupStatus(lang("SumUp Abbruch fehlgeschlagen: ", "SumUp cancel failed: ") + String(statusCode),
                   truncateForLog(response, 240));
    showTemporaryMessage(lang("Abbruch Fehler", "Cancel error"),
                         lang("Terminal aktiv", "Terminal active"), 2000);
    showSumupPaymentPendingScreen();
    return false;
  }

  String status = jsonGetString(response, "status");
  bool ok = response.length() == 0 ||
            jsonGetBool(response, "ok", false) ||
            status == "canceled" ||
            status == "cancelled" ||
            status == "failed" ||
            status == "expired";
  String message = jsonGetString(response, "message");
  if (!ok) {
    setSumupStatus(message.length() > 0 ? message : lang("SumUp Abbruch wurde nicht bestaetigt.",
                                                         "SumUp cancel was not confirmed."),
                   truncateForLog(response, 240));
    showTemporaryMessage(lang("Abbruch Fehler", "Cancel error"),
                         lang("Nicht bestaetigt", "Not confirmed"), 2000);
    showSumupPaymentPendingScreen();
    return false;
  }

  sumupPaymentPending = false;
  sumupPendingPaymentId = "";
  sumupPendingAmountCents = 0;
  sumupPendingVendShaftIndex = -1;
  sumupPendingStartedMs = 0;
  sumupNextPollMs = 0;
  setSumupStatus(message.length() > 0 ? message : lang("SumUp Zahlung abgebrochen.",
                                                       "SumUp payment cancelled."),
                 "payment_id=" + paymentId + (status.length() > 0 ? " | status=" + status : ""));
  showTemporaryMessage(lang("Kauf abgebrochen", "Purchase canceled"),
                       lang("Terminal bereit", "Terminal ready"), 1600);
  showNormalScreen();
  return true;
}

void pollSumupPaymentStatus() {
  if (!sumupPaymentPending || !wifiConnected || !isSumupConfigured()) return;
  unsigned long now = millis();
  if (now < sumupNextPollMs) return;
  if ((now - sumupPendingStartedMs) > sumupTimeoutMs) {
    sumupPaymentPending = false;
    sumupPendingVendShaftIndex = -1;
    setSumupStatus(lang("SumUp Timeout.", "SumUp timeout."),
                   "payment_id=" + sumupPendingPaymentId);
    showTemporaryMessage(lang("Zahlung offen", "Payment timeout"), formatCentsToMoney(sumupPendingAmountCents), 2000);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = sumupServerUrl;
  if (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  url += "/status?machine_id=" + sumupMachineId + "&payment_id=" + sumupPendingPaymentId;
  if (!http.begin(client, url)) {
    setSumupStatus(lang("SumUp Status URL ungueltig.", "Invalid SumUp status URL."), url);
    sumupNextPollMs = now + sumupPollIntervalMs;
    return;
  }

  http.addHeader("Authorization", "Bearer " + sumupApiToken);
  int statusCode = http.GET();
  String response = http.getString();
  http.end();
  sumupNextPollMs = now + sumupPollIntervalMs;
  appendSumupLog(lang("Pruefe SumUp Status", "Checking SumUp status"),
                 "GET " + url + " | http=" + String(statusCode));

  if (statusCode < 200 || statusCode >= 300) {
    setSumupStatus(lang("SumUp Statusfehler: ", "SumUp status error: ") + String(statusCode),
                   truncateForLog(response, 240));
    return;
  }

  String status = jsonGetString(response, "status");
  String message = jsonGetString(response, "message");
  uint32_t amountCents = jsonGetUInt(response, "amount_cents", sumupPendingAmountCents);

  if (status == "successful" || status == "paid" || status == "success") {
    sumupPaymentPending = false;
    int vendShaftIndex = sumupPendingVendShaftIndex;
    sumupPendingVendShaftIndex = -1;
    creditCents += amountCents;
    setSumupStatus(message.length() > 0 ? message : lang("SumUp Zahlung erfolgreich.", "SumUp payment successful."),
                   "payment_id=" + sumupPendingPaymentId + " | amount_cents=" + String(amountCents));
    if (vendShaftIndex >= 0 && vendShaftIndex < productShaftCount) {
      if (!vendProductAtIndex(vendShaftIndex, "SumUp")) {
        showTemporaryMessage(lang("Zahlung ok", "Payment ok"), formatCentsToMoney(amountCents), 2000);
        showNormalScreen();
      }
    } else {
      showTemporaryMessage(lang("Guthaben aufgeladen", "Credit loaded"), formatCentsToMoney(amountCents), 2000);
      showNormalScreen();
    }
  } else if (status == "failed" || status == "canceled" || status == "cancelled" || status == "expired") {
    sumupPaymentPending = false;
    sumupPendingVendShaftIndex = -1;
    setSumupStatus(message.length() > 0 ? message : lang("SumUp Zahlung fehlgeschlagen.", "SumUp payment failed."),
                   "payment_id=" + sumupPendingPaymentId + " | status=" + status);
    showTemporaryMessage(lang("Zahlung fehlgeschl.", "Payment failed"), formatCentsToMoney(amountCents), 2000);
    showNormalScreen();
  } else {
    setSumupStatus(message.length() > 0 ? message : lang("SumUp wartet auf Zahlung.", "SumUp waiting for payment."),
                   "payment_id=" + sumupPendingPaymentId + " | status=" + status);
  }
}

String getLastMotorTestMessage() {
  return lastMotorTestMessage.length() > 0 ? lastMotorTestMessage
                                           : lang("Noch kein Motortest ausgefuehrt.", "No motor test executed yet.");
}

String getLastShaftActionMessage() {
  return lastShaftActionMessage.length() > 0 ? lastShaftActionMessage
                                             : lang("Noch keine Schachtaktion ausgefuehrt.", "No slot action executed yet.");
}

String getLastDoorLockMessage() {
  return lastDoorLockMessage.length() > 0 ? lastDoorLockMessage
                                          : lang("Magnetschloss bereit.", "Magnet lock ready.");
}

void clearPendingProductSelection() {
  pendingProductRow = '\0';
  pendingProductSelectionMs = 0;
}

void showPendingProductSelection() {
  if (pendingProductRow == '\0') {
    showNormalScreen();
    return;
  }

  lcdPrint2(lang("Reihe, dann Fach", "Row, then slot"), String(pendingProductRow) + "_");
}

String renderMotorSelect(const String& fieldName, uint8_t selectedMotor, const String& inputId) {
  String html = "<select id='" + inputId + "' name='" + fieldName + "'>";
  html += "<option value='0'";
  if (selectedMotor == 0) html += " selected";
  html += ">" + lang("Nicht zugewiesen", "Not assigned") + "</option>";

  for (int i = 1; i <= stepperMotorCount; i++) {
    html += "<option value='" + String(i) + "'";
    if (selectedMotor == i) html += " selected";
    html += ">Motor " + String(i) + "</option>";
  }

  html += "</select>";
  return html;
}

String generateSessionToken() {
  uint32_t r1 = (uint32_t)esp_random();
  uint32_t r2 = (uint32_t)esp_random();
  char buf[32];
  snprintf(buf, sizeof(buf), "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);
  return String(buf);
}

String getCookieValue(const String& cookieHeader, const String& key) {
  String search = key + "=";
  int start = cookieHeader.indexOf(search);
  if (start < 0) return "";

  start += search.length();
  int end = cookieHeader.indexOf(';', start);
  if (end < 0) end = cookieHeader.length();

  return cookieHeader.substring(start, end);
}

bool isWebAuthenticated() {
  if (!server.hasHeader("Cookie")) return false;
  String cookie = server.header("Cookie");
  String token = getCookieValue(cookie, "ESPSESSIONID");
  return token.length() > 0 && token == webSessionToken;
}

void redirectTo(const String& target) {
  server.sendHeader("Location", target, true);
  server.send(302, "text/plain", "");
}

void handleWebLoginPage(const String& errorMsg = "") {
  String html = htmlHeader(lang("Login", "Login"));
  html += "<h2>" + lang("Verkaufsautomat Login", "Vending Machine Login") + "</h2>";
  html += "<p>" + lang("Bitte Service-PIN eingeben.", "Please enter the service PIN.") + "</p>";

  if (errorMsg.length() > 0) {
    html += "<p class='err'>" + errorMsg + "</p>";
  }

  html += "<form method='POST' action='/login'>";
  html += "<div class='row'><input type='password' name='pin' maxlength='8' placeholder='" + lang("Service-PIN", "Service PIN") + "'></div>";
  html += "<div class='row'><button type='submit'>" + lang("Anmelden", "Login") + "</button></div>";
  html += "</form>";
  html += htmlFooter();

  server.send(200, "text/html; charset=utf-8", html);
}

void handleRoot() {
  if (!isWebAuthenticated()) {
    handleWebLoginPage();
    return;
  }

  uint64_t sdTotalBytes = 0;
  uint64_t sdUsedBytes = 0;
  uint64_t sdFreeBytes = 0;

  if (sdCardMounted) {
    sdTotalBytes = SD.totalBytes();
    sdUsedBytes = SD.usedBytes();
    sdFreeBytes = sdTotalBytes >= sdUsedBytes ? (sdTotalBytes - sdUsedBytes) : 0;
  }

  String html = htmlHeader(lang("Uebersicht", "Overview"));
  html.reserve(7000);
  html += "<h2>" + lang("Verkaufsautomat", "Vendingmachine") + "</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("overview");

  html += "<h3>" + lang("System", "System") + "</h3>";
  html += "<p><span class='label'>Firmware:</span> " + String(FW_VERSION) + "</p>";
  html += "<p><span class='label'>Motor-ESP UART:</span> RX GPIO " + String(motorControllerRxPin) +
          ", TX GPIO " + String(motorControllerTxPin) + "</p>";
  html += "<p><span class='label'>Motor-ESP:</span> " + escapeHtml(lastMotorControllerMessage.length() > 0 ? lastMotorControllerMessage : lang("noch keine Rueckmeldung", "no response yet")) + "</p>";
  html += "<p><span class='label'>" + lang("Muenz-Puls Pin:", "Coin-pulse pin:") + "</span> GPIO " + String(coinPulsePin) + "</p>";
  html += "<p><span class='label'>SD SPI:</span> SCK GPIO " + String(sdCardSckPin) +
          ", MOSI GPIO " + String(sdCardMosiPin) +
          ", MISO GPIO " + String(sdCardMisoPin) +
          ", CS GPIO " + String(sdCardCsPin) + "</p>";
  if (sdCardMounted) {
    html += "<p class='ok'><span class='label'>SD:</span> " + escapeHtml(sdCardStatusMessage) + "</p>";
    html += "<p><span class='label'>" + lang("SD gesamt:", "SD total:") + "</span> " + formatBytes(sdTotalBytes) + "</p>";
    html += "<p><span class='label'>" + lang("SD frei:", "SD free:") + "</span> " + formatBytes(sdFreeBytes) + "</p>";
    html += "<p><span class='label'>" + lang("SD belegt:", "SD used:") + "</span> " + formatBytes(sdUsedBytes) + "</p>";
  } else {
    html += "<p class='err'><span class='label'>SD:</span> " + escapeHtml(sdCardStatusMessage) + "</p>";
  }

  html += "<h3>WiFi</h3>";
  if (wifiSSID.length() == 0) {
    html += "<p class='err'><span class='label'>" + lang("Status:", "Status:") + "</span> " + lang("Keine SSID konfiguriert", "No SSID configured") + "</p>";
  } else if (wifiConnected) {
    html += "<p class='ok'><span class='label'>" + lang("Status:", "Status:") + "</span> " + lang("Verbunden", "Connected") + "</p>";
  } else {
    html += "<p class='err'><span class='label'>" + lang("Status:", "Status:") + "</span> " + lang("Nicht verbunden", "Not connected") + "</p>";
  }

  html += "<p><span class='label'>SSID:</span> " + escapeHtml(wifiSSID) + "</p>";
  html += "<p><span class='label'>DHCP:</span> " + String(wifiDhcp ? lang("Ja", "yes") : lang("Nein", "no")) + "</p>";
  html += "<p><span class='label'>NTP:</span> " + escapeHtml(wifiNtpServer) + "</p>";
  if (wifiConnected) {
    html += "<p><span class='label'>" + lang("Aktuelle IP:", "Current IP:") + "</span> " + WiFi.localIP().toString() + "</p>";
  }
  html += "<p><a class='button-link' href='/wifi'>" + lang("WiFi Einstellungen oeffnen", "Open WiFi settings") + "</a></p>";

  html += "<h3>SumUp</h3>";
  html += "<p><span class='label'>Status:</span> " + escapeHtml(sumupLastMessage.length() > 0 ? sumupLastMessage : lang("nicht initialisiert", "not initialized")) + "</p>";
  html += "<p><span class='label'>" + lang("Aktiv:", "Enabled:") + "</span> " + String(sumupEnabled ? lang("Ja", "yes") : lang("Nein", "no")) + "</p>";
  html += "<p><span class='label'>" + lang("Server:", "Server:") + "</span> " + escapeHtml(sumupServerUrl) + "</p>";
  html += "<p><span class='label'>" + lang("Maschine:", "Machine:") + "</span> " + escapeHtml(sumupMachineId) + "</p>";
  html += "<p><span class='label'>" + lang("Offene Zahlung:", "Pending payment:") + "</span> " + String(sumupPaymentPending ? lang("Ja", "yes") : lang("Nein", "no")) + "</p>";
  html += "<p><a class='button-link' href='/sumup'>SumUp " + lang("Einstellungen oeffnen", "settings") + "</a></p>";

  html += "<h3>" + lang("Status", "Status") + "</h3>";
  html += "<p><span class='label'>" + lang("Guthaben:", "Credit:") + "</span> " + formatCentsToMoney(creditCents) + "</p>";
  if (lastCoinPulseCount > 0) {
    html += "<p><span class='label'>" + lang("Letzte Muenze:", "Last coin:") + "</span> " +
            String(lastCoinPulseCount) + " " + lang("Pulse", "pulses") +
            " (" + formatCentsToMoney(lastCoinValueCents) + ")</p>";
  }
  html += "<p><span class='label'>" + lang("Konfigurierte Schaechte:", "Configurable Slots:") + "</span> " + String(productShaftCount) + "</p>";
  html += "<p><span class='label'>" + lang("Maximal moeglich:", "Maximum possible:") + "</span> " + String(productShaftMaxCount) + "</p>";
  html += "<div class='section-card'>";
  html += "<h3>" + lang("Fronttuer", "Front Door") + "</h3>";
  html += "<p class='inline-note'>" + getLastDoorLockMessage() + "</p>";
  html += "<form method='POST' action='/door/unlock'>";
  html += "<div class='row'><button type='submit'>" + lang("Magnetschloss 5 Sekunden aktivieren", "Activate door lock for 5 seconds") + "</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div class='section-card'>";
  html += "<h3>" + lang("Sprache", "Language") + "</h3>";
  if (lastLanguageMessage.length() > 0) {
    html += "<p class='inline-note'>" + lastLanguageMessage + "</p>";
  }
  html += "<p><span class='label'>" + lang("Aktuell:", "Current:") + "</span> " +
          (currentLanguage == LANG_DE ? "Deutsch" : "English") + "</p>";
  html += "<form method='POST' action='/language/toggle'>";
  html += String("<div class='row'><button type='submit'>") +
          (currentLanguage == LANG_DE ? "Switch to English" : "Auf Deutsch umstellen") +
          "</button></div>";
  html += "</form>";
  html += "</div>";

  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleWifiPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String html = htmlHeader("WiFi");
  html.reserve(5000);
  html += "<h2>WiFi</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("wifi");
  html += "<div class='section-card'>";
  html += "<p class='hint'>" + lang("Aenderungen werden gespeichert und das WLAN wird direkt neu verbunden.",
                                      "Changes are saved and WiFi reconnects immediately.") + "</p>";
  html += "<form method='POST' action='/wifi'>";
  html += "<div class='row'><label class='label' for='wifiSSID'>SSID</label>";
  html += "<input id='wifiSSID' name='wifiSSID' maxlength='64' value='" + escapeHtml(wifiSSID) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiPass'>" + lang("Passwort", "Password") + "</label>";
  html += "<input id='wifiPass' name='wifiPass' maxlength='64' value='" + escapeHtml(wifiPassword) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiDhcp'>DHCP</label>";
  html += "<select id='wifiDhcp' name='wifiDhcp'>";
  html += "<option value='1'" + String(wifiDhcp ? " selected" : "") + ">" + lang("Ja", "Yes") + "</option>";
  html += "<option value='0'" + String(!wifiDhcp ? " selected" : "") + ">" + lang("Nein", "No") + "</option>";
  html += "</select></div>";
  html += "<div class='row'><label class='label' for='wifiIP'>IP</label>";
  html += "<input id='wifiIP' name='wifiIP' value='" + escapeHtml(wifiManualIp) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiSub'>" + lang("Subnetz", "Subnet") + "</label>";
  html += "<input id='wifiSub' name='wifiSub' value='" + escapeHtml(wifiSubnet) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiGw'>Gateway</label>";
  html += "<input id='wifiGw' name='wifiGw' value='" + escapeHtml(wifiGateway) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiDns'>DNS</label>";
  html += "<input id='wifiDns' name='wifiDns' value='" + escapeHtml(wifiDns) + "'></div>";
  html += "<div class='row'><label class='label' for='wifiNtp'>NTP Server</label>";
  html += "<input id='wifiNtp' name='wifiNtp' maxlength='64' value='" + escapeHtml(wifiNtpServer) + "'></div>";
  html += "<div class='row'><button type='submit'>" + lang("WiFi Einstellungen speichern", "Save WiFi settings") + "</button></div>";
  html += "</form>";
  html += "</div>";
  html += htmlFooter();

  server.send(200, "text/html; charset=utf-8", html);
}

void handleEmailPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  bool emailConfigComplete = isEmailConfigComplete();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  server.sendContent(htmlHeader("E-Mail"));
  server.sendContent("<h2>E-Mail</h2>");
  server.sendContent("<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>");
  server.sendContent(renderWebTabs("email"));
  server.sendContent("<div class='section-card'>");
  server.sendContent("<p class='hint'>" + lang("Benachrichtigt bei niedrigem Bestand. Unterstuetzt SMTP und SMTPS.",
                                                "Notifies on low stock. Supports SMTP and SMTPS.") + "</p>");
  if (lastEmailSettingsMessage.length() > 0) {
    server.sendContent("<p class='inline-note'>" + escapeHtml(lastEmailSettingsMessage) + "</p>");
  }
  if (lastEmailSendMessage.length() > 0) {
    server.sendContent("<p class='inline-note'>" + escapeHtml(lastEmailSendMessage) + "</p>");
  }
  server.sendContent("<form method='POST' action='/email'>");
  server.sendContent("<div class='row'><label class='label' for='mailEn'>" + lang("Benachrichtigung aktiv", "Notifications enabled") + "</label>");
  server.sendContent("<select id='mailEn' name='mailEn'>");
  server.sendContent("<option value='1'" + String(emailNotifyEnabled ? " selected" : "") + ">" + lang("Ja", "Yes") + "</option>");
  server.sendContent("<option value='0'" + String(!emailNotifyEnabled ? " selected" : "") + ">" + lang("Nein", "No") + "</option>");
  server.sendContent("</select></div>");
  server.sendContent("<div class='row'><label class='label' for='mailProto'>" + lang("Protokoll", "Protocol") + "</label>");
  server.sendContent("<select id='mailProto' name='mailProto'>");
  server.sendContent("<option value='smtp'" + String(emailProtocol == "smtp" ? " selected" : "") + ">SMTP</option>");
  server.sendContent("<option value='smtps'" + String(emailProtocol == "smtps" ? " selected" : "") + ">SMTPS</option>");
  server.sendContent("</select></div>");
  server.sendContent("<div class='row'><label class='label' for='mailHost'>SMTP Host</label>");
  server.sendContent("<input id='mailHost' name='mailHost' maxlength='64' value='" + escapeHtml(emailHost) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailPort'>Port</label>");
  server.sendContent("<input id='mailPort' name='mailPort' value='" + String(emailPort) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailUser'>" + lang("Benutzername", "Username") + "</label>");
  server.sendContent("<input id='mailUser' name='mailUser' maxlength='64' value='" + escapeHtml(emailUsername) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailPass'>" + lang("Passwort", "Password") + "</label>");
  server.sendContent("<input id='mailPass' name='mailPass' maxlength='64' type='password' value='" + escapeHtml(emailPassword) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailFrom'>From</label>");
  server.sendContent("<input id='mailFrom' name='mailFrom' maxlength='96' value='" + escapeHtml(emailFrom) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailTo'>To</label>");
  server.sendContent("<input id='mailTo' name='mailTo' maxlength='96' value='" + escapeHtml(emailTo) + "'></div>");
  server.sendContent("<div class='row'><label class='label' for='mailThres'>" + lang("Bestandsschwellwert", "Stock threshold") + "</label>");
  server.sendContent("<input id='mailThres' name='mailThres' value='" + String(emailLowStockThreshold) + "'></div>");
  server.sendContent("<div class='row'><button type='submit'>" + lang("E-Mail Einstellungen speichern", "Save email settings") + "</button></div>");
  server.sendContent("</form>");
  if (emailConfigComplete) {
    server.sendContent("<form method='POST' action='/email/test'>");
    server.sendContent("<div class='row'><button type='submit'>" + lang("Test E-Mail senden", "Send test email") + "</button></div>");
    server.sendContent("</form>");
    server.sendContent("<form method='POST' action='/email/cashbook'>");
    server.sendContent("<div class='row'><button type='submit'>" + lang("Kassenbuch per E-Mail senden", "Send cashbook by email") + "</button></div>");
    server.sendContent("</form>");
  } else {
    server.sendContent("<p class='hint'>" + lang("Fuer den Testversand muessen Host, Port, From und To gesetzt sein.",
                                                  "For test sending, host, port, from and to must be set.") + "</p>");
  }
  server.sendContent("</div>");
  server.sendContent(htmlFooter());
}

void handleSumupPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String html = htmlHeader("SumUp");
  String sumupLog = readSumupLogTail(5000);
  html += "<h2>SumUp</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("sumup");
  html += "<div class='section-card'>";
  html += "<p><strong>" + lang("SumUp Bridge verfuegbar", "SumUp bridge available") + "</strong></p>";
  html += "<p class='hint'>" + lang("Wir bieten eine SumUp Bridge unter https://sumup.kreativwelt3d.de an. Dort koennen sich Nutzer registrieren, ihren SumUp API-Key hinterlegen und Terminale fuer den Automaten anlegen.",
                                      "We provide a SumUp bridge at https://sumup.kreativwelt3d.de. Users can register there, store their SumUp API key and create terminals for the machine.") + "</p>";
  html += "<p><a class='button-link' href='https://sumup.kreativwelt3d.de' target='_blank' rel='noopener noreferrer'>https://sumup.kreativwelt3d.de</a></p>";
  html += "</div>";
  html += "<p class='hint'>" + lang("Der Automat spricht per HTTPS mit deinem eigenen Server. Dieser Server uebernimmt die eigentliche Kommunikation mit der SumUp API und dem Virtual Solo.",
                                      "The machine talks to your own server over HTTPS. That server handles the actual communication with the SumUp API and the Virtual Solo.") + "</p>";
  html += "<p class='hint'>" + lang("Keypad Bedienung: A startet die direkte Aufladung, danach gibst du den Betrag in Euro ueber 0..9 ein und bestaetigst erneut mit A. C loescht die letzte Ziffer, D bricht ab. Wenn bereits ein Produkt mit Restbetrag offen ist, startet A stattdessen genau diese Kartenzahlung.",
                                      "Keypad flow: A opens direct top-up, then you enter the amount in euros with 0..9 and confirm with A again. C deletes the last digit, D cancels. If a product is already waiting for the missing balance, A starts that exact card payment instead.") + "</p>";
  html += "<p class='inline-note'>" + escapeHtml(sumupLastMessage.length() > 0 ? sumupLastMessage : lang("Noch keine SumUp Aktion ausgefuehrt.", "No SumUp action executed yet.")) + "</p>";
  String sumupFormServerUrl = sumupServerUrl.length() > 0 ? sumupServerUrl : String(defaultSumupServerUrl);
  String sumupEndpointBaseUrl = sumupFormServerUrl;
  if (sumupEndpointBaseUrl.endsWith("/")) {
    sumupEndpointBaseUrl.remove(sumupEndpointBaseUrl.length() - 1);
  }
  html += "<form method='POST' action='/sumup'>";
  html += "<div class='row'><label class='label' for='sumEn'>" + lang("SumUp aktiv", "SumUp enabled") + "</label>";
  html += "<select id='sumEn' name='sumEn'>";
  html += "<option value='0'" + String(sumupEnabled ? "" : " selected") + ">" + lang("Nein", "No") + "</option>";
  html += "<option value='1'" + String(sumupEnabled ? " selected" : "") + ">" + lang("Ja", "Yes") + "</option>";
  html += "</select></div>";
  html += "<div class='row'><label class='label' for='sumUrl'>" + lang("Server Basis-URL", "Server base URL") + "</label>";
  html += "<input id='sumUrl' name='sumUrl' value='" + escapeHtml(sumupFormServerUrl) + "' placeholder='https://dein-server.de/api/sumup'></div>";
  html += "<div class='row'><label class='label' for='sumTok'>" + lang("Bearer Token", "Bearer token") + "</label>";
  html += "<input id='sumTok' name='sumTok' value='" + escapeHtml(sumupApiToken) + "' required></div>";
  html += "<div class='row'><label class='label' for='sumMach'>" + lang("Automat / Machine ID", "Machine ID") + "</label>";
  html += "<input id='sumMach' name='sumMach' value='" + escapeHtml(sumupMachineId) + "'></div>";
  html += "<div class='row'><label class='label' for='sumCur'>" + lang("Waehrung", "Currency") + "</label>";
  html += "<input id='sumCur' name='sumCur' value='" + escapeHtml(sumupCurrency) + "' maxlength='3'></div>";
  html += "<div class='row'><label class='label' for='sumPoll'>" + lang("Polling Intervall (Sek.)", "Polling interval (sec)") + "</label>";
  html += "<input id='sumPoll' name='sumPoll' value='" + String(sumupPollIntervalMs / 1000UL) + "'></div>";
  html += "<div class='row'><label class='label' for='sumTout'>" + lang("Timeout (Sek.)", "Timeout (sec)") + "</label>";
  html += "<input id='sumTout' name='sumTout' value='" + String(sumupTimeoutMs / 1000UL) + "'></div>";
  html += "<div class='row'><button type='submit'>" + lang("SumUp Einstellungen speichern", "Save SumUp settings") + "</button></div>";
  html += "</form>";
  html += "<div class='section-card'>";
  html += "<h3>" + lang("Erwartete Server-Endpunkte", "Expected server endpoints") + "</h3>";
  html += "<p class='hint'>POST " + escapeHtml(sumupEndpointBaseUrl) + "/start</p>";
  html += "<p class='hint'>GET " + escapeHtml(sumupEndpointBaseUrl) + "/status?machine_id=...&payment_id=...</p>";
  html += "<p class='hint'>POST " + escapeHtml(sumupEndpointBaseUrl) + "/cancel</p>";
  html += "</div>";
  html += "<div class='section-card'>";
  html += "<h3>" + lang("SumUp Log", "SumUp log") + "</h3>";
  html += "<p class='hint'>" + lang("Datei auf SD:", "File on SD:") + " " + String(sumupLogPath) + "</p>";
  html += "<p class='hint'>" + lang("Es werden die neuesten Eintraege angezeigt.", "The newest entries are shown.") + "</p>";
  html += "<pre style='white-space:pre-wrap; overflow:auto; max-height:360px; padding:12px; border-radius:10px; background:#111; color:#f5f5f5;'>" + escapeHtml(sumupLog) + "</pre>";
  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleUpdatePage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String startReason;
  bool canInstall = canStartFirmwareUpdate(startReason);
  bool updateNewer = isCachedFirmwareUpdateNewer();

  String html = htmlHeader("Update");
  html.reserve(6500);
  html += "<h2>Update</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("update");

  html += "<div class='section-card'>";
  html += "<h3>" + lang("Firmware", "Firmware") + "</h3>";
  html += "<p><span class='label'>" + lang("Installiert:", "Installed:") + "</span> " + String(FW_VERSION) + "</p>";
  html += "<p><span class='label'>Manifest:</span> " + escapeHtml(String(firmwareUpdateManifestUrl)) + "</p>";
  html += "<p><span class='label'>Status:</span> " +
          escapeHtml(lastUpdateMessage.length() > 0 ? lastUpdateMessage : lang("Noch keine Update-Pruefung ausgefuehrt.", "No update check has been run yet.")) + "</p>";
  if (lastUpdateDetail.length() > 0) {
    html += "<p class='inline-note'>" + escapeHtml(lastUpdateDetail) + "</p>";
  }
  if (!canInstall) {
    html += "<p class='err'>" + escapeHtml(startReason) + "</p>";
  }
  html += "<form method='POST' action='/update/check'>";
  html += "<div class='row'><button type='submit'>" + lang("Nach Updates suchen", "Check for updates") + "</button></div>";
  html += "</form>";
  html += "</div>";

  if (cachedUpdateInfo.valid) {
    html += "<div class='section-card'>";
    html += "<h3>" + lang("Verfuegbares Paket", "Available package") + "</h3>";
    html += "<p><span class='label'>Version:</span> " + escapeHtml(cachedUpdateInfo.version) + "</p>";
    html += "<p><span class='label'>" + lang("Groesse:", "Size:") + "</span> " +
            (cachedUpdateInfo.size > 0 ? formatBytes(cachedUpdateInfo.size) : lang("unbekannt", "unknown")) + "</p>";
    html += "<p><span class='label'>URL:</span> " + escapeHtml(cachedUpdateInfo.firmwareUrl) + "</p>";
    if (cachedUpdateInfo.sha256.length() > 0) {
      html += "<p><span class='label'>SHA-256:</span> " + escapeHtml(cachedUpdateInfo.sha256) + "</p>";
    }
    if (cachedUpdateInfo.md5.length() > 0) {
      html += "<p><span class='label'>MD5:</span> " + escapeHtml(cachedUpdateInfo.md5) + "</p>";
    }
    if (cachedUpdateInfo.notes.length() > 0) {
      html += "<p class='inline-note'>" + escapeHtml(cachedUpdateInfo.notes) + "</p>";
    }
    if (updateNewer) {
      html += "<form method='POST' action='/update/install'>";
      html += "<div class='row'><button type='submit'";
      if (!canInstall) {
        html += " disabled";
      }
      html += ">" + lang("Update installieren", "Install update") + "</button></div>";
      html += "</form>";
    } else {
      html += "<p class='ok'>" + lang("Die installierte Firmware ist aktuell.", "The installed firmware is current.") + "</p>";
    }
    html += "</div>";
  }


  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleUpdateCheckPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  FirmwareUpdateInfo info;
  if (fetchFirmwareUpdateManifest(info)) {
    if (compareVersionStrings(info.version, FW_VERSION) > 0) {
      setUpdateStatus(lang("Update verfuegbar.", "Update available."),
                      "installed=" + String(FW_VERSION) + " available=" + info.version);
    } else {
      setUpdateStatus(lang("Kein neueres Update verfuegbar.", "No newer update available."),
                      "installed=" + String(FW_VERSION) + " available=" + info.version);
    }
  } else {
    clearCachedUpdateInfo();
  }

  redirectTo("/update");
}

void handleUpdateInstallPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String reason;
  if (!canStartFirmwareUpdate(reason)) {
    setUpdateStatus(lang("Update kann nicht gestartet werden.", "Update cannot be started."), reason);
    redirectTo("/update");
    return;
  }

  FirmwareUpdateInfo info;
  if (!fetchFirmwareUpdateManifest(info)) {
    redirectTo("/update");
    return;
  }

  if (compareVersionStrings(info.version, FW_VERSION) <= 0) {
    setUpdateStatus(lang("Kein neueres Update verfuegbar.", "No newer update available."),
                    "installed=" + String(FW_VERSION) + " available=" + info.version);
    redirectTo("/update");
    return;
  }

  String html = htmlHeader("Update");
  html += "<h2>Update</h2>";
  html += "<p class='inline-note'>" + lang("Firmware wird installiert. Der Automat startet danach neu.",
                                           "Firmware is being installed. The machine will restart afterwards.") + "</p>";
  html += "<p><span class='label'>Version:</span> " + escapeHtml(info.version) + "</p>";
  html += "<script>setTimeout(function(){location.href='/update';},25000);</script>";
  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
  delay(500);

  installFirmwareUpdate(info);
}

void handleCoinsPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String currencyLabel = getCurrencyLabel();
  String pulseLabel = lang("Pulse", "Pulses");
  String valueLabel = lang("Wert", "Value");
  String addLabel = lang("Eintrag hinzufuegen", "Add entry");
  String removeLabel = lang("Eintrag entfernen", "Remove entry");
  String minCountMessage = lang("Mindestens ein Eintrag ist erforderlich.", "At least one entry is required.");
  String maxCountMessage = lang("Maximal ", "Maximum ") + String(coinMappingMaxCount) + lang(" Eintraege moeglich.", " entries allowed.");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  server.sendContent(htmlHeader(lang("Muenzkonfiguration", "Coin Configuration")));
  server.sendContent("<h2>" + lang("Muenzkonfiguration", "Coin Configuration") + "</h2>");
  server.sendContent("<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>");
  server.sendContent(renderWebTabs("coins"));
  server.sendContent("<div class='section-card'>");
  server.sendContent("<p><span class='label'>" + lang("Pulseingang Pin:", "Pulse input pin:") + "</span> GPIO " + String(coinPulsePin) + "</p>");
  server.sendContent("<p><span class='label'>" + lang("Aktuelles Guthaben:", "Current credit:") + "</span> " + formatCentsToMoney(creditCents) + "</p>");
  server.sendContent("<p class='hint'>" + lang("Konfigurierbare Mappings: bis zu ", "Configurable mappings: up to ") + String(coinMappingMaxCount) + "</p>");
  server.sendContent("<p class='hint'>" + lang("Jeder Eintrag ordnet eine Pulsanzahl einem Muenzwert zu.", "Each entry maps a pulse count to a coin value.") + "</p>");
  if (lastCoinSettingsMessage.length() > 0) {
    server.sendContent("<p class='inline-note'>" + escapeHtml(lastCoinSettingsMessage) + "</p>");
  }
  server.sendContent("<form method='POST' action='/coins'>");
  server.sendContent("<div class='row'><label class='label' for='currency'>" + lang("Waehrung", "Currency") + "</label>");
  server.sendContent(renderCurrencySelect());
  server.sendContent("</div>");
  server.sendContent("<input type='hidden' id='entryCount' name='entryCount' value='" + String(coinMappingCount) + "'>");
  server.sendContent("<div id='coinMappings' class='coin-map-list'>");
  for (int i = 0; i < coinMappingCount; i++) {
    server.sendContent(renderCoinMappingRow(i + 1, coinMappingPulses[i], coinMappingValuesCents[i]));
  }
  server.sendContent("</div>");
  server.sendContent("<div class='row'><button type='button' id='addCoinMapping'>" + addLabel + "</button></div>");
  server.sendContent("<div class='row'><button type='submit'>" + lang("Muenzwerte speichern", "Save coin values") + "</button></div>");
  server.sendContent("</form>");
  String script = "<script>";
  script += "const coinMappingMaxCount=" + String(coinMappingMaxCount) + ";";
  script += "const coinMappingContainer=document.getElementById('coinMappings');";
  script += "const coinEntryCountInput=document.getElementById('entryCount');";
  script += "function reindexCoinRows(){";
  script += "const rows=coinMappingContainer.querySelectorAll('.coin-map-row');";
  script += "rows.forEach((row,index)=>{";
  script += "const number=index+1;";
  script += "const pulseLabel=row.querySelector('[data-role=\"pulse-label\"]');";
  script += "const pulseInput=row.querySelector('[data-role=\"pulse-input\"]');";
  script += "const valueLabel=row.querySelector('[data-role=\"value-label\"]');";
  script += "const valueInput=row.querySelector('[data-role=\"value-input\"]');";
  script += "pulseLabel.htmlFor='pulse'+number;";
  script += "pulseLabel.textContent='" + escapeHtml(pulseLabel) + "';";
  script += "pulseInput.id='pulse'+number;";
  script += "pulseInput.name='pulse'+number;";
  script += "valueLabel.htmlFor='value'+number;";
  script += "valueLabel.textContent='" + escapeHtml(valueLabel) + " (" + escapeHtml(currencyLabel) + ")';";
  script += "valueInput.id='value'+number;";
  script += "valueInput.name='value'+number;";
  script += "});";
  script += "coinEntryCountInput.value=rows.length;";
  script += "}";
  script += "function buildCoinRow(pulseValue,valueAmount){";
  script += "const wrapper=document.createElement('div');";
  script += "wrapper.className='coin-map-row';";
  script += "wrapper.innerHTML='<div class=\"row\"><label class=\"label\" data-role=\"pulse-label\"></label><input data-role=\"pulse-input\" inputmode=\"numeric\"></div>' + "
            "'<div class=\"row\"><label class=\"label\" data-role=\"value-label\"></label><input data-role=\"value-input\"></div>' + "
            "'<div class=\"row\"><button type=\"button\" class=\"button-link coin-remove\" onclick=\"removeCoinRow(this)\">" + escapeHtml(removeLabel) + "</button></div>';";
  script += "wrapper.querySelector('[data-role=\"pulse-input\"]').value=pulseValue;";
  script += "wrapper.querySelector('[data-role=\"value-input\"]').value=valueAmount;";
  script += "coinMappingContainer.appendChild(wrapper);";
  script += "reindexCoinRows();";
  script += "}";
  script += "window.removeCoinRow=function(button){";
  script += "const rows=coinMappingContainer.querySelectorAll('.coin-map-row');";
  script += "if(rows.length<=1){alert('" + escapeHtml(minCountMessage) + "');return;}";
  script += "button.closest('.coin-map-row').remove();";
  script += "reindexCoinRows();";
  script += "};";
  script += "document.getElementById('addCoinMapping').addEventListener('click',()=>{";
  script += "const rows=coinMappingContainer.querySelectorAll('.coin-map-row');";
  script += "if(rows.length>=coinMappingMaxCount){alert('" + escapeHtml(maxCountMessage) + "');return;}";
  script += "buildCoinRow('', '0.00');";
  script += "});";
  script += "reindexCoinRows();";
  script += "</script>";
  server.sendContent(script);
  server.sendContent("</div>");
  server.sendContent(htmlFooter());
}

void handleShaftsPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  normalizeProductShaftLayout();
  uint8_t activeRows = getProductShaftActiveRowCount();
  String html = htmlHeader(lang("Schachtkonfiguration", "Slot Configuration"));
  html += "<h2>" + lang("Schachtkonfiguration", "Slot Configuration") + "</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("shafts");
  html += "<p class='hint'>" + lang("Schaechte sind jetzt dynamisch bis zur maximalen Motoranzahl. Pro Reihe sind bis zu 8 Schaechte moeglich, pro Schacht kann Motor 2 optional fuer synchronen Lauf gesetzt werden. Auf dem Keypad erfolgt die Auswahl als zweistelliger Code: erst Reihe 1-6, dann Fach 1-8.",
                                      "Slots are now dynamic up to the maximum motor count. Each row can contain up to 8 slots, and each slot can optionally use motor 2 for synced movement. On the keypad, selection uses a two-digit code: first row 1-6, then slot 1-8.") + "</p>";
  html += "<p class='inline-note'>" + lang("Aktiv: ", "Active: ") + String(productShaftCount) + " / " + String(productShaftMaxCount) + " " +
          lang("Schaechte in ", "slots in ") + String(activeRows) + " / " + String(productShaftMaxRowCount) + " " + lang("Reihen.", "rows.") + "</p>";
  html += "<p class='inline-note'>" + escapeHtml(getLastShaftActionMessage()) + "</p>";

  html += "<details class='section-card'>";
  html += "<summary>" + lang("Layout und Grundeinstellungen", "Layout and base settings") + "</summary>";
  html += "<div class='details-body'>";
  html += "<div class='grid'>";

  html += "<div class='motor-card'>";
  html += "<h3>" + lang("Reihen verwalten", "Manage rows") + "</h3>";
  html += "<p class='hint'>" + lang("Neue Reihen starten mit einem leeren Schacht und erweitern den waehlbaren Codebereich fuer das Keypad.",
                                      "New rows start with one empty slot and extend the keypad code range.") + "</p>";
  html += "<form method='POST' action='/shafts/addrow'>";
  html += "<div class='row'><button type='submit'";
  if (activeRows >= productShaftMaxRowCount || productShaftCount >= productShaftMaxCount) {
    html += " disabled";
  }
  html += ">" + lang("Reihe hinzufuegen", "Add row") + "</button></div>";
  html += "</form>";
  html += "<form method='POST' action='/shafts/removerow'>";
  html += "<div class='row'><button type='submit'>" + lang("Letzte Reihe entfernen", "Remove last row") + "</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div class='motor-card'>";
  html += "<h3>" + lang("Schrittmotor Microsteps", "Stepper microsteps") + "</h3>";
  html += "<p class='hint'>" + lang("Die Auswurfschritte pro Umdrehung werden automatisch berechnet: 200 x Microsteps.",
                                     "Eject steps per revolution are calculated automatically: 200 x microsteps.") + "</p>";
  html += "<p class='inline-note'>" + lang("Aktuell: ", "Current: ") + String(productShaftMicrosteps) + " microsteps, " +
          String(getConfiguredProductShaftEjectSteps()) + lang(" Schritte/Umdrehung.", " steps/revolution.") + "</p>";
  html += "<form method='POST' action='/shafts/microsteps'>";
  html += "<div class='row'><label class='label' for='microsteps'>" + lang("Microsteps", "Microsteps") + "</label>";
  html += "<select id='microsteps' name='microsteps'>";
  const int supportedCount = sizeof(productShaftMicrostepsSupported) / sizeof(productShaftMicrostepsSupported[0]);
  for (int i = 0; i < supportedCount; i++) {
    uint8_t value = productShaftMicrostepsSupported[i];
    html += "<option value='" + String(value) + "'";
    if (value == productShaftMicrosteps) {
      html += " selected";
    }
    html += ">" + String(value) + "</option>";
  }
  html += "</select></div>";
  html += "<div class='row'><button type='submit'>" + lang("Microsteps speichern", "Save microsteps") + "</button></div>";
  html += "</form>";
  html += "</div>";

  html += "<div class='motor-card'>";
  html += "<h3>" + lang("Schaechte verwalten", "Manage slots") + "</h3>";
  html += "<p class='hint'>" + lang("Fuege Schaechte jetzt direkt mit Plus oder Minus in der jeweiligen Reihe hinzu oder entferne sie dort wieder.",
                                      "Slots are now added or removed directly within the respective row using plus or minus.") + "</p>";
  html += "</div>";

  html += "</div>";
  html += "</div>";
  html += "</details>";

  for (int row = 0; row < activeRows; row++) {
    int slotCount = productShaftRowSlotCount[row];
    html += "<details class='section-card'";
    if (row == 0) {
      html += " open";
    }
    html += ">";
    html += "<summary>" + lang("Reihe ", "Row ") + String(row + 1) + " - " + String(slotCount) + "/" + String(productShaftSlotsPerRow) + " " + lang("Schaechte", "slots") + "</summary>";
    html += "<div class='details-body'>";
    html += "<div class='actions'>";
    html += "<span class='muted'>" + lang("Codes: ", "Codes: ") + String(row + 1) + "1-" + String(row + 1) + String(slotCount) + "</span>";
    html += "<form method='POST' action='/shafts/add'>";
    html += "<input type='hidden' name='row' value='" + String(row + 1) + "'>";
    html += "<button type='submit'";
    if (slotCount >= productShaftSlotsPerRow || productShaftCount >= productShaftMaxCount) {
      html += " disabled";
    }
    html += ">+</button>";
    html += "</form>";
    html += "<form method='POST' action='/shafts/remove'>";
    html += "<input type='hidden' name='row' value='" + String(row + 1) + "'>";
    html += "<button type='submit'";
    if (slotCount <= 1 && activeRows <= 1) {
      html += " disabled";
    }
    html += ">-</button>";
    html += "</form>";
    if (slotCount < productShaftSlotsPerRow && productShaftCount < productShaftMaxCount) {
      html += "<span class='muted'>" + lang("Plus fuegt am Reihenende hinzu.", "Plus adds at the end of the row.") + "</span>";
    }
    html += "</div>";
    html += "<div class='grid'>";

    for (int slot = 0; slot < slotCount; slot++) {
      int i = getProductShaftIndexForRowSlot(row, slot);
      if (i < 0 || i >= productShaftCount) {
        continue;
      }
      html += "<div class='motor-card'>";
      html += "<h3>" + getProductShaftLabel(i) + "</h3>";
      html += "<p><span class='label'>" + lang("Code:", "Code:") + "</span> " + getProductShaftCode(i) + "</p>";
      if (productShaftName[i].length() > 0) {
        html += "<p><span class='label'>" + lang("Produkt:", "Product:") + "</span> " + escapeHtml(productShaftName[i]) + "</p>";
      }
      html += "<p><span class='label'>" + lang("Bestand:", "Stock:") + "</span> " + String(productShaftQuantity[i]) + "/" + String(productShaftCapacity[i]) + "</p>";
      html += "<form method='POST' action='/shafts/save'>";
      html += "<input type='hidden' name='shaft' value='" + String(i + 1) + "'>";
      html += "<div class='row'><label class='label' for='name" + String(i + 1) + "'>" + lang("Produktname", "Product name") + "</label>";
      html += "<input id='name" + String(i + 1) + "' name='name' maxlength='32' value='" + escapeHtml(productShaftName[i]) + "'>";
      html += "</div>";
      html += "<div class='row'><label class='label' for='shaftP" + String(i + 1) + "'>" + lang("Motor 1", "Motor 1") + "</label>";
      html += renderMotorSelect("motor1", productShaftPrimaryMotor[i], "shaftP" + String(i + 1));
      html += "</div>";
      html += "<div class='row'><label class='label' for='shaftS" + String(i + 1) + "'>" + lang("Motor 2 synchron (optional)", "Motor 2 synced (optional)") + "</label>";
      html += renderMotorSelect("motor2", productShaftSecondaryMotor[i], "shaftS" + String(i + 1));
      html += "</div>";
      html += "<div class='row'><label class='label' for='price" + String(i + 1) + "'>" + lang("Preis", "Price") + " (" + getCurrencyLabel() + ")</label>";
      html += "<input id='price" + String(i + 1) + "' name='price' value='" + formatCentsToMoney(productShaftPriceCents[i]).substring(0, formatCentsToMoney(productShaftPriceCents[i]).length() - (getCurrencyLabel().length() + 1)) + "'>";
      html += "</div>";
      html += "<div class='row'><label class='label' for='capacity" + String(i + 1) + "'>" + lang("Kapazitaet", "Capacity") + "</label>";
      html += "<input id='capacity" + String(i + 1) + "' name='capacity' value='" + String(productShaftCapacity[i]) + "'>";
      html += "</div>";
      html += "<div class='row'><label class='label' for='quantity" + String(i + 1) + "'>" + lang("Aktuelle Fuellmenge", "Current fill level") + "</label>";
      html += "<input id='quantity" + String(i + 1) + "' name='quantity' value='" + String(productShaftQuantity[i]) + "'>";
      html += "</div>";
      html += "<div class='row'><button type='submit'>" + lang("Zuordnung speichern", "Save assignment") + "</button></div>";
      html += "</form>";
      html += "<form method='POST' action='/shafts/eject'>";
      html += "<input type='hidden' name='shaft' value='" + String(i + 1) + "'>";
      html += "<div class='row'><button type='submit'>" + lang("Manuell auswerfen", "Manual eject") + "</button></div>";
      html += "</form>";
      html += "</div>";
    }

    html += "</div>";
    html += "</div>";
    html += "</details>";
  }

  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleCashbookPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String html = htmlHeader(lang("Kassenbuch", "Cashbook"));
  html += "<h2>" + lang("Kassenbuch", "Cashbook") + "</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("cashbook");

  if (!sdCardMounted) {
    html += "<p class='err'>" + lang("SD Karte nicht verfuegbar.", "SD card not available.") + "</p>";
    html += htmlFooter();
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }

  if (!ensureCashbookCsvExists()) {
    html += "<p class='err'>" + lang("CSV konnte nicht erstellt oder gelesen werden.", "CSV could not be created or read.") + "</p>";
    html += htmlFooter();
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }

  File file = SD.open(cashbookCsvPath, FILE_READ);
  if (!file) {
    html += "<p class='err'>" + lang("Kassenbuch konnte nicht geoeffnet werden.", "Cashbook could not be opened.") + "</p>";
    html += htmlFooter();
    server.send(200, "text/html; charset=utf-8", html);
    return;
  }

  html += "<p class='hint'>" + lang("CSV Datei auf SD:", "CSV file on SD:") + " " + String(cashbookCsvPath) + "</p>";
  html += "<div class='section-card'>";
  html += "<table><thead><tr>";
  html += "<th>" + lang("Uhrzeit", "Time") + "</th>";
  html += "<th>" + lang("Artikelname", "Article") + "</th>";
  html += "<th>" + lang("Kosten", "Cost") + "</th>";
  html += "<th>" + lang("Bezahlart", "Payment method") + "</th>";
  html += "</tr></thead><tbody>";

  bool hasRows = false;
  bool isHeader = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (isHeader) {
      isHeader = false;
      continue;
    }

    hasRows = true;
    html += "<tr>";
    html += "<td>" + escapeHtml(getCsvField(line, 0)) + "</td>";
    html += "<td>" + escapeHtml(getCsvField(line, 1)) + "</td>";
    html += "<td>" + escapeHtml(getCsvField(line, 2)) + "</td>";
    html += "<td>" + escapeHtml(getCsvField(line, 3)) + "</td>";
    html += "</tr>";
  }
  file.close();

  if (!hasRows) {
    html += "<tr><td colspan='4'>" + lang("Noch keine Verkaeufe protokolliert.", "No sales recorded yet.") + "</td></tr>";
  }

  html += "</tbody></table>";
  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleTestsPage() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  String html = htmlHeader(lang("Motortests", "Motor Tests"));
  html += "<h2>" + lang("Motortests", "Motor Tests") + "</h2>";
  html += "<div class='actions'><a class='button-link' href='/logout'>" + lang("Logout", "Logout") + "</a></div>";
  html += renderWebTabs("tests");
  html += "<p class='inline-note'><span class='label'>Motor-ESP UART:</span> RX GPIO " + String(motorControllerRxPin) +
          ", TX GPIO " + String(motorControllerTxPin) + "</p>";
  html += "<p class='hint'>" + lang("Die Tests werden per UART an den Motor-ESP uebergeben.",
                                      "Tests are sent over UART to the motor ESP.") + "</p>";
  html += "<p class='inline-note'>" + getLastMotorTestMessage() + "</p>";
  html += "<div class='grid'>";

  for (int i = 0; i < stepperMotorCount; i++) {
    html += "<div class='motor-card'>";
    html += "<h3>Motor " + String(i + 1) + "</h3>";
    html += "<form method='POST' action='/tests/motor'>";
    html += "<input type='hidden' name='motor' value='" + String(i + 1) + "'>";
    html += "<div class='row'><label class='label' for='steps" + String(i + 1) + "'>" + lang("Schritte", "Steps") + "</label>";
    html += "<input id='steps" + String(i + 1) + "' name='steps' value='" + String(stepperTestDefaultSteps) + "'></div>";
    html += "<div class='row'><label class='label' for='pulse" + String(i + 1) + "'>" + lang("Pulsdauer pro Pegel (us)", "Pulse width per level (us)") + "</label>";
    html += "<input id='pulse" + String(i + 1) + "' name='pulse' value='" + String(stepperTestDefaultPulseUs) + "'></div>";
    html += "<div class='row'><button type='submit'>" + lang("Motor ", "Test motor ") + String(i + 1) + (currentLanguage == LANG_DE ? " testen" : "") + "</button></div>";
    html += "</form>";
    html += "</div>";
  }

  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleCoinSettingsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("currency")) {
    server.send(400, "text/plain; charset=utf-8", "Fehlendes Feld: currency");
    return;
  }

  currentCurrency = server.arg("currency");
  saveCurrencySetting();

  if (!server.hasArg("entryCount")) {
    server.send(400, "text/plain; charset=utf-8", "Fehlendes Feld: entryCount");
    return;
  }

  uint16_t submittedCount = 0;
  if (!parseUnsigned16(server.arg("entryCount"), 1, coinMappingMaxCount, submittedCount)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Anzahl fuer entryCount");
    return;
  }

  bool usedPulseCounts[256] = {false};
  coinMappingCount = (uint8_t)submittedCount;

  for (int i = 0; i < coinMappingCount; i++) {
    String pulseArgName = "pulse" + String(i + 1);
    String valueArgName = "value" + String(i + 1);
    if (!server.hasArg(pulseArgName) || !server.hasArg(valueArgName)) {
      server.send(400, "text/plain; charset=utf-8", "Fehlende Felder fuer Eintrag " + String(i + 1));
      return;
    }

    uint16_t pulseValue = 0;
    if (!parseUnsigned16(server.arg(pulseArgName), 1, 255, pulseValue)) {
      server.send(400, "text/plain; charset=utf-8", "Ungueltige Pulsanzahl fuer " + pulseArgName);
      return;
    }
    if (usedPulseCounts[pulseValue]) {
      server.send(400, "text/plain; charset=utf-8", "Doppelte Pulsanzahl: " + String(pulseValue));
      return;
    }
    usedPulseCounts[pulseValue] = true;

    uint16_t cents = 0;
    if (!parseEuroToCents(server.arg(valueArgName), cents)) {
      server.send(400, "text/plain; charset=utf-8", "Ungueltiger Betrag fuer " + valueArgName);
      return;
    }

    coinMappingPulses[i] = (uint8_t)pulseValue;
    coinMappingValuesCents[i] = cents;
  }

  sortCoinMappingsByPulseCount();
  saveCoinSettings();
  lastCoinSettingsMessage = lang("Muenz-Mappings und Waehrung gespeichert.", "Coin mappings and currency saved.");
  redirectTo("/coins");
}

void handleMotorTestPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("motor") || !server.hasArg("steps") || !server.hasArg("pulse")) {
    server.send(400, "text/plain; charset=utf-8", "Felder motor, steps und pulse sind erforderlich.");
    return;
  }

  uint16_t motorNumber = 0;
  uint16_t steps = 0;
  uint16_t pulseUs = 0;

  if (!parseUnsigned16(server.arg("motor"), 1, stepperMotorCount, motorNumber)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Motornummer.");
    return;
  }

  if (!parseUnsigned16(server.arg("steps"), 1, stepperTestMaxSteps, steps)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Schrittzahl.");
    return;
  }

  if (!parseUnsigned16(server.arg("pulse"), stepperTestMinPulseUs, stepperTestMaxPulseUs, pulseUs)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Pulsdauer.");
    return;
  }

  runStepperMotorTest((int)motorNumber - 1, steps, pulseUs);
  redirectTo("/tests");
}

void handleDoorUnlockPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  pulseDoorLock();
  redirectTo("/");
}

void handleLanguageTogglePost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  currentLanguage = (currentLanguage == LANG_DE) ? LANG_EN : LANG_DE;
  saveLanguageSetting();
  lastLanguageMessage = currentLanguage == LANG_DE ? "Sprache gespeichert: Deutsch." : "Language saved: English.";
  redirectTo("/");
}

void handleWifiSettingsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("wifiSSID") || !server.hasArg("wifiPass") || !server.hasArg("wifiDhcp") ||
      !server.hasArg("wifiIP") || !server.hasArg("wifiSub") || !server.hasArg("wifiGw") ||
      !server.hasArg("wifiDns") || !server.hasArg("wifiNtp")) {
    server.send(400, "text/plain; charset=utf-8", "Fehlende WiFi Felder.");
    return;
  }

  String newSsid = server.arg("wifiSSID");
  String newPass = server.arg("wifiPass");
  String newIp = server.arg("wifiIP");
  String newSubnet = server.arg("wifiSub");
  String newGateway = server.arg("wifiGw");
  String newDns = server.arg("wifiDns");
  String newNtp = server.arg("wifiNtp");
  String dhcpArg = server.arg("wifiDhcp");

  newSsid.trim();
  newPass.trim();
  newIp.trim();
  newSubnet.trim();
  newGateway.trim();
  newDns.trim();
  newNtp.trim();

  if (!(dhcpArg == "0" || dhcpArg == "1")) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger DHCP Wert.");
    return;
  }

  bool newDhcp = dhcpArg == "1";
  if (!newDhcp) {
    if (!isValidIPv4(newIp) || !isValidIPv4(newSubnet) || !isValidIPv4(newGateway) || !isValidIPv4(newDns)) {
      server.send(400, "text/plain; charset=utf-8", "IP, Subnetz, Gateway oder DNS ungueltig.");
      return;
    }
  }

  if (newNtp.length() == 0) {
    newNtp = "pool.ntp.org";
  }

  wifiSSID = newSsid;
  wifiPassword = newPass;
  wifiDhcp = newDhcp;
  wifiManualIp = newIp;
  wifiSubnet = newSubnet;
  wifiGateway = newGateway;
  wifiDns = newDns;
  wifiNtpServer = newNtp;
  saveWifiSettings();
  reconnectWifiAfterSettingsSave("Weboberflaeche");

  redirectTo("/wifi");
}

void handleEmailSettingsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("mailEn") || !server.hasArg("mailProto") || !server.hasArg("mailHost") ||
      !server.hasArg("mailPort") || !server.hasArg("mailUser") || !server.hasArg("mailPass") ||
      !server.hasArg("mailFrom") || !server.hasArg("mailTo") || !server.hasArg("mailThres")) {
    server.send(400, "text/plain; charset=utf-8", "Fehlende E-Mail Felder.");
    return;
  }

  String enabledArg = server.arg("mailEn");
  String protocolArg = server.arg("mailProto");
  String hostArg = server.arg("mailHost");
  String userArg = server.arg("mailUser");
  String passArg = server.arg("mailPass");
  String fromArg = server.arg("mailFrom");
  String toArg = server.arg("mailTo");
  String portArg = server.arg("mailPort");
  String thresholdArg = server.arg("mailThres");

  hostArg.trim();
  userArg.trim();
  passArg.trim();
  fromArg.trim();
  toArg.trim();

  if (!(enabledArg == "0" || enabledArg == "1")) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Aktiv Status.");
    return;
  }

  if (!(protocolArg == "smtp" || protocolArg == "smtps")) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiges Protokoll.");
    return;
  }

  uint16_t portValue = 0;
  if (!parseUnsigned16(portArg, 1, 65535, portValue)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Port.");
    return;
  }

  uint16_t thresholdValue = 0;
  if (!parseUnsigned16(thresholdArg, 0, productShaftMaxCapacity, thresholdValue)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Schwellwert.");
    return;
  }

  emailNotifyEnabled = enabledArg == "1";
  emailProtocol = protocolArg;
  emailHost = hostArg;
  emailPort = portValue;
  emailUsername = userArg;
  emailPassword = passArg;
  emailFrom = fromArg;
  emailTo = toArg;
  emailLowStockThreshold = (uint8_t)thresholdValue;

  for (int i = 0; i < productShaftCount; i++) {
    lowStockNotificationSent[i] = productShaftQuantity[i] <= emailLowStockThreshold;
  }

  saveEmailSettings();
  lastEmailSettingsMessage = lang("E-Mail Einstellungen gespeichert.", "Email settings saved.");
  redirectTo("/email");
}

void handleSumupSettingsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("sumEn") || !server.hasArg("sumUrl") || !server.hasArg("sumTok") ||
      !server.hasArg("sumMach") || !server.hasArg("sumCur") || !server.hasArg("sumPoll") || !server.hasArg("sumTout")) {
    server.send(400, "text/plain; charset=utf-8", "Fehlende SumUp Felder.");
    return;
  }

  sumupEnabled = server.arg("sumEn") == "1";
  sumupServerUrl = server.arg("sumUrl");
  sumupServerUrl.trim();
  sumupApiToken = server.arg("sumTok");
  sumupApiToken.trim();
  sumupMachineId = server.arg("sumMach");
  sumupMachineId.trim();
  sumupCurrency = server.arg("sumCur");
  sumupCurrency.trim();
  sumupCurrency.toUpperCase();
  if (sumupCurrency.length() == 0) {
    sumupCurrency = "EUR";
  }

  if (sumupEnabled) {
    if (sumupServerUrl.length() == 0) {
      server.send(400, "text/plain; charset=utf-8", "Server Basis-URL ist erforderlich.");
      return;
    }
    if (sumupApiToken.length() == 0) {
      server.send(400, "text/plain; charset=utf-8", "Bearer Token ist erforderlich.");
      return;
    }
    if (sumupMachineId.length() == 0) {
      server.send(400, "text/plain; charset=utf-8", "Machine ID ist erforderlich.");
      return;
    }
  }

  uint16_t pollSeconds = 0;
  uint16_t timeoutSeconds = 0;
  if (!parseUnsigned16(server.arg("sumPoll"), 1, 30, pollSeconds)) {
    server.send(400, "text/plain; charset=utf-8", "Polling Intervall muss zwischen 1 und 30 Sekunden liegen.");
    return;
  }
  if (!parseUnsigned16(server.arg("sumTout"), 10, 300, timeoutSeconds)) {
    server.send(400, "text/plain; charset=utf-8", "Timeout muss zwischen 10 und 300 Sekunden liegen.");
    return;
  }
  sumupPollIntervalMs = (uint32_t)pollSeconds * 1000UL;
  sumupTimeoutMs = (uint32_t)timeoutSeconds * 1000UL;

  saveSumupSettings();
  setSumupStatus(lang("SumUp Einstellungen gespeichert.", "SumUp settings saved."),
                 "enabled=" + String(sumupEnabled ? "1" : "0") +
                 " | url=" + sumupServerUrl +
                 " | machine_id=" + sumupMachineId +
                 " | currency=" + sumupCurrency);
  redirectTo("/sumup");
}

void handleEmailTestPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!isEmailConfigComplete()) {
    lastEmailSendMessage = lang("Testversand nicht moeglich. Konfiguration unvollstaendig.",
                                "Test send not possible. Configuration incomplete.");
    redirectTo("/email");
    return;
  }

  String subject = lang("Test E-Mail vom Verkaufsautomat", "Test email from vending machine");
  String body = lang("Dies ist eine Testnachricht aus der Weboberflaeche des Verkaufsautomaten.\n\n",
                     "This is a test message from the vending machine web interface.\n\n");
  body += "SMTP: " + emailHost + ":" + String(emailPort) + "\n";
  body += "Protokoll: " + emailProtocol + "\n";
  body += "Zeit: " + getCurrentTimestamp() + "\n";

  sendEmailMessage(subject, body);
  redirectTo("/email");
}

void handleEmailCashbookPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!isEmailConfigComplete()) {
    lastEmailSendMessage = lang("Kassenbuchversand nicht moeglich. Konfiguration unvollstaendig.",
                                "Cashbook sending not possible. Configuration incomplete.");
    redirectTo("/email");
    return;
  }

  String subject = lang("Kassenbuch Export vom Verkaufsautomat", "Cashbook export from vending machine");
  String body = lang("Das Kassenbuch ist als CSV im Anhang beigefuegt.",
                     "The cashbook is attached as CSV.");
  sendEmailWithAttachment(subject, body, cashbookCsvPath, "kassenbuch.csv", "text/csv");
  redirectTo("/email");
}

void handleShaftSettingsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("shaft") || !server.hasArg("motor1") || !server.hasArg("motor2") || !server.hasArg("price") ||
      !server.hasArg("name") || !server.hasArg("capacity") || !server.hasArg("quantity")) {
    server.send(400, "text/plain; charset=utf-8", "Felder shaft, motor1, motor2, price, name, capacity und quantity sind erforderlich.");
    return;
  }

  uint16_t shaftNumber = 0;
  if (!parseUnsigned16(server.arg("shaft"), 1, productShaftCount, shaftNumber)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Schacht.");
    return;
  }

  int shaftIndex = (int)shaftNumber - 1;
  uint8_t motor1 = 0;
  uint8_t motor2 = 0;
  uint8_t capacity = 0;
  uint8_t quantity = 0;
  uint16_t priceCents = 0;
  String productName = server.arg("name");
  productName.trim();
  if (productName.length() > 32) {
    productName = productName.substring(0, 32);
  }

  if (!parseMotorSelection(server.arg("motor1"), motor1)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Motorzuweisung fuer Motor 1.");
    return;
  }

  if (!parseEuroToCents(server.arg("price"), priceCents)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Preis.");
    return;
  }

  uint16_t capacityValue = 0;
  if (!parseUnsigned16(server.arg("capacity"), productShaftMinCapacity, productShaftMaxCapacity, capacityValue)) {
    server.send(400, "text/plain; charset=utf-8", "Kapazitaet muss zwischen 5 und 9 liegen.");
    return;
  }
  capacity = (uint8_t)capacityValue;

  uint16_t quantityValue = 0;
  if (!parseUnsigned16(server.arg("quantity"), 0, capacity, quantityValue)) {
    server.send(400, "text/plain; charset=utf-8", "Die Fuellmenge muss zwischen 0 und der Kapazitaet liegen.");
    return;
  }
  quantity = (uint8_t)quantityValue;

  if (!parseMotorSelection(server.arg("motor2"), motor2)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Motorzuweisung fuer Motor 2.");
    return;
  }

  if (motor1 == 0) {
    server.send(400, "text/plain; charset=utf-8", "Dieser Schacht benoetigt einen Motor.");
    return;
  }
  if (motor2 > 0 && motor1 == motor2) {
    server.send(400, "text/plain; charset=utf-8", "Motor 1 und Motor 2 muessen unterschiedlich sein.");
    return;
  }

  productShaftPrimaryMotor[shaftIndex] = motor1;
  productShaftSecondaryMotor[shaftIndex] = motor2;
  productShaftPriceCents[shaftIndex] = priceCents;
  productShaftCapacity[shaftIndex] = capacity;
  productShaftQuantity[shaftIndex] = quantity;
  productShaftName[shaftIndex] = productName;
  lowStockNotificationSent[shaftIndex] = productShaftQuantity[shaftIndex] <= emailLowStockThreshold;
  saveProductShaftSettings();

  lastShaftActionMessage = getProductShaftLabel(shaftIndex) + ": " +
                           lang("gespeichert, Code ", "saved, code ") +
                           getProductShaftCode(shaftIndex) + ", " +
                           lang("Preis ", "price ") +
                           formatCentsToMoney(productShaftPriceCents[shaftIndex]) + ", " +
                           lang("Bestand ", "stock ") +
                           String(productShaftQuantity[shaftIndex]) + "/" + String(productShaftCapacity[shaftIndex]) +
                           (productShaftName[shaftIndex].length() > 0 ? ", " + productShaftName[shaftIndex] : "") + ".";
  redirectTo("/shafts");
}

void handleShaftEjectPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("shaft")) {
    server.send(400, "text/plain; charset=utf-8", "Feld shaft ist erforderlich.");
    return;
  }

  uint16_t shaftNumber = 0;
  if (!parseUnsigned16(server.arg("shaft"), 1, productShaftCount, shaftNumber)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Schacht.");
    return;
  }

  runProductShaftEject((int)shaftNumber - 1, getConfiguredProductShaftEjectSteps(), productShaftManualEjectPulseUs);
  redirectTo("/shafts");
}

void handleShaftMicrostepsPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("microsteps")) {
    server.send(400, "text/plain; charset=utf-8", "Feld microsteps ist erforderlich.");
    return;
  }

  uint16_t microstepsValue = 0;
  if (!parseUnsigned16(server.arg("microsteps"), 1, 32, microstepsValue)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltiger Microstep-Wert.");
    return;
  }

  uint8_t parsedMicrosteps = (uint8_t)microstepsValue;
  if (!isSupportedProductShaftMicrostep(parsedMicrosteps)) {
    server.send(400, "text/plain; charset=utf-8", "Erlaubt sind: 1, 2, 4, 8, 16, 32.");
    return;
  }

  productShaftMicrosteps = parsedMicrosteps;
  saveProductShaftSettings();
  lastShaftActionMessage = lang("Microsteps gespeichert: ", "Microsteps saved: ") +
                           String(productShaftMicrosteps) + ", " +
                           String(getConfiguredProductShaftEjectSteps()) +
                           lang(" Schritte/Umdrehung.", " steps/revolution.");
  redirectTo("/shafts");
}

void handleShaftAddPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("row")) {
    server.send(400, "text/plain; charset=utf-8", "Feld row ist erforderlich.");
    return;
  }

  normalizeProductShaftLayout();
  uint8_t activeRows = getProductShaftActiveRowCount();
  uint16_t rowNumber = 0;
  if (!parseUnsigned16(server.arg("row"), 1, activeRows, rowNumber)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Reihe.");
    return;
  }

  int rowIndex = (int)rowNumber - 1;
  if (productShaftCount >= productShaftMaxCount) {
    lastShaftActionMessage = lang("Maximale Anzahl an Schaechten erreicht.", "Maximum slot count reached.");
  } else if (productShaftRowSlotCount[rowIndex] >= productShaftSlotsPerRow) {
    lastShaftActionMessage = lang("Diese Reihe ist bereits voll.", "This row is already full.");
  } else {
    int insertIndex = getProductShaftRowStartIndex(rowIndex) + productShaftRowSlotCount[rowIndex];
    if (insertProductShaftAt(insertIndex)) {
      productShaftRowSlotCount[rowIndex]++;
      normalizeProductShaftLayout();
      saveProductShaftSettings();
      lastShaftActionMessage = lang("Schacht zu Reihe ", "Slot added to row ") + String(rowNumber) + ".";
    } else {
      lastShaftActionMessage = lang("Schacht konnte nicht hinzugefuegt werden.", "Slot could not be added.");
    }
  }
  redirectTo("/shafts");
}

void handleShaftAddRowPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  normalizeProductShaftLayout();
  uint8_t activeRows = getProductShaftActiveRowCount();
  if (activeRows >= productShaftMaxRowCount || productShaftCount >= productShaftMaxCount) {
    lastShaftActionMessage = lang("Keine weitere Reihe moeglich.", "No additional row possible.");
    redirectTo("/shafts");
    return;
  }

  if (insertProductShaftAt(productShaftCount)) {
    productShaftRowSlotCount[activeRows] = 1;
    normalizeProductShaftLayout();
    saveProductShaftSettings();
    lastShaftActionMessage = lang("Neue Reihe hinzugefuegt: Reihe ", "Added new row: row ") + String(activeRows + 1) + ".";
  } else {
    lastShaftActionMessage = lang("Reihe konnte nicht hinzugefuegt werden.", "Row could not be added.");
  }
  redirectTo("/shafts");
}

void handleShaftRemovePost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  if (!server.hasArg("row")) {
    server.send(400, "text/plain; charset=utf-8", "Feld row ist erforderlich.");
    return;
  }

  normalizeProductShaftLayout();
  uint8_t activeRows = getProductShaftActiveRowCount();
  uint16_t rowNumber = 0;
  if (!parseUnsigned16(server.arg("row"), 1, activeRows, rowNumber)) {
    server.send(400, "text/plain; charset=utf-8", "Ungueltige Reihe.");
    return;
  }

  if (productShaftCount <= 1) {
    lastShaftActionMessage = lang("Mindestens ein Schacht muss aktiv bleiben.", "At least one slot must remain active.");
    redirectTo("/shafts");
    return;
  }

  int rowIndex = (int)rowNumber - 1;
  int rowSlotCount = productShaftRowSlotCount[rowIndex];
  if (rowSlotCount <= 0) {
    lastShaftActionMessage = lang("Kein Schacht zum Entfernen gefunden.", "No slot found to remove.");
    redirectTo("/shafts");
    return;
  }

  if (rowSlotCount == 1 && activeRows <= 1) {
    lastShaftActionMessage = lang("Mindestens ein Schacht muss aktiv bleiben.", "At least one slot must remain active.");
    redirectTo("/shafts");
    return;
  }

  int removeIndex = getProductShaftRowStartIndex(rowIndex) + rowSlotCount - 1;
  if (removeProductShaftAt(removeIndex)) {
    productShaftRowSlotCount[rowIndex]--;
    normalizeProductShaftLayout();
    saveProductShaftSettings();
    lastShaftActionMessage = lang("Letzten Schacht aus Reihe ", "Removed last slot from row ") + String(rowNumber) + ".";
  } else {
    lastShaftActionMessage = lang("Schacht konnte nicht entfernt werden.", "Slot could not be removed.");
  }
  redirectTo("/shafts");
}

void handleShaftRemoveRowPost() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  normalizeProductShaftLayout();
  uint8_t activeRows = getProductShaftActiveRowCount();
  if (activeRows <= 1) {
    lastShaftActionMessage = lang("Mindestens eine Reihe muss aktiv bleiben.", "At least one row must remain active.");
    redirectTo("/shafts");
    return;
  }

  int lastRow = activeRows - 1;
  int slotsToRemove = productShaftRowSlotCount[lastRow];
  if (slotsToRemove <= 0) {
    lastShaftActionMessage = lang("Keine Reihe zum Entfernen gefunden.", "No row found to remove.");
    redirectTo("/shafts");
    return;
  }

  bool removed = true;
  for (int i = 0; i < slotsToRemove; i++) {
    if (!removeProductShaftAt(productShaftCount - 1)) {
      removed = false;
      break;
    }
  }

  productShaftRowSlotCount[lastRow] = 0;
  normalizeProductShaftLayout();
  if (removed) {
    saveProductShaftSettings();
    lastShaftActionMessage = lang("Letzte Reihe entfernt. Aktive Reihen: ", "Removed last row. Active rows: ") + String(getProductShaftActiveRowCount()) + ".";
  } else {
    lastShaftActionMessage = lang("Reihe konnte nicht vollstaendig entfernt werden.", "Row could not be fully removed.");
  }
  redirectTo("/shafts");
}

void handleLoginPost() {
  if (!server.hasArg("pin")) {
    handleWebLoginPage(lang("PIN fehlt.", "PIN missing."));
    return;
  }

  String pin = server.arg("pin");

  if (pin == adminPin) {
    webSessionToken = generateSessionToken();
    server.sendHeader("Set-Cookie", "ESPSESSIONID=" + webSessionToken + "; Path=/; HttpOnly");
    redirectTo("/");
  } else {
    handleWebLoginPage(lang("Falscher PIN.", "Wrong PIN."));
  }
}

void handleLogout() {
  server.sendHeader("Set-Cookie", "ESPSESSIONID=deleted; Path=/; Max-Age=0");
  redirectTo("/");
}

void handleNotFound() {
  if (!isWebAuthenticated()) {
    redirectTo("/");
    return;
  }

  server.send(404, "text/plain; charset=utf-8", "Seite nicht gefunden");
}

void setupWebServer() {
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/email", HTTP_GET, handleEmailPage);
  server.on("/sumup", HTTP_GET, handleSumupPage);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/coins", HTTP_GET, handleCoinsPage);
  server.on("/shafts", HTTP_GET, handleShaftsPage);
  server.on("/cashbook", HTTP_GET, handleCashbookPage);
  server.on("/tests", HTTP_GET, handleTestsPage);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/door/unlock", HTTP_POST, handleDoorUnlockPost);
  server.on("/language/toggle", HTTP_POST, handleLanguageTogglePost);
  server.on("/wifi", HTTP_POST, handleWifiSettingsPost);
  server.on("/email", HTTP_POST, handleEmailSettingsPost);
  server.on("/sumup", HTTP_POST, handleSumupSettingsPost);
  server.on("/update/check", HTTP_POST, handleUpdateCheckPost);
  server.on("/update/install", HTTP_POST, handleUpdateInstallPost);
  server.on("/email/test", HTTP_POST, handleEmailTestPost);
  server.on("/email/cashbook", HTTP_POST, handleEmailCashbookPost);
  server.on("/coins", HTTP_POST, handleCoinSettingsPost);
  server.on("/shafts/save", HTTP_POST, handleShaftSettingsPost);
  server.on("/shafts/eject", HTTP_POST, handleShaftEjectPost);
  server.on("/shafts/microsteps", HTTP_POST, handleShaftMicrostepsPost);
  server.on("/shafts/add", HTTP_POST, handleShaftAddPost);
  server.on("/shafts/addrow", HTTP_POST, handleShaftAddRowPost);
  server.on("/shafts/remove", HTTP_POST, handleShaftRemovePost);
  server.on("/shafts/removerow", HTTP_POST, handleShaftRemoveRowPost);
  server.on("/tests/motor", HTTP_POST, handleMotorTestPost);
  server.onNotFound(handleNotFound);

  server.begin();
  webServerStarted = true;

  Serial.println("Webserver gestartet");
}

// =====================================================
// Modus-Handler
// =====================================================
void handleNormalModeKey(char key) {
  if (sumupPaymentPending) {
    if (key == 'C') {
      cancelSumupPayment();
      return;
    }

    showSumupPaymentPendingScreen();
    return;
  }

  handleComboDetection(key);

  if (key == '*' || key == '#') {
    Serial.print("Normalmodus Taste: ");
    Serial.println(key);
    return;
  }

  if (key == 'A') {
    if (pendingSumupTopupSelection) {
      uint32_t amountEuros = pendingSumupTopupInput.toInt();
      uint32_t amountCents = amountEuros * 100UL;
      if (amountCents == 0) {
        showTemporaryMessage(lang("Betrag fehlt", "Amount missing"),
                             lang("0-9 waehlen", "Choose 0-9"), 1500);
        showPendingSumupTopupAmount();
        return;
      }
      clearPendingSumupTopupSelection();
      if (!startSumupTopup(amountCents, -1)) {
        showTemporaryMessage(lang("SumUp Fehler", "SumUp error"), formatCentsToMoney(amountCents), 2000);
        showNormalScreen();
      }
    } else if (pendingCashlessSelection) {
      uint32_t amountCents = getPendingCashlessAmountCents();
      int shaftIndex = pendingCashlessShaftIndex;
      clearPendingCashlessSelection();
      if (amountCents == 0) {
        if (shaftIndex >= 0) {
          vendProductAtIndex(shaftIndex, lang("Muenzen", "Coins"));
        } else {
          showNormalScreen();
        }
        return;
      }
      if (!startSumupTopup(amountCents, shaftIndex)) {
        showTemporaryMessage(lang("SumUp Fehler", "SumUp error"), formatCentsToMoney(amountCents), 2000);
        showNormalScreen();
      }
    } else if (pendingProductRow != '\0') {
      showTemporaryMessage(lang("Fach noch waehlen", "Choose slot first"),
                           lang("Oder D fuer Abbr", "Or D to cancel"), 1800);
    } else {
      showPendingSumupTopupSelection();
    }
    return;
  }

  if (pendingSumupTopupSelection) {
    if (key >= '0' && key <= '9') {
      if (pendingSumupTopupInput.length() < 4) {
        if (pendingSumupTopupInput.length() > 0 || key != '0') {
          pendingSumupTopupInput += key;
        }
      }
      pendingSumupTopupSelectionMs = millis();
      showPendingSumupTopupAmount();
      return;
    }

    if (key == 'C') {
      if (pendingSumupTopupInput.length() > 0) {
        pendingSumupTopupInput.remove(pendingSumupTopupInput.length() - 1);
        pendingSumupTopupSelectionMs = millis();
        showPendingSumupTopupAmount();
      } else {
        clearPendingSumupTopupSelection();
        showNormalScreen();
      }
      return;
    }

    if (key == 'D') {
      clearPendingSumupTopupSelection();
      showNormalScreen();
      return;
    }
  }

  if (pendingCashlessSelection && key == 'D') {
    clearPendingCashlessSelection();
    showNormalScreen();
    return;
  }

  if (key >= '1' && key <= '6' && pendingProductRow == '\0') {
    pendingProductRow = key;
    pendingProductSelectionMs = millis();
    showPendingProductSelection();
    Serial.print("Produktreihe gewaehlt: ");
    Serial.println(key);
    return;
  }

  if (key >= '1' && key <= '8' && pendingProductRow != '\0') {
    char selectedRow = pendingProductRow;
    clearPendingProductSelection();
    vendProductByCode(selectedRow, key);
    Serial.print("Produkcode verarbeitet: ");
    Serial.print(selectedRow);
    Serial.println(key);
    return;
  }

  if (key == 'D') {
    clearPendingProductSelection();
    showNormalScreen();
    return;
  }

  Serial.print("Normalmodus Taste: ");
  Serial.println(key);
}

void handleServicePinKey(char key) {
  if (isDigitKey(key)) {
    if (pinInput.length() < 8) {
      pinInput += key;
      showServicePinScreen();
    }
    return;
  }

  if (key == '*') {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
      showServicePinScreen();
    }
    return;
  }

  if (key == 'C') {
    currentMode = MODE_NORMAL;
    pinInput = "";
    showNormalScreen();
    return;
  }

  if (key == 'D') {
    if (pinInput == adminPin) {
      pinInput = "";
      enterServiceMenu();
    } else {
      showTemporaryMessage(lang("Falscher PIN", "Wrong PIN"));
      pinInput = "";
      showServicePinScreen();
    }
  }
}

void handleServiceMenuKey(char key) {
  if (key == 'A') {
    if (serviceMenuIndex > 0) {
      serviceMenuIndex--;
      showServiceMenu();
    }
    return;
  }

  if (key == 'B') {
    if (serviceMenuIndex < serviceMenuCount - 1) {
      serviceMenuIndex++;
      showServiceMenu();
    }
    return;
  }

  if (key == 'C') {
    currentMode = MODE_NORMAL;
    showNormalScreen();
    return;
  }

  if (key == 'D') {
    switch (serviceMenuIndex) {
      case 0:
        currentMode = MODE_INFO;
        showInfoScreen();
        break;
      case 1:
        enterWifiMenu();
        break;
      case 2:
        enterAdminPinMenu();
        break;
      case 3:
        enterLanguageMenu();
        break;
      case 4:
        if (lcdAvailable) {
          runInitialKeypadSetup();
        } else {
          showTemporaryMessage(lang("LCD fehlt", "LCD missing"),
                               lang("Setup nicht moegl", "Setup unavailable"));
        }
        enterServiceMenu();
        break;
      case 5:
        lcdPrint2(lang("Entriegele...", "Unlocking..."),
                  lang("Bitte warten", "Please wait"));
        if (pulseDoorLock()) {
          showTemporaryMessage(lang("Tuer entriegelt", "Door unlocked"), "5s");
        } else {
          showTemporaryMessage(lang("Fehler Schloss", "Lock error"),
                               lastDoorLockMessage.substring(0, 16), 2200);
        }
        showServiceMenu();
        break;
    }
  }
}

void handleLanguageMenuKey(char key) {
  if (key == 'A' || key == 'B') {
    currentLanguage = (currentLanguage == LANG_DE) ? LANG_EN : LANG_DE;
    showLanguageMenu();
    return;
  }

  if (key == 'C') {
    enterServiceMenu();
    return;
  }

  if (key == 'D') {
    saveLanguageSetting();
    showTemporaryMessage(lang("Sprache gespei", "Language saved"));
    enterServiceMenu();
  }
}

void handleInfoKey(char key) {
  if (key == 'C') {
    enterServiceMenu();
  }
}

void handleWifiMenuKey(char key) {
  if (key == 'A') {
    if (wifiMenuIndex > 0) {
      wifiMenuIndex--;
      showWifiMenu();
    }
    return;
  }

  if (key == 'B') {
    if (wifiMenuIndex < wifiMenuCount - 1) {
      wifiMenuIndex++;
      showWifiMenu();
    }
    return;
  }

  if (key == 'C') {
    enterServiceMenu();
    return;
  }

  if (key == 'D') {
    switch (wifiMenuIndex) {
      case 0:
        beginTextInput(wifiSSID, MODE_WIFI_EDIT_SSID, TEXTSET_UPPER);
        showTextInputScreen("SSID:");
        break;

      case 1:
        beginTextInput(wifiPassword, MODE_WIFI_EDIT_PASSWORD, TEXTSET_LOWER);
        showTextInputScreen("Passwort:", true);
        break;

      case 2:
        currentMode = MODE_WIFI_DHCP;
        showWifiDhcpEdit();
        break;

      case 3:
        if (wifiDhcp) {
          showTemporaryMessage("DHCP ist aktiv", "Menue gesperrt");
          showWifiMenu();
        } else {
          enterManualIpMenu();
        }
        break;
    }
  }
}

void handleWifiDhcpKey(char key) {
  if (key == 'A' || key == 'B') {
    wifiDhcp = !wifiDhcp;
    showWifiDhcpEdit();
    return;
  }

  if (key == 'C') {
    enterWifiMenu();
    return;
  }

  if (key == 'D') {
    saveWifiSettingsWithFeedback("Tastatur DHCP");
    enterWifiMenu();
  }
}

void handleManualIpMenuKey(char key) {
  if (key == 'A') {
    if (manualIpMenuIndex > 0) {
      manualIpMenuIndex--;
      showManualIpMenu();
    }
    return;
  }

  if (key == 'B') {
    if (manualIpMenuIndex < manualIpMenuCount - 1) {
      manualIpMenuIndex++;
      showManualIpMenu();
    }
    return;
  }

  if (key == 'C') {
    enterWifiMenu();
    return;
  }

  if (key == 'D') {
    switch (manualIpMenuIndex) {
      case 0:
        beginTextInput(wifiManualIp, MODE_WIFI_EDIT_IP, TEXTSET_NUM);
        showTextInputScreen("IP:");
        break;

      case 1:
        beginTextInput(wifiSubnet, MODE_WIFI_EDIT_SUBNET, TEXTSET_NUM);
        showTextInputScreen("Subnetz:");
        break;

      case 2:
        beginTextInput(wifiGateway, MODE_WIFI_EDIT_GATEWAY, TEXTSET_NUM);
        showTextInputScreen("Gateway:");
        break;

      case 3:
        beginTextInput(wifiDns, MODE_WIFI_EDIT_DNS, TEXTSET_NUM);
        showTextInputScreen("DNS Server:");
        break;
    }
  }
}

void handleWifiSsidEditKey(char key) {
  if (handleMultiTapTextInput(key, "SSID:")) return;

  if (key == 'C') {
    textBuffer = "";
    originalTextBuffer = "";
    activeMultiTapKey = '\0';
    enterWifiMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();
    wifiSSID = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur SSID");
    enterWifiMenu();
  }
}

void handleWifiPasswordEditKey(char key) {
  if (handleMultiTapTextInput(key, "Passwort:", true)) return;

  if (key == 'C') {
    textBuffer = "";
    originalTextBuffer = "";
    activeMultiTapKey = '\0';
    enterWifiMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();
    wifiPassword = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur Passwort");
    enterWifiMenu();
  }
}

void handleWifiEditIpKey(char key) {
  if (handleMultiTapTextInput(key, "IP:")) return;

  if (key == 'C') {
    activeMultiTapKey = '\0';
    enterManualIpMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();

    if (!isValidIPv4(textBuffer)) {
      showTemporaryMessage("Ungueltige IP");
      showTextInputScreen("IP:");
      return;
    }

    wifiManualIp = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur IP");
    enterManualIpMenu();
  }
}

void handleWifiEditSubnetKey(char key) {
  if (handleMultiTapTextInput(key, "Subnetz:")) return;

  if (key == 'C') {
    activeMultiTapKey = '\0';
    enterManualIpMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();

    if (!isValidIPv4(textBuffer)) {
      showTemporaryMessage("Ungueltiges", "Subnetz");
      showTextInputScreen("Subnetz:");
      return;
    }

    wifiSubnet = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur Subnetz");
    enterManualIpMenu();
  }
}

void handleWifiEditGatewayKey(char key) {
  if (handleMultiTapTextInput(key, "Gateway:")) return;

  if (key == 'C') {
    activeMultiTapKey = '\0';
    enterManualIpMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();

    if (!isValidIPv4(textBuffer)) {
      showTemporaryMessage("Ungueltiges", "Gateway");
      showTextInputScreen("Gateway:");
      return;
    }

    wifiGateway = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur Gateway");
    enterManualIpMenu();
  }
}

void handleWifiEditDnsKey(char key) {
  if (handleMultiTapTextInput(key, "DNS Server:")) return;

  if (key == 'C') {
    activeMultiTapKey = '\0';
    enterManualIpMenu();
    return;
  }

  if (key == 'D') {
    commitPendingMultiTap();

    if (!isValidIPv4(textBuffer)) {
      showTemporaryMessage("Ungueltiger", "DNS Server");
      showTextInputScreen("DNS Server:");
      return;
    }

    wifiDns = textBuffer;
    saveWifiSettingsWithFeedback("Tastatur DNS");
    enterManualIpMenu();
  }
}

void handleAdminPinMenuKey(char key) {
  if (key == 'C') {
    enterServiceMenu();
    return;
  }

  if (key == 'D') {
    pinInput = "";
    currentMode = MODE_ADMIN_PIN_ENTER_OLD;
    showEnterOldPin();
  }
}

void handleAdminPinEnterOldKey(char key) {
  if (isDigitKey(key)) {
    if (pinInput.length() < 8) {
      pinInput += key;
      showEnterOldPin();
    }
    return;
  }

  if (key == '*') {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
      showEnterOldPin();
    }
    return;
  }

  if (key == 'C') {
    pinInput = "";
    enterAdminPinMenu();
    return;
  }

  if (key == 'D') {
    if (pinInput == adminPin) {
      pinInput = "";
      currentMode = MODE_ADMIN_PIN_ENTER_NEW;
      showEnterNewPin();
    } else {
      showTemporaryMessage(lang("Alter PIN falsch", "Old PIN wrong"));
      pinInput = "";
      showEnterOldPin();
    }
  }
}

void handleAdminPinEnterNewKey(char key) {
  if (isDigitKey(key)) {
    if (pinInput.length() < 8) {
      pinInput += key;
      showEnterNewPin();
    }
    return;
  }

  if (key == '*') {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
      showEnterNewPin();
    }
    return;
  }

  if (key == 'C') {
    pinInput = "";
    enterAdminPinMenu();
    return;
  }

  if (key == 'D') {
    if (pinInput.length() == 8) {
      tempNewPin = pinInput;
      pinInput = "";
      currentMode = MODE_ADMIN_PIN_CONFIRM_NEW;
      showConfirmNewPin();
    } else {
      showTemporaryMessage(lang("PIN braucht", "PIN needs"), lang("8 Stellen", "8 digits"));
      showEnterNewPin();
    }
  }
}

void handleAdminPinConfirmNewKey(char key) {
  if (isDigitKey(key)) {
    if (pinInput.length() < 8) {
      pinInput += key;
      showConfirmNewPin();
    }
    return;
  }

  if (key == '*') {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
      showConfirmNewPin();
    }
    return;
  }

  if (key == 'C') {
    pinInput = "";
    tempNewPin = "";
    enterAdminPinMenu();
    return;
  }

  if (key == 'D') {
    if (pinInput == tempNewPin) {
      adminPin = tempNewPin;
      saveAdminPin();
      showTemporaryMessage(lang("PIN geaendert", "PIN changed"));
      pinInput = "";
      tempNewPin = "";
      enterAdminPinMenu();
    } else {
      showTemporaryMessage(lang("PIN ungleich", "PIN mismatch"));
      pinInput = "";
      tempNewPin = "";
      enterAdminPinMenu();
    }
  }
}

// =====================================================
// Zentrale Tastenverarbeitung
// =====================================================
void handleKey(char key) {
  Serial.print("Taste: ");
  Serial.println(key);

  switch (currentMode) {
    case MODE_NORMAL:
      handleNormalModeKey(key);
      break;

    case MODE_SERVICE_PIN:
      handleServicePinKey(key);
      break;

    case MODE_SERVICE_MENU:
      handleServiceMenuKey(key);
      break;

    case MODE_INFO:
      handleInfoKey(key);
      break;

    case MODE_WIFI_MENU:
      handleWifiMenuKey(key);
      break;

    case MODE_LANGUAGE_MENU:
      handleLanguageMenuKey(key);
      break;

    case MODE_WIFI_EDIT_SSID:
      handleWifiSsidEditKey(key);
      break;

    case MODE_WIFI_EDIT_PASSWORD:
      handleWifiPasswordEditKey(key);
      break;

    case MODE_WIFI_DHCP:
      handleWifiDhcpKey(key);
      break;

    case MODE_WIFI_MANUAL_IP_MENU:
      handleManualIpMenuKey(key);
      break;

    case MODE_WIFI_EDIT_IP:
      handleWifiEditIpKey(key);
      break;

    case MODE_WIFI_EDIT_SUBNET:
      handleWifiEditSubnetKey(key);
      break;

    case MODE_WIFI_EDIT_GATEWAY:
      handleWifiEditGatewayKey(key);
      break;

    case MODE_WIFI_EDIT_DNS:
      handleWifiEditDnsKey(key);
      break;

    case MODE_ADMIN_PIN_MENU:
      handleAdminPinMenuKey(key);
      break;

    case MODE_ADMIN_PIN_ENTER_OLD:
      handleAdminPinEnterOldKey(key);
      break;

    case MODE_ADMIN_PIN_ENTER_NEW:
      handleAdminPinEnterNewKey(key);
      break;

    case MODE_ADMIN_PIN_CONFIRM_NEW:
      handleAdminPinConfirmNewKey(key);
      break;
  }
}

// =====================================================
// Setup / Loop
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(2000);

  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.println();
  printBootBanner();
  Serial.println("Boot started");
  Serial.printf("Reset Reason: %s (%d)\n", getResetReasonText(resetReason), (int)resetReason);
  Serial.flush();

  logBootStep("Wire.begin start");
  Wire.begin(8, 9);
  Wire.setTimeOut(50);
  logBootStep("Wire.begin ok");
  scanI2cBus();

  logBootStep("LCD init start");
  if (VM_ENABLE_LCD) {
    int lcdStatus = lcd.begin(16, 2);
    if (lcdStatus == 0) {
      lcd.backlight();
      lcdAvailable = true;
      Serial.println("LCD initialized");
      showBootSplash();
      logBootStep("LCD init ok");
    } else {
      lcdAvailable = false;
      Serial.printf("LCD init error: %d\n", lcdStatus);
      logBootStep("LCD init failed");
    }
  } else {
    lcdAvailable = false;
    Serial.println(VM_ENABLE_LCD ? "LCD not found, skipping initialization"
                                 : "LCD disabled by build flag");
    logBootStep("LCD init skipped");
  }

  logBootStep("NVS check start");
  if (!ensureNvsReady()) {
    Serial.println("WARNUNG: NVS nicht verfügbar, Einstellungen sind nicht persistent");
  }
  logBootStep("NVS check ok");
  logBootStep("loadSettings start");
  loadSettings();
  logBootStep("loadSettings ok");
  Serial.println("Settings loaded from Preferences");
  Serial.println("Admin PIN length: " + String(adminPin.length()));

  if (keypadNeedsSetup) {
    Serial.println("Keypad: no stored mapping found, starting setup");
    if (lcdAvailable) {
      runInitialKeypadSetup();
    } else {
      Serial.println("Keypad setup skipped: LCD not available");
    }
  }

  wifiSkipThisBoot = false;
  if (wifiSSID.length() > 0 &&
      (resetReason == ESP_RST_PANIC ||
       resetReason == ESP_RST_INT_WDT ||
       resetReason == ESP_RST_TASK_WDT ||
       resetReason == ESP_RST_WDT)) {
    Serial.println("Note: crash/watchdog reset detected, WiFi startup remains enabled");
  }

  logBootStep("GPIO init start");
  pinMode(coinPulsePin, INPUT_PULLUP);
  setupMotorControllerBus();
  logBootStep("GPIO init ok");
  logBootStep("SD init start");
  initSdCard();
  logBootStep("SD init ok");

  logBootStep("WiFi init start");
  if (wifiSSID.length() > 0) {
    wifiConnectPending = true;
    lastWifiAttemptMs = millis() - wifiRetryIntervalMs;
    Serial.println("WiFi: startup scheduled after setup");
  } else {
    stopWifiCompletely();
  }
  logBootStep("WiFi init ok");

  logBootStep("UI init start");
  showNormalScreen();
  logBootStep("UI init ok");
  blinkBootSuccessLed();

  Serial.println("System started");
  Serial.println("SSID: " + wifiSSID);
  Serial.println("DHCP: " + String(wifiDhcp ? "Yes" : "No"));
  if (wifiDhcp) {
    Serial.println("Network mode: DHCP");
    if (wifiConnected) {
      Serial.println("Active IP: " + WiFi.localIP().toString());
      Serial.println("Active gateway: " + WiFi.gatewayIP().toString());
      Serial.println("Active subnet: " + WiFi.subnetMask().toString());
      Serial.println("Active DNS: " + WiFi.dnsIP().toString());
    } else {
      Serial.println("Active IP: not assigned yet");
    }
  } else {
    Serial.println("Network mode: static");
    Serial.println("Configured IP: " + wifiManualIp);
    Serial.println("Configured subnet: " + wifiSubnet);
    Serial.println("Configured gateway: " + wifiGateway);
    Serial.println("Configured DNS: " + wifiDns);
  }
  Serial.println("NTP: " + wifiNtpServer);
  Serial.println("Email enabled: " + String(emailNotifyEnabled ? "Yes" : "No"));
  Serial.println("Email protocol: " + emailProtocol);
  Serial.println("Email host: " + emailHost + ":" + String(emailPort));
  Serial.println("Coin Pulse Pin: GPIO " + String(coinPulsePin));
  Serial.println("Motor-ESP UART RX: GPIO " + String(motorControllerRxPin));
  Serial.println("Motor-ESP UART TX: GPIO " + String(motorControllerTxPin));
  Serial.println("SD SPI SCK: GPIO " + String(sdCardSckPin));
  Serial.println("SD SPI MOSI: GPIO " + String(sdCardMosiPin));
  Serial.println("SD SPI MISO: GPIO " + String(sdCardMisoPin));
  Serial.println("SD SPI CS: GPIO " + String(sdCardCsPin));
  Serial.println("SD status: " + sdCardStatusMessage);
}

void loop() {
  processMotorControllerBus();
  processCoinSignal();
  pollSumupPaymentStatus();

  if (wifiConnectPending) {
    wifiConnectPending = false;
    Serial.println("WiFi: performing initial connection attempt in loop");
    applyWifiConfig();
  }

  if (currentMode == MODE_NORMAL &&
      pendingProductRow != '\0' &&
      (millis() - pendingProductSelectionMs) > productSelectionTimeoutMs) {
    clearPendingProductSelection();
    showNormalScreen();
  }

  if (currentMode == MODE_NORMAL &&
      pendingCashlessSelection &&
      (millis() - pendingProductSelectionMs) > productSelectionTimeoutMs) {
    clearPendingCashlessSelection();
    showNormalScreen();
  }

  if (currentMode == MODE_NORMAL &&
      pendingSumupTopupSelection &&
      (millis() - pendingSumupTopupSelectionMs) > productSelectionTimeoutMs) {
    clearPendingSumupTopupSelection();
    showNormalScreen();
  }

  if ((currentMode == MODE_WIFI_EDIT_SSID ||
       currentMode == MODE_WIFI_EDIT_PASSWORD ||
       currentMode == MODE_WIFI_EDIT_IP ||
       currentMode == MODE_WIFI_EDIT_SUBNET ||
       currentMode == MODE_WIFI_EDIT_GATEWAY ||
       currentMode == MODE_WIFI_EDIT_DNS) &&
      activeMultiTapKey != '\0' &&
      (millis() - lastMultiTapTime) > multiTapTimeoutMs) {

    if (currentMode == MODE_WIFI_EDIT_SSID) {
      commitPendingMultiTap();
      showTextInputScreen("SSID:");
    } else if (currentMode == MODE_WIFI_EDIT_PASSWORD) {
      commitPendingMultiTap();
      showTextInputScreen("Passwort:", true);
    } else if (currentMode == MODE_WIFI_EDIT_IP) {
      commitPendingMultiTap();
      showTextInputScreen("IP:");
    } else if (currentMode == MODE_WIFI_EDIT_SUBNET) {
      commitPendingMultiTap();
      showTextInputScreen("Subnetz:");
    } else if (currentMode == MODE_WIFI_EDIT_GATEWAY) {
      commitPendingMultiTap();
      showTextInputScreen("Gateway:");
    } else if (currentMode == MODE_WIFI_EDIT_DNS) {
      commitPendingMultiTap();
      showTextInputScreen("DNS Server:");
    }
  }

  ensureWifiConnection();
  processCoinPulseBurst();
  refreshNormalScreenIfNeeded();

  if (webServerStarted && !wifiConnected) {
    server.stop();
    webServerStarted = false;
    Serial.println("Webserver gestoppt (WiFi offline)");
  }

  if (webServerStarted) {
    server.handleClient();
  }

  char rawKey = keypad.getKey();
  if (rawKey) {
    char key = translateDetectedKey(rawKey);
    if (key != rawKey) {
      Serial.printf("Taste raw=%c -> mapped=%c\n", rawKey, key);
    }
    handleKey(key);
  }
}
