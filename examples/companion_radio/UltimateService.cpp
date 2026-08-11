#ifdef NEONPOCKET_ULTIMATE

#include "UltimateService.h"

#include "DataStore.h"
#include <ultimate_build_sha.h>
#include <esp_heap_caps.h>
#include <stddef.h>
#include <string.h>

namespace {
constexpr const char* history_path = "/np/history.bin";
constexpr const char* history_new_path = "/np/history.new";
constexpr const char* history_old_path = "/np/history.old";
constexpr const char* meta_a_path = "/np/meta-a.bin";
constexpr const char* meta_b_path = "/np/meta-b.bin";
constexpr const char* settings_path = "/np/settings.bin";
constexpr const char* settings_tmp_path = "/np/settings.tmp";
constexpr const char* metrics_path = "/np/metrics.bin";

bool generationAfter(uint32_t lhs, uint32_t rhs) {
  return static_cast<int32_t>(lhs - rhs) > 0;
}
}

UltimateService ultimate_service;

const char* UltimateService::getBuildSha() const {
  return ULTIMATE_BUILD_SHA;
}

uint32_t UltimateService::crc32(const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFU;
  while (length--) {
    crc ^= *bytes++;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool UltimateService::validCapacity(uint16_t capacity) {
  return capacity == 0 || capacity == 128 || capacity == 512 || capacity == 2048;
}

void UltimateService::copyText(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) return;
  if (source == nullptr) source = "";
  strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = 0;
}

void UltimateService::setDefaults() {
  memset(&settings, 0, sizeof(settings));
  settings.history_capacity = 512;
  settings.scan_cadence_ms = 650;
  settings.private_notifications = false;
  static const char* defaults[8] = {
      "On my way", "All good", "Need help", "Please repeat",
      "Received", "Where are you?", "Yes", "No"};
  for (uint8_t i = 0; i < 8; i++) {
    copyText(settings.quick_phrases[i], sizeof(settings.quick_phrases[i]), defaults[i]);
  }
}

bool UltimateService::loadSettings() {
  setDefaults();
  if (!filesystem->exists(settings_path)) {
    if (filesystem->exists(settings_tmp_path)) {
      filesystem->rename(settings_tmp_path, settings_path);
    } else {
      return saveSettings();
    }
  }

  File file = filesystem->open(settings_path, FILE_READ);
  SettingsFile stored = {};
  const bool complete = file && file.size() == sizeof(stored) &&
      file.read(reinterpret_cast<uint8_t*>(&stored), sizeof(stored)) == sizeof(stored);
  if (file) file.close();
  const uint32_t expected = crc32(&stored, offsetof(SettingsFile, crc32));
  if (!complete || stored.magic != settings_magic || stored.version != format_version ||
      stored.crc32 != expected || !validCapacity(stored.history_capacity)) {
    Serial.println("Ultimate: settings invalid; using safe defaults");
    return saveSettings();
  }

  settings.history_capacity = stored.history_capacity;
  settings.scan_cadence_ms = stored.scan_cadence_ms;
  if (settings.scan_cadence_ms != 450 && settings.scan_cadence_ms != 650 &&
      settings.scan_cadence_ms != 900 && settings.scan_cadence_ms != 1200) {
    settings.scan_cadence_ms = 650;
  }
  settings.private_notifications = stored.private_notifications != 0;
  for (uint8_t i = 0; i < 8; i++) {
    copyText(settings.quick_phrases[i], sizeof(settings.quick_phrases[i]),
             stored.quick_phrases[i]);
  }
  return true;
}

bool UltimateService::saveSettings() {
  SettingsFile stored = {};
  stored.magic = settings_magic;
  stored.version = format_version;
  stored.history_capacity = settings.history_capacity;
  stored.scan_cadence_ms = settings.scan_cadence_ms;
  stored.private_notifications = settings.private_notifications ? 1 : 0;
  for (uint8_t i = 0; i < 8; i++) {
    copyText(stored.quick_phrases[i], sizeof(stored.quick_phrases[i]),
             settings.quick_phrases[i]);
  }
  stored.crc32 = crc32(&stored, offsetof(SettingsFile, crc32));

  if (!removeIfExists(settings_tmp_path)) return false;
  File file = filesystem->open(settings_tmp_path, FILE_WRITE);
  const bool written = file &&
      file.write(reinterpret_cast<const uint8_t*>(&stored), sizeof(stored)) == sizeof(stored);
  if (file) {
    file.flush();
    file.close();
  }
  if (!written) {
    removeIfExists(settings_tmp_path);
    return false;
  }
  if (!removeIfExists(settings_path)) return false;
  return filesystem->rename(settings_tmp_path, settings_path);
}

bool UltimateService::removeIfExists(const char* path) {
  return !filesystem->exists(path) || filesystem->remove(path);
}

bool UltimateService::loadMeta() {
  auto readCopy = [this](const char* path, JournalMeta& copy) {
    File file = filesystem->open(path, FILE_READ);
    const bool complete = file && file.size() == sizeof(copy) &&
        file.read(reinterpret_cast<uint8_t*>(&copy), sizeof(copy)) == sizeof(copy);
    if (file) file.close();
    return complete && copy.magic == meta_magic && copy.version == format_version &&
        validCapacity(copy.capacity) && copy.capacity != 0 && copy.count <= copy.capacity &&
        copy.head < copy.capacity &&
        copy.crc32 == crc32(&copy, offsetof(JournalMeta, crc32));
  };

  JournalMeta a = {};
  JournalMeta b = {};
  const bool a_valid = readCopy(meta_a_path, a);
  const bool b_valid = readCopy(meta_b_path, b);
  if (a_valid || b_valid) {
    meta = !b_valid || (a_valid && generationAfter(a.generation, b.generation)) ? a : b;
    return true;
  }

  memset(&meta, 0, sizeof(meta));
  meta.magic = meta_magic;
  meta.version = format_version;
  meta.capacity = settings.history_capacity == 0 ? 512 : settings.history_capacity;
  meta.next_sequence = 1;
  return true;
}

bool UltimateService::writeMeta() {
  JournalMeta next = meta;
  next.magic = meta_magic;
  next.version = format_version;
  next.generation = meta.generation + 1;
  next.crc32 = crc32(&next, offsetof(JournalMeta, crc32));
  const char* path = (next.generation & 1U) ? meta_a_path : meta_b_path;
  File file = filesystem->open(path, FILE_WRITE);
  const bool written = file &&
      file.write(reinterpret_cast<const uint8_t*>(&next), sizeof(next)) == sizeof(next);
  if (file) {
    file.flush();
    file.close();
  }
  if (!written) return false;
  meta = next;
  return true;
}

bool UltimateService::recoverJournalSwap() {
  if (!removeIfExists(history_new_path)) return false;
  if (!filesystem->exists(history_old_path)) return true;

  bool current_matches = false;
  if (filesystem->exists(history_path)) {
    File current = filesystem->open(history_path, FILE_READ);
    current_matches = current && current.size() ==
        static_cast<size_t>(meta.capacity) * sizeof(UltimateHistoryRecord);
    if (current) current.close();
  }
  if (current_matches) {
    return removeIfExists(history_old_path);
  }

  if (!removeIfExists(history_path)) return false;
  return filesystem->rename(history_old_path, history_path);
}

bool UltimateService::ensureJournalFile(uint16_t capacity) {
  const size_t expected = static_cast<size_t>(capacity) * sizeof(UltimateHistoryRecord);
  if (filesystem->exists(history_path)) {
    File existing = filesystem->open(history_path, FILE_READ);
    const bool valid = existing && existing.size() == expected;
    if (existing) existing.close();
    return valid;
  }

  File file = filesystem->open(history_path, FILE_WRITE);
  if (!file) return false;
  UltimateHistoryRecord empty = {};
  bool ok = true;
  for (uint16_t slot = 0; slot < capacity && ok; slot++) {
    ok = file.write(reinterpret_cast<const uint8_t*>(&empty), sizeof(empty)) == sizeof(empty);
    if ((slot & 31U) == 31U) yield();
  }
  file.flush();
  file.close();
  return ok;
}

bool UltimateService::readSlot(uint16_t slot, UltimateHistoryRecord& record) const {
  if (slot >= meta.capacity) return false;
  File file = filesystem->open(history_path, FILE_READ);
  if (!file || !file.seek(static_cast<size_t>(slot) * sizeof(record))) {
    if (file) file.close();
    return false;
  }
  const bool complete = file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.close();
  return complete && record.magic == history_magic &&
      record.crc32 == crc32(&record, offsetof(UltimateHistoryRecord, crc32));
}

bool UltimateService::writeSlot(uint16_t slot, const UltimateHistoryRecord& record) {
  if (slot >= meta.capacity) return false;
  File file = filesystem->open(history_path, "r+");
  if (!file || !file.seek(static_cast<size_t>(slot) * sizeof(record))) {
    if (file) file.close();
    return false;
  }
  const bool written = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  return written;
}

bool UltimateService::appendRecord(const PendingEvent& event) {
  if (settings.history_capacity == 0) return true;
  UltimateHistoryRecord record = {};
  record.magic = history_magic;
  record.sequence = meta.next_sequence;
  record.timestamp = event.timestamp;
  record.flags = event.flags;
  record.kind = event.subtype;
  record.path_len = event.path_len;
  record.snr_quarter_db = event.snr_quarter_db;
  record.rssi_dbm = event.rssi_dbm;
  record.target = event.target;
  memcpy(record.peer_key, event.peer_key, sizeof(record.peer_key));
  copyText(record.sender, sizeof(record.sender), event.sender);
  copyText(record.text, sizeof(record.text), event.text);
  record.crc32 = crc32(&record, offsetof(UltimateHistoryRecord, crc32));

  if (!writeSlot(meta.head, record)) return false;
  const JournalMeta previous = meta;
  meta.head = (meta.head + 1) % meta.capacity;
  if (meta.count < meta.capacity) meta.count++;
  meta.next_sequence++;
  if (!writeMeta()) {
    meta = previous;
    return false;
  }

  snapshot.last_sequence = record.sequence;
  snapshot.history_count = meta.count;
  if ((record.flags & (ULTIMATE_HISTORY_INCOMING | ULTIMATE_HISTORY_READ)) ==
      ULTIMATE_HISTORY_INCOMING) {
    if (snapshot.unread_count < 0xFFFF) snapshot.unread_count++;
  }
  updateThread(record, false);
  return true;
}

bool UltimateService::pushEvent(const PendingEvent& event) {
  bool accepted = false;
  portENTER_CRITICAL(&pending_lock);
  if (pending_count < pending_capacity) {
    pending[pending_head] = event;
    pending_head = (pending_head + 1) % pending_capacity;
    pending_count++;
    accepted = true;
  } else {
    snapshot.event_drops++;
  }
  portEXIT_CRITICAL(&pending_lock);
  return accepted;
}

bool UltimateService::popEvent(PendingEvent& event) {
  bool available = false;
  portENTER_CRITICAL(&pending_lock);
  if (pending_count > 0) {
    event = pending[pending_tail];
    pending_tail = (pending_tail + 1) % pending_capacity;
    pending_count--;
    available = true;
  }
  portEXIT_CRITICAL(&pending_lock);
  return available;
}

bool UltimateService::enqueueMessage(
    UltimateMessageKind kind, bool incoming, uint8_t target, const uint8_t peer_key[6],
    const char* sender, const char* text, uint32_t timestamp, uint8_t path_len,
    int16_t rssi_dbm, int8_t snr_quarter_db) {
  PendingEvent event = {};
  event.type = PendingType::Message;
  event.subtype = static_cast<uint8_t>(kind);
  event.flags = incoming ? ULTIMATE_HISTORY_INCOMING : ULTIMATE_HISTORY_READ;
  event.target = target;
  event.timestamp = timestamp ? timestamp : (clock ? clock->getCurrentTime() : 0);
  event.path_len = path_len;
  event.rssi_dbm = rssi_dbm;
  event.snr_quarter_db = snr_quarter_db;
  if (peer_key) memcpy(event.peer_key, peer_key, sizeof(event.peer_key));
  if (kind == UltimateMessageKind::Channel && sender && sender[0] != '#') {
    event.sender[0] = '#';
    copyText(event.sender + 1, sizeof(event.sender) - 1, sender);
  } else {
    copyText(event.sender, sizeof(event.sender), sender);
  }
  copyText(event.text, sizeof(event.text), text);
  return pushEvent(event);
}

bool UltimateService::enqueueRadio(
    UltimateRadioEvent radio_event, uint8_t payload_type, int16_t rssi_dbm,
    int8_t snr_quarter_db, uint32_t airtime_ms) {
  PendingEvent event = {};
  event.type = PendingType::Radio;
  event.subtype = static_cast<uint8_t>(radio_event);
  event.target = payload_type;
  event.timestamp = clock ? clock->getCurrentTime() : 0;
  event.airtime_ms = airtime_ms;
  event.rssi_dbm = rssi_dbm;
  event.snr_quarter_db = snr_quarter_db;
  return pushEvent(event);
}

bool UltimateService::enqueueNode(
    const uint8_t peer_key[6], const char* name, uint8_t role, uint8_t path_len,
    uint32_t timestamp, int16_t rssi_dbm, int8_t snr_quarter_db,
    bool signal_attributable) {
  PendingEvent event = {};
  event.type = PendingType::Node;
  event.flags = signal_attributable ? 1 : 0;
  event.role = role;
  event.path_len = path_len;
  event.timestamp = timestamp;
  event.rssi_dbm = rssi_dbm;
  event.snr_quarter_db = snr_quarter_db;
  if (peer_key) memcpy(event.peer_key, peer_key, sizeof(event.peer_key));
  copyText(event.sender, sizeof(event.sender), name);
  return pushEvent(event);
}

void UltimateService::updateNetwork(const PendingEvent& event) {
  uint8_t use = network_count;
  uint32_t oldest = UINT32_MAX;
  for (uint8_t i = 0; i < network_count; i++) {
    if (memcmp(network[i].key, event.peer_key, sizeof(network[i].key)) == 0) {
      use = i;
      break;
    }
    if (network[i].last_seen < oldest) {
      oldest = network[i].last_seen;
      if (network_count == network_capacity) use = i;
    }
  }
  if (use == network_count && network_count < network_capacity) network_count++;

  UltimateNetworkNode node = use < network_count ? network[use] : UltimateNetworkNode{};
  memcpy(node.key, event.peer_key, sizeof(node.key));
  copyText(node.name, sizeof(node.name), event.sender);
  node.last_seen = event.timestamp;
  node.packet_count++;
  node.role = event.role;
  node.path_len = event.path_len;
  node.signal_attributable = event.flags != 0;
  node.rssi_dbm = node.signal_attributable ? event.rssi_dbm : 0;
  node.snr_quarter_db = node.signal_attributable ? event.snr_quarter_db : 0;
  if (use > 0) memmove(&network[1], &network[0], use * sizeof(network[0]));
  network[0] = node;
}

void UltimateService::updateThread(const UltimateHistoryRecord& record, bool newest_first) {
  uint8_t found = thread_count;
  for (uint8_t i = 0; i < thread_count; i++) {
    const bool same = threads[i].kind == record.kind &&
        (record.kind == static_cast<uint8_t>(UltimateMessageKind::Channel)
             ? threads[i].target == record.target
             : memcmp(threads[i].peer_key, record.peer_key, sizeof(record.peer_key)) == 0);
    if (same) {
      found = i;
      break;
    }
  }

  if (found == thread_count) {
    if (thread_count < thread_capacity) {
      thread_count++;
    } else if (newest_first) {
      return;
    } else {
      found = thread_capacity - 1;
    }
  }

  UltimateThreadSummary summary = found < thread_count ? threads[found] : UltimateThreadSummary{};
  if (summary.newest_sequence == 0 || record.sequence >= summary.newest_sequence) {
    summary.kind = record.kind;
    summary.target = record.target;
    memcpy(summary.peer_key, record.peer_key, sizeof(summary.peer_key));
    copyText(summary.label, sizeof(summary.label), record.sender);
    summary.newest_sequence = record.sequence;
  }
  if ((record.flags & (ULTIMATE_HISTORY_INCOMING | ULTIMATE_HISTORY_READ)) ==
      ULTIMATE_HISTORY_INCOMING && summary.unread < 0xFFFF) {
    summary.unread++;
  }

  if (!newest_first && found > 0) {
    memmove(&threads[1], &threads[0], found * sizeof(threads[0]));
    threads[0] = summary;
  } else {
    threads[found] = summary;
  }
}

void UltimateService::rebuildThreads() {
  memset(threads, 0, sizeof(threads));
  thread_count = 0;
  snapshot.unread_count = 0;
  if (meta.count == 0 || meta.capacity == 0) return;

  File file = filesystem->open(history_path, FILE_READ);
  if (!file) return;
  for (uint16_t offset = 0; offset < meta.count; offset++) {
    const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
    UltimateHistoryRecord record;
    const bool valid = file.seek(static_cast<size_t>(slot) * sizeof(record)) &&
        file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) &&
        record.magic == history_magic &&
        record.crc32 == crc32(&record, offsetof(UltimateHistoryRecord, crc32));
    if (valid) {
      updateThread(record, true);
      if ((record.flags & (ULTIMATE_HISTORY_INCOMING | ULTIMATE_HISTORY_READ)) ==
              ULTIMATE_HISTORY_INCOMING && snapshot.unread_count < 0xFFFF) {
        snapshot.unread_count++;
      }
    }
    if ((offset & 31U) == 31U) yield();
  }
  file.close();
}

