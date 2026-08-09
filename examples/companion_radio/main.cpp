#include <Arduino.h>   // needed for PlatformIO
#include <stdlib.h>
#include <Mesh.h>
#include "MyMesh.h"

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

// interface manager
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface
#if defined(BLE_PIN_CODE)
  #ifdef ESP32
    // include esp32 bluetooth interface
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    // include nrf52 bluetooth interface
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef WIFI_SSID
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #ifdef ESP32
    // include esp32 wifi interface
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
#endif

// include RCC6 Wi-Fi AP/LAN Web companion interface
#ifdef RCC6_WEB_AP
  #include <helpers/esp32/SerialWebInterface.h>
  SerialWebInterface web_interface;
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface usb_serial_interface;
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

// platform file system
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  #ifdef NEONPOCKET_SAFE_SPIFFS_BOOTSTRAP
    #include <esp_partition.h>
  #endif
  DataStore store(SPIFFS, rtc_clock);
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) {
    delay(1000);
  }
}

#ifdef DISPLAY_CLASS
static void showFatal(DisplayDriver* display_driver, const char* line1, const char* line2) {
  if (display_driver == NULL) return;
  if (!display_driver->isOn()) display_driver->turnOn();
  if (!display_driver->isOn()) return;
  display_driver->startFrame();
  display_driver->setTextSize(1);
  display_driver->setColor(UIColor::warning_txt);
  display_driver->drawTextCentered(display_driver->width() / 2,
      display_driver->height() / 2 - 10, line1);
  display_driver->setColor(UIColor::primary_txt);
  display_driver->drawTextCentered(display_driver->width() / 2,
      display_driver->height() / 2 + 8, line2);
  display_driver->endFrame();
}
#endif

#ifdef NEONPOCKET_MEMORY_GATE_BYTES
static unsigned long next_neon_memory_probe = 0;

static bool probeNeonMemory() {
  void* probe = malloc(NEONPOCKET_MEMORY_GATE_BYTES);
  if (probe == NULL) return false;
  free(probe);
  return true;
}
#endif

#if defined(ESP32) && defined(NEONPOCKET_SAFE_SPIFFS_BOOTSTRAP)
static bool isSpiffsPartitionErased() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (partition == nullptr) {
    Serial.println("ERROR: SPIFFS partition not found");
    return false;
  }

  static uint8_t chunk[1024];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(chunk)) {
    const size_t remaining = partition->size - offset;
    const size_t length = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (esp_partition_read(partition, offset, chunk, length) != ESP_OK) {
      Serial.println("ERROR: SPIFFS partition read failed");
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      if (chunk[i] != 0xFF) return false;
    }
  }
  return true;
}

static bool beginSpiffsPreservingData() {
  if (SPIFFS.begin(false)) return true;
  if (!isSpiffsPartitionErased()) {
    Serial.println("ERROR: SPIFFS mount failed; nonblank data was not formatted");
    return false;
  }

  Serial.println("SPIFFS: blank partition detected; creating filesystem");
  if (!SPIFFS.format()) {
    Serial.println("ERROR: SPIFFS format failed");
    return false;
  }
  return SPIFFS.begin(false);
}
#endif

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

void setup() {
  Serial.begin(115200);
#ifdef RC52_STARTUP_DIAGNOSTICS
  delay(2500);
  Serial.println("RC52_DIAG stage=serial-ready");
#endif
  board.begin();
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=board-ready");
#endif

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=display-begin");
#endif
  if (display.begin()) {
    disp = &display;
#ifdef RC52_STARTUP_DIAGNOSTICS
    Serial.println("RC52_DIAG stage=display-ready");
#endif
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  } else {
    Serial.println("ERROR: required display initialization failed");
  #ifdef DISPLAY_REQUIRED
    halt();
  #endif
  }
#endif

#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=radio-begin");
#endif
  if (!radio_init()) {
    Serial.println("ERROR: radio initialization failed");
#ifdef DISPLAY_CLASS
    if (disp != NULL) {
      disp->startFrame();
      disp->setTextSize(1);
      disp->setColor(UIColor::warning_txt);
      disp->drawTextCentered(disp->width() / 2, disp->height() / 2 - 10, "RADIO INIT FAILED");
      disp->setColor(UIColor::primary_txt);
      disp->drawTextCentered(disp->width() / 2, disp->height() / 2 + 8, "Reset device");
      disp->endFrame();
    }
#endif
    halt();
  }
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=radio-ready");
#endif

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=storage-begin");
#endif
  const bool internal_fs_ready = InternalFS.begin();
#ifdef NEONPOCKET_UI
  if (!internal_fs_ready) {
    Serial.println("ERROR: internal filesystem mount failed; refusing to format");
  #ifdef DISPLAY_CLASS
    showFatal(disp, "STORAGE FAILED", "Identity preserved");
  #endif
    halt();
  }
#endif
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=storage-ready");
#endif
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
#elif defined(ESP32)
#ifdef NEONPOCKET_SAFE_SPIFFS_BOOTSTRAP
  if (!beginSpiffsPreservingData()) {
  #ifdef DISPLAY_CLASS
    showFatal(disp, "STORAGE ERROR", "Data not erased");
  #endif
    halt();
  }
#else
  SPIFFS.begin(true);
#endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
#else
  #error "need to define filesystem"
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE)
  bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
