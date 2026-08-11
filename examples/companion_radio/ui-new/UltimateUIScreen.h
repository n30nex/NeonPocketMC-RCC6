#pragma once

#ifdef NEONPOCKET_ULTIMATE

#include <helpers/ui/UIScreen.h>
#include "../UltimateService.h"

class UITask;
class SensorManager;
struct NodePrefs;

class UltimateUIScreen : public UIScreen {
  enum class Area : uint8_t { Home, Inbox, Network, Radio, Tools, Power, Count };
  enum class View : uint8_t {
    Root,
    InboxThreads,
    InboxMessage,
    NetworkList,
    NetworkDetail,
    RadioPages,
    ToolMenu,
    TargetPicker,
    PhrasePicker,
    Keyboard,
    SendConfirm,
    HistorySettings,
  };
  enum class ScanPhase : uint8_t { Group, Item };

  struct ThreadSummary {
    uint8_t kind;
    uint8_t target;
    uint8_t peer_key[6];
    char label[32];
    uint32_t newest_sequence;
    uint16_t unread;
  };

  struct ComposeTarget {
    uint8_t kind;
    uint8_t target;
    uint8_t peer_key[6];
    char label[32];
  };

  UITask* task;
  mesh::RTCClock* rtc;
  SensorManager* sensors;
  NodePrefs* prefs;
  Area area = Area::Home;
  View view = View::Root;
  uint8_t selection = 0;
  uint8_t radio_page = 0;
  uint16_t message_page_offset = 0;
  uint16_t message_next_offset = 0;
  bool message_has_more = false;
  uint32_t selected_message_sequence = 0;
  uint16_t selected_message_ordinal = 0;
  UltimateHistoryRecord current_message = {};
  bool current_message_valid = false;
  UltimateHistoryRecord home_latest = {};
  uint32_t home_latest_sequence = 0;
  uint16_t home_latest_unread_count = 0;
  bool home_latest_valid = false;
  ThreadSummary selected_thread = {};
  ThreadSummary threads[32] = {};
  uint8_t thread_count = 0;
  ComposeTarget compose_target = {};
  ComposeTarget targets[48] = {};
  uint8_t target_count = 0;
  char compose_text[141] = {};
  uint8_t compose_length = 0;
  ScanPhase scan_phase = ScanPhase::Group;
  uint8_t scan_group = 0;
  uint8_t scan_item = 0;
  bool uppercase = false;
  uint32_t next_scan = 0;
  bool shutdown_pending = false;
  bool history_clear_armed = false;
  uint32_t transition_started = 0;
  int8_t transition_direction = 0;

  void refreshThreads();
  bool findThreadMessage(uint16_t ordinal, UltimateHistoryRecord& record) const;
  bool loadThreadMessage(uint16_t ordinal);
  uint8_t buildTargets(ComposeTarget* targets, uint8_t capacity) const;
  void refreshTargets();
  void enterView(View next);
  void startAreaTransition(int8_t direction);
  void advanceArea();
  void renderHeader(DisplayDriver& display, const char* title);
  void renderFooter(DisplayDriver& display, const char* hint);
  void renderRoot(DisplayDriver& display);
  void renderHome(DisplayDriver& display);
  void renderInboxRoot(DisplayDriver& display);
  void renderNetworkRoot(DisplayDriver& display);
  void renderRadioRoot(DisplayDriver& display);
  void renderToolsRoot(DisplayDriver& display);
  void renderPowerRoot(DisplayDriver& display);
  void renderInboxThreads(DisplayDriver& display);
  void renderInboxMessage(DisplayDriver& display);
  void renderNetworkList(DisplayDriver& display);
  void renderNetworkDetail(DisplayDriver& display);
  void renderRadioPages(DisplayDriver& display);
  void renderToolMenu(DisplayDriver& display);
  void renderTargetPicker(DisplayDriver& display);
  void renderPhrasePicker(DisplayDriver& display);
  void renderKeyboard(DisplayDriver& display);
  void renderSendConfirm(DisplayDriver& display);
  void renderHistorySettings(DisplayDriver& display);
  void renderTransition(DisplayDriver& display);
  size_t renderMessagePage(DisplayDriver& display, const char* text, size_t offset) const;
  void handleRootAction();
  void handleToolAction();
  void handleKeyboardSelect();
  void appendComposer(char value);
  bool sendComposer();
  const char* areaTitle() const;

public:
  UltimateUIScreen(UITask* ui_task, mesh::RTCClock* clock, SensorManager* sensor_manager,
                   NodePrefs* node_prefs)
      : task(ui_task), rtc(clock), sensors(sensor_manager), prefs(node_prefs) {}

  int render(DisplayDriver& display) override;
  void poll() override;
  bool handleInput(char c) override;

  void openInbox();
  void showPowerConfirm();
  void requestShutdown();
  bool isPowerPage() const { return area == Area::Power && view == View::Root; }
};

#endif