void UltimateService::updateMetrics(const PendingEvent& event) {
  const uint32_t now = event.timestamp ? event.timestamp : (clock ? clock->getCurrentTime() : 0);
  const uint32_t hour = now / 3600U;
  const uint16_t slot = hour % hourly_capacity;
  UltimateMetricBucket& bucket = hourly[slot];
  if (bucket.hour != hour) {
    memset(&bucket, 0, sizeof(bucket));
    bucket.hour = hour;
  }
  if (event.type == PendingType::Radio) {
    const UltimateRadioEvent kind = static_cast<UltimateRadioEvent>(event.subtype);
    if (kind == UltimateRadioEvent::Rx) {
      bucket.rx_packets++;
      bucket.rssi_total += event.rssi_dbm;
      bucket.snr_quarter_total += event.snr_quarter_db;
      if (bucket.signal_samples < 0xFFFF) bucket.signal_samples++;
    } else if (kind == UltimateRadioEvent::Tx) {
      bucket.tx_packets++;
    } else if (kind == UltimateRadioEvent::TxFailed) {
      bucket.tx_failures++;
    }
    bucket.airtime_ms += event.airtime_ms;
  }
  bucket.battery_mv = snapshot.battery_mv;
  bucket.crc32 = crc32(&bucket, offsetof(UltimateMetricBucket, crc32));
}

