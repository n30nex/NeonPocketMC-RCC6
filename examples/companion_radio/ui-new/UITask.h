#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
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
#ifdef HELTEC_RCC6_NEON_UI
  DisplayDriver::Color _alert_color = DisplayDriver::LIGHT;
  unsigned long _alert_started = 0;
  DisplayDriver::Color _pulse_color = DisplayDriver::YELLOW;
  unsigned long _pulse_started = 0;
  unsigned long _pulse_until = 0;
  unsigned long _pending_pulse_duration = 0;
  unsigned long _pending_alert_duration = 0;
  bool _wake_pending = false;
  bool _connection_known = false;
  bool _last_connection = false;
  bool _menu_open = false;
  uint8_t _menu_index = 0;
  unsigned long _menu_started = 0;
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
  DisplayDriver::Color _next_message_color = DisplayDriver::YELLOW;

  void startNeonPulse(DisplayDriver::Color color, unsigned long duration_millis = 300);
  bool handleNeonInput(char c);
  void selectNeonMenuItem();
  void renderNeonMenu(DisplayDriver& display);
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

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis,
      DisplayDriver::Color color = DisplayDriver::LIGHT);
  int  getMsgCount() const { return _msgcount; }
#ifdef HELTEC_RCC6_NEON_UI
  const char* getLatestSender() const { return _latest_sender; }
  const char* getLatestPreview() const { return _latest_preview; }
  bool hasLatestPreview() const { return _latest_preview[0] != 0; }
  uint16_t getCachedBattMilliVolts() const { return _cached_batt_millivolts; }
  bool hasCachedRadioSample() const { return _radio_sample_known; }
  int16_t getCachedRadioRSSI() const { return _radio_rssi_dbm; }
  int16_t getCachedRadioSNRQuarter() const { return _radio_snr_quarter_db; }
  void armManualAdvert() { _manual_advert_until = millis() + 60000; }
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
#ifdef HELTEC_RCC6_NEON_UI
  void newMsgWithEvent(uint8_t path_len, const char* from_name, const char* text,
      int msgcount, UIEventType type) override;
#endif
  void notify(UIEventType t = UIEventType::none) override;
#ifdef HELTEC_RCC6_NEON_UI
  void onNewContactVisual() override;
  void onRadioEvent(UIRadioEvent event, uint8_t payload_type,
      int16_t rssi_dbm = 0, int16_t snr_quarter_db = 0) override;
#endif
  void loop() override;

  void shutdown(bool restart = false);
};