#endif

// add wifi interface
#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  wifi_interface.begin(TCP_PORT);
  interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
#endif

// add RCC6 Web/AP interface
#ifdef RCC6_WEB_AP
  board.setInhibitSleep(true);
  web_interface.begin(the_mesh.getNodePrefs()->node_name, TCP_PORT);
  interface_manager.addInterface(InterfaceType::WiFi, &web_interface);
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  ethernet_interface.begin();
  interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

  the_mesh.startInterface(interface_manager);
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

#ifdef NEONPOCKET_MEMORY_GATE_BYTES
  if (!probeNeonMemory()) {
    Serial.println("ERROR: NeonPocket 16 KB memory gate failed");
  #ifdef DISPLAY_CLASS
    showFatal(disp, "MEMORY GATE FAILED", "Reset device");
  #endif
    halt();
  }
  Serial.println("NeonPocket: 16 KB memory gate passed");
  next_neon_memory_probe = millis() + 60000;
#endif

  board.onBootComplete();
#ifdef RC52_STARTUP_DIAGNOSTICS
  Serial.println("RC52_DIAG stage=boot-complete");
#endif
}

#ifdef NEONPOCKET_SCREEN_CAPTURE
static char capture_command[24];
static uint8_t capture_command_len = 0;

static void handleScreenCaptureCommand(const char* command) {
  if (strcmp(command, "NP PING") == 0) {
    Serial.println("NPOK PONG");
    return;
  }

  if (strncmp(command, "NP PAGE ", 8) == 0 &&
      command[8] >= '0' && command[8] <= '9' && command[9] == 0) {
    const uint8_t page = command[8] - '0';
    if (ui_task.diagnosticSetPage(page)) {
      Serial.print("NPOK PAGE ");
      Serial.println(page);
    } else {
      Serial.println("NPERR PAGE");
    }
    return;
  }

  if (strcmp(command, "NP CAPTURE") == 0) {
    Serial.println("NPFB 220 128 56320 LE_RGB565");
    const size_t sent = display.writeFramebuffer(Serial);
    Serial.print("\nNPEND ");
    Serial.println((unsigned)sent);
    Serial.flush();
    return;
  }

  Serial.println("NPERR COMMAND");
}

static void pollScreenCaptureCommands() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      capture_command[capture_command_len] = 0;
      if (capture_command_len > 0) handleScreenCaptureCommand(capture_command);
      capture_command_len = 0;
    } else if (capture_command_len + 1 < sizeof(capture_command)) {
      capture_command[capture_command_len++] = c;
    } else {
      capture_command_len = 0;
    }
  }
}
#endif

void loop() {
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
#ifdef NEONPOCKET_SCREEN_CAPTURE
  pollScreenCaptureCommands();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

#ifdef NEONPOCKET_MEMORY_GATE_BYTES
  const unsigned long memory_now = millis();
  if ((long)(memory_now - next_neon_memory_probe) >= 0) {
    next_neon_memory_probe = memory_now + 60000;
    if (!probeNeonMemory()) {
      Serial.println("ERROR: NeonPocket runtime memory gate failed");
  #ifdef DISPLAY_CLASS
      showFatal(&display, "MEMORY GATE FAILED", "Activity halted");
  #endif
      halt();
    }
  }
#endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif
}