void UltimateService::processEvent(const PendingEvent& event) {
  if (event.type == PendingType::Message) {
    if (!appendRecord(event)) snapshot.event_drops++;
  } else if (event.type == PendingType::Node) {
    updateNetwork(event);
  } else if (event.type == PendingType::Radio) {
    const UltimateRadioEvent kind = static_cast<UltimateRadioEvent>(event.subtype);
    if (kind == UltimateRadioEvent::Rx) {
      snapshot.rx_packets++;
      snapshot.last_rssi_dbm = event.rssi_dbm;
      snapshot.last_snr_quarter_db = event.snr_quarter_db;
      snapshot.last_signal_valid = true;
      last_signal_millis = millis();
    } else if (kind == UltimateRadioEvent::Tx) {
      snapshot.tx_packets++;
    } else if (kind == UltimateRadioEvent::TxFailed) {
      snapshot.tx_failures++;
    }
    snapshot.airtime_ms += event.airtime_ms;
  }
  updateMetrics(event);
}

bool UltimateService::saveMetricBucket(uint16_t slot) {
  if (slot >= hourly_capacity) return false;
  File file = filesystem->open(metrics_path, "r+");
  if (!file) return false;
  UltimateMetricBucket bucket = hourly[slot];
  bucket.crc32 = crc32(&bucket, offsetof(UltimateMetricBucket, crc32));
  const bool ok = file.seek(static_cast<size_t>(slot) * sizeof(bucket)) &&
      file.write(reinterpret_cast<const uint8_t*>(&bucket), sizeof(bucket)) == sizeof(bucket);
  file.flush();
  file.close();
  return ok;
}

