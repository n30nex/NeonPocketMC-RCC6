#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#ifdef NEONPOCKET_UI
  static constexpr ColorVal NEON_DARK = 0x0000;
  static constexpr ColorVal NEON_LIGHT = 0xFFFF;
  static constexpr ColorVal NEON_RED = 0xF800;
  static constexpr ColorVal NEON_GREEN = 0x07E0;
  static constexpr ColorVal NEON_BLUE = 0x001F;
  static constexpr ColorVal NEON_YELLOW = 0xFFE0;
  static constexpr ColorVal NEON_ORANGE = 0xFD20;
#else
  #define NEON_DARK UIColor::window_bkg
  #define NEON_LIGHT UIColor::primary_txt
  #define NEON_RED UIColor::warning_txt
  #define NEON_GREEN UIColor::primary_txt
  #define NEON_BLUE UIColor::corp_blue
  #define NEON_YELLOW UIColor::secondary_txt
  #define NEON_ORANGE UIColor::warning_txt
#endif

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount = 0;
#ifdef NEONPOCKET_UI
  ColorVal _alert_color = NEON_LIGHT;
  unsigned long _alert_started = 0;
  ColorVal _pulse_color = NEON_YELLOW;
  unsigned long _pulse_started = 0;
  unsigned long _pulse_until = 0;
  unsigned long _pending_pulse_duration = 0;
  unsigned long _pending_alert_duration = 0;
  bool _wake_pending = false;
  bool _connection_known = false;
  bool _last_connection = false;
  unsigned long _power_confirm_until = 0;
  char _latest_sender[32] = "";
  char _latest_preview[48] = "";
  bool _radio_rx_pending = false;
  bool _radio_tx_pending = false;
  UIRadioEvent _radio_tx_event = UIRadioEvent::TxComplete;
  uint8_t _radio_tx_payload_type = 0;
  int16_t _radio_rssi_dbm = 0;
  int16_t _radio_snr_quarter_db = 0;
  bool _radio_sample_known = false;
  unsigned long _last_rx_pulse = 0;
  uint16_t _cached_batt_millivolts = 0;
  unsigned long _next_batt_sample = 0;
  bool _battery_low_warning = false;
  unsigned long _manual_advert_until = 0;
  ColorVal _next_message_color = NEON_YELLOW;
  bool _unread_overflow = false;
#ifdef NEONPOCKET_RCC6_UI_EXTENSIONS
  unsigned long _next_diag_sample = 0;
  uint32_t _cached_uptime_seconds = 0;
  uint32_t _cached_rx_packets = 0;
  uint32_t _cached_tx_packets = 0;
  uint32_t _cached_rx_errors = 0;
  uint32_t _cached_heap_free = 0;
  uint32_t _cached_heap_max = 0;
  int16_t _cached_noise_floor = 0;

  void sampleDiagnostics();
#endif
#ifdef NEONPOCKET_ULTIMATE
  bool _private_notification_locked = false;
#endif

  void startNeonPulse(ColorVal color, unsigned long duration_millis = 300);
  bool handleNeonInput(char c);
#endif
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* curr;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
#ifdef NEONPOCKET_UI
#ifdef NEONPOCKET_ULTIMATE
  void gotoMsgPreviewScreen();
  bool isPrivateNotificationLocked() const { return _private_notification_locked; }
#else
  void gotoMsgPreviewScreen() { setCurrScreen(msg_preview); }
#endif
#endif
  void showAlert(const char* text, int duration_millis,
      ColorVal color = NEON_LIGHT);
  int  getMsgCount() const { return _msgcount; }
#ifdef NEONPOCKET_UI
  const char* getLatestSender() const { return _latest_sender; }
  const char* getLatestPreview() const { return _latest_preview; }
  bool hasLatestPreview() const { return _latest_preview[0] != 0; }
  bool hasUnreadOverflow() const { return _unread_overflow; }
  void setLocalUnread(int count, const char* sender = nullptr, const char* preview = nullptr,
      bool overflow = false);
  bool isPowerConfirmArmed() const;
  uint16_t getCachedBattMilliVolts() const { return _cached_batt_millivolts; }
  bool hasCachedRadioSample() const { return _radio_sample_known; }
  int16_t getCachedRadioRSSI() const { return _radio_rssi_dbm; }
  int16_t getCachedRadioSNRQuarter() const { return _radio_snr_quarter_db; }
  void armManualAdvert() { _manual_advert_until = millis() + 60000; }
#ifdef NEONPOCKET_RCC6_UI_EXTENSIONS
  uint32_t getCachedUptimeSeconds() const { return _cached_uptime_seconds; }
  uint32_t getCachedRxPackets() const { return _cached_rx_packets; }
  uint32_t getCachedTxPackets() const { return _cached_tx_packets; }
  uint32_t getCachedRxErrors() const { return _cached_rx_errors; }
  uint32_t getCachedHeapFree() const { return _cached_heap_free; }
  uint32_t getCachedHeapMax() const { return _cached_heap_max; }
  int16_t getCachedNoiseFloor() const { return _cached_noise_floor; }
  void requestRefresh() { _next_refresh = 0; }
#endif
#endif
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();


  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
#ifdef NEONPOCKET_UI
  void newMsgWithEvent(uint8_t path_len, const char* from_name, const char* text,
      int msgcount, UIEventType type) override;
#endif
  void notify(UIEventType t = UIEventType::none) override;
#ifdef NEONPOCKET_UI
  void onNewContactVisual() override;
  void onRadioEvent(UIRadioEvent event, uint8_t payload_type,
      int16_t rssi_dbm = 0, int16_t snr_quarter_db = 0) override;
#endif
  void loop() override;

  void shutdown(bool restart = false);
};