void UltimateService::loadMetrics() {
  memset(hourly, 0, sizeof(hourly));
  File file = filesystem->open(metrics_path, FILE_READ);
  if (file && file.size() == sizeof(hourly)) {
    file.read(reinterpret_cast<uint8_t*>(hourly), sizeof(hourly));
    file.close();
    for (uint16_t i = 0; i < hourly_capacity; i++) {
      if (hourly[i].crc32 != crc32(&hourly[i], offsetof(UltimateMetricBucket, crc32))) {
        memset(&hourly[i], 0, sizeof(hourly[i]));
      }
    }
    return;
  }
  if (file) file.close();
  file = filesystem->open(metrics_path, FILE_WRITE);
  if (file) {
    file.write(reinterpret_cast<const uint8_t*>(hourly), sizeof(hourly));
    file.flush();
    file.close();
  }
}

void UltimateService::sampleHighResolution() {
  HighResolutionMetric& sample = high_resolution[high_resolution_head];
  sample.timestamp = clock->getCurrentTime();
  sample.rx_packets = static_cast<uint16_t>(snapshot.rx_packets - last_metric_rx);
  sample.tx_packets = static_cast<uint16_t>(snapshot.tx_packets - last_metric_tx);
  sample.tx_failures = static_cast<uint16_t>(snapshot.tx_failures - last_metric_fail);
  sample.rssi_dbm = snapshot.last_rssi_dbm;
  sample.snr_quarter_db = snapshot.last_snr_quarter_db;
  sample.queue_depth = snapshot.outbound_queue_depth;
  sample.battery_mv = snapshot.battery_mv;
  last_metric_rx = snapshot.rx_packets;
  last_metric_tx = snapshot.tx_packets;
  last_metric_fail = snapshot.tx_failures;
  high_resolution_head = (high_resolution_head + 1) % high_resolution_capacity;
  if (high_resolution_count < high_resolution_capacity) high_resolution_count++;
}

void UltimateService::sampleStatus() {
  snapshot.uptime_seconds = millis() / 1000U;
  snapshot.free_heap = ESP.getFreeHeap();
  snapshot.largest_allocation = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  snapshot.storage_used_kb = store->getStorageUsedKb();
  snapshot.storage_total_kb = store->getStorageTotalKb();
  snapshot.battery_mv = board->getBattMilliVolts();
  snapshot.history_count = meta.count;
  snapshot.history_capacity = settings.history_capacity;

  if (snapshot.last_signal_valid && millis() - last_signal_millis > 120000U) {
    snapshot.last_signal_valid = false;
    snapshot.last_rssi_dbm = 0;
    snapshot.last_snr_quarter_db = 0;
  }

  const uint32_t now = clock->getCurrentTime();
  for (uint8_t i = 0; i < network_count; i++) {
    if (network[i].signal_attributable && now - network[i].last_seen > 120) {
      network[i].signal_attributable = false;
      network[i].rssi_dbm = 0;
      network[i].snr_quarter_db = 0;
    }
  }
  sampleHighResolution();
  const uint32_t hour = now / 3600U;
  if (last_hour != 0 && hour != last_hour) {
    saveMetricBucket(last_hour % hourly_capacity);
  }
  last_hour = hour;
}

bool UltimateService::begin(
    fs::FS& fs, mesh::RTCClock& rtc, mesh::MainBoard& main_board, DataStore& data_store) {
  filesystem = &fs;
  clock = &rtc;
  board = &main_board;
  store = &data_store;
  if (!filesystem->exists("/np") && !filesystem->mkdir("/np")) {
    Serial.println("Ultimate: cannot create /np storage namespace");
    return false;
  }
  if (!loadSettings() || !loadMeta() || !recoverJournalSwap() ||
      !ensureJournalFile(meta.capacity)) {
    Serial.println("Ultimate: journal initialization failed");
    return false;
  }
  if (meta.generation == 0 && !writeMeta()) return false;
  if (settings.history_capacity != 0 && settings.history_capacity != meta.capacity &&
      !setHistoryCapacity(settings.history_capacity)) {
    return false;
  }

  snapshot.history_count = meta.count;
  snapshot.history_capacity = settings.history_capacity;
  snapshot.last_sequence = meta.next_sequence ? meta.next_sequence - 1 : 0;
  rebuildThreads();
  loadMetrics();
  last_hour = clock->getCurrentTime() / 3600U;
  sampleStatus();
  next_status_sample = millis() + 60000;
  Serial.print("Ultimate: history ready, capacity=");
  Serial.print(settings.history_capacity);
  Serial.print(" records=");
  Serial.println(meta.count);
  Serial.print("Ultimate: build=");
  Serial.println(getBuildSha());
  return true;
}

void UltimateService::loop() {
  PendingEvent event;
  uint8_t budget = 1;  // keep journal writes inside one 66 ms UI frame budget
  while (budget-- && popEvent(event)) processEvent(event);
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_status_sample) >= 0) {
    next_status_sample = now + 60000;
    sampleStatus();
  }
}

void UltimateService::refreshStatusNow() {
  sampleStatus();
  next_status_sample = millis() + 60000;
}

const UltimateNetworkNode* UltimateService::getNetworkNode(uint8_t index) const {
  return index < network_count ? &network[index] : nullptr;
}

const UltimateThreadSummary* UltimateService::getThread(uint8_t index) const {
  return index < thread_count ? &threads[index] : nullptr;
}

const UltimateMetricBucket* UltimateService::getHourlyMetricNewest(uint16_t offset) const {
  if (offset >= hourly_capacity || clock == nullptr) return nullptr;
  const uint32_t hour = clock->getCurrentTime() / 3600U;
  const UltimateMetricBucket& bucket = hourly[(hour + hourly_capacity - offset) % hourly_capacity];
  return bucket.hour == hour - offset ? &bucket : nullptr;
}

bool UltimateService::getHighResolutionNewest(
    uint8_t offset, uint32_t& timestamp, uint16_t& rx, uint16_t& tx,
    uint16_t& failures, int16_t& rssi, int8_t& snr_quarter,
    uint8_t& queue_depth, uint16_t& battery_mv) const {
  if (offset >= high_resolution_count) return false;
  const uint8_t index = (high_resolution_head + high_resolution_capacity - 1 - offset) %
      high_resolution_capacity;
  const HighResolutionMetric& sample = high_resolution[index];
  timestamp = sample.timestamp;
  rx = sample.rx_packets;
  tx = sample.tx_packets;
  failures = sample.tx_failures;
  rssi = sample.rssi_dbm;
  snr_quarter = sample.snr_quarter_db;
  queue_depth = sample.queue_depth;
  battery_mv = sample.battery_mv;
  return true;
}

bool UltimateService::getHistoryNewest(uint16_t offset, UltimateHistoryRecord& record) const {
  if (offset >= meta.count || meta.capacity == 0) return false;
  const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
  return readSlot(slot, record);
}

bool UltimateService::getNewestUnread(UltimateHistoryRecord& record) const {
  struct Search {
    UltimateHistoryRecord* output;
    bool found;
  } search = {&record, false};
  visitHistory(0, 0, false,
      [](const UltimateHistoryRecord& candidate, void* context) {
        Search* result = static_cast<Search*>(context);
        if ((candidate.flags & (ULTIMATE_HISTORY_INCOMING | ULTIMATE_HISTORY_READ)) ==
            ULTIMATE_HISTORY_INCOMING) {
          *result->output = candidate;
          result->found = true;
          return false;
        }
        return true;
      },
      &search);
  return search.found;
}

uint16_t UltimateService::visitHistory(uint32_t before_sequence, uint16_t limit,
    bool oldest_first, UltimateHistoryVisitor visitor, void* context) const {
  if (meta.count == 0 || meta.capacity == 0 || filesystem == nullptr || visitor == nullptr) {
    return 0;
  }
  File file = filesystem->open(history_path, FILE_READ);
  if (!file) return 0;
  uint16_t visited = 0;
  for (uint16_t step = 0; step < meta.count && (limit == 0 || visited < limit); step++) {
    const uint16_t offset = oldest_first ? meta.count - 1 - step : step;
    const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
    UltimateHistoryRecord record;
    const bool valid = file.seek(static_cast<size_t>(slot) * sizeof(record)) &&
        file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) &&
        record.magic == history_magic &&
        record.crc32 == crc32(&record, offsetof(UltimateHistoryRecord, crc32));
    if (valid && (before_sequence == 0 || record.sequence < before_sequence)) {
      visited++;
      if (!visitor(record, context)) break;
    }
    if ((step & 31U) == 31U) yield();
  }
  file.close();
  return visited;
}

bool UltimateService::getThreadMessage(uint8_t kind, uint8_t target,
    const uint8_t* peer_key, uint16_t ordinal, UltimateHistoryRecord& record) const {
  if (meta.count == 0 || meta.capacity == 0 || filesystem == nullptr) return false;
  File file = filesystem->open(history_path, "r");
  if (!file) return false;
  uint16_t matched = 0;
  bool found = false;
  for (uint16_t offset = 0; offset < meta.count; offset++) {
    const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
    UltimateHistoryRecord candidate;
    if (!file.seek(static_cast<size_t>(slot) * sizeof(candidate)) ||
        file.read(reinterpret_cast<uint8_t*>(&candidate), sizeof(candidate)) != sizeof(candidate) ||
        candidate.magic != history_magic ||
        candidate.crc32 != crc32(&candidate, offsetof(UltimateHistoryRecord, crc32)) ||
        candidate.kind != kind) {
      continue;
    }
    const bool same = kind == static_cast<uint8_t>(UltimateMessageKind::Channel)
        ? candidate.target == target
        : peer_key != nullptr && memcmp(candidate.peer_key, peer_key, sizeof(candidate.peer_key)) == 0;
    if (same && matched++ == ordinal) {
      record = candidate;
      found = true;
      break;
    }
    if ((offset & 31U) == 31U) yield();
  }
  file.close();
  return found;
}

bool UltimateService::markRead(uint32_t sequence) {
  File file = filesystem->open(history_path, "r+");
  if (!file) return false;
  UltimateHistoryRecord record;
  for (uint16_t offset = 0; offset < meta.count; offset++) {
    const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
    const size_t position = static_cast<size_t>(slot) * sizeof(record);
    const bool valid = file.seek(position) &&
        file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) &&
        record.magic == history_magic &&
        record.crc32 == crc32(&record, offsetof(UltimateHistoryRecord, crc32));
    if (!valid || record.sequence != sequence) {
      if ((offset & 31U) == 31U) yield();
      continue;
    }
    if ((record.flags & ULTIMATE_HISTORY_READ) != 0) {
      file.close();
      return true;
    }
    record.flags |= ULTIMATE_HISTORY_READ;
    record.crc32 = crc32(&record, offsetof(UltimateHistoryRecord, crc32));
    const bool written = file.seek(position) &&
        file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
    file.flush();
    file.close();
    if (!written) return false;
    if (snapshot.unread_count) snapshot.unread_count--;
    for (uint8_t i = 0; i < thread_count; i++) {
      const bool same = threads[i].kind == record.kind &&
          (record.kind == static_cast<uint8_t>(UltimateMessageKind::Channel)
               ? threads[i].target == record.target
               : memcmp(threads[i].peer_key, record.peer_key, sizeof(record.peer_key)) == 0);
      if (same && threads[i].unread) {
        threads[i].unread--;
        break;
      }
    }
    return true;
  }
  file.close();
  return false;
}

bool UltimateService::clearHistory() {
  if (!removeIfExists(history_path)) return false;
  if (!ensureJournalFile(meta.capacity)) return false;
  meta.count = 0;
  meta.head = 0;
  if (!writeMeta()) return false;
  snapshot.history_count = 0;
  snapshot.unread_count = 0;
  memset(threads, 0, sizeof(threads));
  thread_count = 0;
  return true;
}

bool UltimateService::setHistoryCapacity(uint16_t capacity) {
  if (!validCapacity(capacity)) return false;
  if (capacity == 0) {
    settings.history_capacity = 0;
    snapshot.history_capacity = 0;
    return saveSettings();
  }
  if (capacity == meta.capacity) {
    settings.history_capacity = capacity;
    snapshot.history_capacity = capacity;
    return saveSettings();
  }

  const uint16_t retain = meta.count < capacity ? meta.count : capacity;
  if (!removeIfExists(history_new_path)) return false;
  File next = filesystem->open(history_new_path, FILE_WRITE);
  if (!next) return false;
  File current = filesystem->open(history_path, FILE_READ);
  if (!current) {
    next.close();
    removeIfExists(history_new_path);
    return false;
  }
  UltimateHistoryRecord empty = {};
  bool ok = true;
  for (uint16_t slot = 0; slot < capacity && ok; slot++) {
    ok = next.write(reinterpret_cast<const uint8_t*>(&empty), sizeof(empty)) == sizeof(empty);
  }
  UltimateHistoryRecord record;
  for (uint16_t i = 0; i < retain && ok; i++) {
    const uint16_t offset = retain - 1 - i;
    const uint16_t slot = (meta.head + meta.capacity - 1 - offset) % meta.capacity;
    const bool valid = current.seek(static_cast<size_t>(slot) * sizeof(record)) &&
        current.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record) &&
        record.magic == history_magic &&
        record.crc32 == crc32(&record, offsetof(UltimateHistoryRecord, crc32));
    if (!valid ||
        !next.seek(static_cast<size_t>(i) * sizeof(record))) {
      ok = false;
      break;
    }
    ok = next.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
    if ((i & 31U) == 31U) yield();
  }
  current.close();
  next.flush();
  next.close();
  if (!ok) {
    removeIfExists(history_new_path);
    return false;
  }

  if (!removeIfExists(history_old_path)) {
    removeIfExists(history_new_path);
    return false;
  }
  if (!filesystem->rename(history_path, history_old_path) ||
      !filesystem->rename(history_new_path, history_path)) {
    removeIfExists(history_path);
    filesystem->rename(history_old_path, history_path);
    removeIfExists(history_new_path);
    return false;
  }

  const JournalMeta old_meta = meta;
  meta.capacity = capacity;
  meta.count = retain;
  meta.head = retain % capacity;
  if (!writeMeta()) {
    meta = old_meta;
    removeIfExists(history_path);
    filesystem->rename(history_old_path, history_path);
    return false;
  }
  removeIfExists(history_old_path);
  settings.history_capacity = capacity;
  snapshot.history_capacity = capacity;
  snapshot.history_count = retain;
  rebuildThreads();
  return saveSettings();
}

bool UltimateService::updateSettings(const UltimateSettings& updated) {
  if (!validCapacity(updated.history_capacity) ||
      (updated.scan_cadence_ms != 450 && updated.scan_cadence_ms != 650 &&
       updated.scan_cadence_ms != 900 && updated.scan_cadence_ms != 1200)) {
    return false;
  }
  UltimateSettings safe = updated;
  for (uint8_t i = 0; i < 8; i++) {
    safe.quick_phrases[i][sizeof(safe.quick_phrases[i]) - 1] = 0;
  }
  if (safe.history_capacity != settings.history_capacity &&
      !setHistoryCapacity(safe.history_capacity)) {
    return false;
  }
  settings = safe;
  snapshot.history_capacity = settings.history_capacity;
  return saveSettings();
}

#endif
