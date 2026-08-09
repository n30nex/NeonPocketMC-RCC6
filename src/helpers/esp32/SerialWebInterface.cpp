#ifdef RCC6_WEB_AP

#include "SerialWebInterface.h"
#include <Preferences.h>
#include <esp_system.h>
#include <errno.h>
#include <lwip/sockets.h>
#define RCC6_WEB_UI_ASSETS_IMPLEMENTATION
#include <Rcc6WebUiAssets.h>

#include <cstdlib>
#include <cstring>

namespace {
constexpr char PASSWORD_ALPHABET[] = "abcdefghjkmnpqrstuvwxyz";
constexpr char HTTP_SESSION_HEADER[] = "X-RCC6-Session";
constexpr char HTTP_ACK_HEADER[] = "X-RCC6-Ack";
constexpr char HTTP_SEQUENCE_HEADER[] = "X-RCC6-Seq";

bool isSafeSsidChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '-' || c == '_';
}
}

void SerialWebInterface::FrameQueue::clear() {
  head = 0;
  count = 0;
}

bool SerialWebInterface::FrameQueue::full() const {
  return count == FRAME_QUEUE_SIZE;
}

bool SerialWebInterface::FrameQueue::empty() const {
  return count == 0;
}

bool SerialWebInterface::FrameQueue::push(const uint8_t* data, size_t len) {
  if (data == nullptr || len < 1 || len > MAX_FRAME_SIZE || full()) return false;

  Frame& frame = frames[(head + count) % FRAME_QUEUE_SIZE];
  frame.len = static_cast<uint16_t>(len);
  memcpy(frame.data, data, len);
  count++;
  return true;
}

SerialWebInterface::Frame* SerialWebInterface::FrameQueue::front() {
  return empty() ? nullptr : &frames[head];
}

void SerialWebInterface::FrameQueue::pop() {
  if (empty()) return;
  head = (head + 1) % FRAME_QUEUE_SIZE;
  count--;
}

size_t SerialWebInterface::FrameQueue::pop(uint8_t* dest) {
  Frame* frame = front();
  if (frame == nullptr || dest == nullptr) return 0;

  const size_t len = frame->len;
  memcpy(dest, frame->data, len);
  pop();
  return len;
}

SerialWebInterface::SerialWebInterface()
  : _initialized(false), _enabled(false), _routes_registered(false),
    _tcp_connected(false), _tcp_port(5000), _ap_ssid{}, _ap_password{},
    _tcp_server(), _tcp_client(), _http_server(80), _session(Session::NONE),
    _http_remote(), _http_session_id{}, _http_last_activity(0),
    _http_inflight_seq(0), _http_next_seq(1), _recv_queue{}, _send_queue{},
    _tcp_rx_state(TcpRxState::MARKER), _tcp_rx_length(0), _tcp_rx_offset(0),
    _tcp_rx_data{}, _tcp_tx_data{}, _tcp_tx_length(0), _tcp_tx_offset(0),
    _http_post_result(HttpPostResult::NONE), _http_post_expected(0),
    _http_post_length(0), _http_post_data{} {
  _recv_queue.clear();
  _send_queue.clear();
}

void SerialWebInterface::begin(const char* node_name, uint16_t tcp_port) {
  _tcp_port = tcp_port == 0 ? 5000 : tcp_port;
  configureIdentity(node_name);
  registerHttpRoutes();
  _initialized = true;
}

IPAddress SerialWebInterface::getApIP() const {
  return IPAddress(192, 168, 4, 1);
}

void SerialWebInterface::configureIdentity(const char* node_name) {
  char suffix[24] = {};
  size_t out = 0;
  bool last_was_separator = false;

  if (node_name != nullptr) {
    for (size_t i = 0; node_name[i] != '\0' && out < sizeof(suffix) - 1; i++) {
      const char c = node_name[i];
      if (isSafeSsidChar(c)) {
        suffix[out++] = c;
        last_was_separator = false;
      } else if (out > 0 && !last_was_separator) {
        suffix[out++] = '-';
        last_was_separator = true;
      }
    }
  }
  while (out > 0 && suffix[out - 1] == '-') out--;
  suffix[out] = '\0';
  if (out == 0) strcpy(suffix, "RCC6");
  snprintf(_ap_ssid, sizeof(_ap_ssid), "MeshCore-%s", suffix);

  Preferences preferences;
  String password;
  const bool opened = preferences.begin("rcc6-web", false);
  if (opened) password = preferences.getString("ap-pass", "");

  if (password.length() != sizeof(_ap_password) - 1) {
    for (size_t i = 0; i < sizeof(_ap_password) - 1; i++) {
      _ap_password[i] = PASSWORD_ALPHABET[esp_random() % (sizeof(PASSWORD_ALPHABET) - 1)];
    }
    _ap_password[sizeof(_ap_password) - 1] = '\0';
    if (opened) preferences.putString("ap-pass", _ap_password);
  } else {
    password.toCharArray(_ap_password, sizeof(_ap_password));
  }

  if (opened) preferences.end();
}

void SerialWebInterface::registerHttpRoutes() {
  if (_routes_registered) return;

  static const char* collected_headers[] = { HTTP_SESSION_HEADER, HTTP_ACK_HEADER };
  _http_server.collectHeaders(collected_headers,
      sizeof(collected_headers) / sizeof(collected_headers[0]));
  _http_server.on("/", HTTP_GET, [this]() { handleIndex(); });
  _http_server.on("/index.html", HTTP_GET, [this]() { handleIndex(); });
  _http_server.on("/api/frame", HTTP_GET, [this]() { handleHttpGetFrame(); });
  _http_server.on("/api/frame", HTTP_POST,
      [this]() { handleHttpPostFrame(); },
      [this]() { handleHttpPostRaw(); });
  _http_server.onNotFound([this]() { sendHttpStatus(404); });
  _routes_registered = true;
}

void SerialWebInterface::enable() {
  if (_enabled || !_initialized) return;

  clearSession();
  WiFi.mode(WIFI_AP);
  const IPAddress ap_ip = getApIP();
  if (!WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(_ap_ssid, _ap_password, 1, false, 1)) {
    WiFi.softAPdisconnect(true);
    Serial.println("Web AP: failed to start");
    return;
  }

  _tcp_server.begin(_tcp_port);
  _tcp_server.setNoDelay(true);
  _http_server.begin();
  _enabled = true;
  Serial.printf("Web AP: %s at %s (HTTP, TCP/%u)\n",
      _ap_ssid, ap_ip.toString().c_str(), _tcp_port);
}

void SerialWebInterface::disable() {
  if (!_enabled) return;

  _enabled = false;
  _http_server.stop();
  _tcp_server.end();
  clearSession();
  WiFi.softAPdisconnect(true);
}

bool SerialWebInterface::isConnected() const {
  if (!_enabled) return false;
  if (_session == Session::TCP) return _tcp_connected;
  if (_session == Session::HTTP) {
    return static_cast<uint32_t>(millis() - _http_last_activity) < HTTP_SESSION_TIMEOUT_MS;
  }
  return false;
}

bool SerialWebInterface::isWriteBusy() const {
  return static_cast<uint16_t>(_recv_queue.count) + _send_queue.count >= FRAME_QUEUE_SIZE;
}

size_t SerialWebInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!isConnected() || isWriteBusy() || src == nullptr || len < 1 || len > MAX_FRAME_SIZE) return 0;
  return _send_queue.push(src, len) ? len : 0;
}

size_t SerialWebInterface::checkRecvFrame(uint8_t dest[]) {
  if (!_enabled) return 0;

  expireHttpSession();
  _http_server.handleClient();
  serviceTcp();
  return _recv_queue.pop(dest);
}

void SerialWebInterface::clearSession() {
  if (_tcp_client) _tcp_client.stop();
  _tcp_client = WiFiClient();
  _tcp_connected = false;
  _session = Session::NONE;
  _http_remote = IPAddress();
  _http_session_id[0] = '\0';
  _http_last_activity = 0;
  _http_inflight_seq = 0;
  _recv_queue.clear();
  _send_queue.clear();
  _tcp_tx_length = 0;
  _tcp_tx_offset = 0;
  _http_post_result = HttpPostResult::NONE;
  _http_post_expected = 0;
  _http_post_length = 0;
  resetTcpParser();
}

void SerialWebInterface::resetTcpParser() {
  _tcp_rx_state = TcpRxState::MARKER;
  _tcp_rx_length = 0;
  _tcp_rx_offset = 0;
}

void SerialWebInterface::expireHttpSession() {
  if (_session == Session::HTTP &&
      static_cast<uint32_t>(millis() - _http_last_activity) >= HTTP_SESSION_TIMEOUT_MS &&
      _recv_queue.empty() && _send_queue.empty() && _http_inflight_seq == 0) {
    clearSession();
  }
}

bool SerialWebInterface::httpSessionTokenValid(const String& token) const {
  if (token.length() != 32) return false;
  for (size_t i = 0; i < 32; i++) {
    const char c = token[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

bool SerialWebInterface::claimHttpSession() {
  expireHttpSession();
  const IPAddress remote = _http_server.client().remoteIP();
  const String token = _http_server.header(HTTP_SESSION_HEADER);

  if (!httpSessionTokenValid(token)) return false;

  if (_session == Session::TCP) {
    if (_tcp_client.connected()) return false;
    clearSession();
  }
  if (_session == Session::HTTP &&
      (_http_remote != remote || strncmp(_http_session_id, token.c_str(), 32) != 0)) return false;
  if (_session == Session::NONE) {
    _session = Session::HTTP;
    _http_remote = remote;
    token.toCharArray(_http_session_id, sizeof(_http_session_id));
  }
  _http_last_activity = millis();
  return true;
}

bool SerialWebInterface::acknowledgeHttpFrame() {
  if (_http_inflight_seq == 0) return false;
  const String value = _http_server.header(HTTP_ACK_HEADER);
  if (value.length() < 1 || value.length() > 10) return false;

  char* end = nullptr;
  const unsigned long ack = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || ack != _http_inflight_seq) return false;

  _send_queue.pop();
  _http_inflight_seq = 0;
  return true;
}

uint32_t SerialWebInterface::nextHttpSequence() {
  uint32_t sequence = _http_next_seq++;
  if (sequence == 0) sequence = _http_next_seq++;
  return sequence;
}

void SerialWebInterface::rejectTcpClient(WiFiClient& client) {
  client.stop();
}

void SerialWebInterface::serviceTcp() {
  if (_session == Session::TCP && !_tcp_client.connected()) clearSession();

  WiFiClient incoming = _tcp_server.accept();
  if (incoming) {
    if (_session == Session::NONE) {
      _tcp_client = incoming;
      _tcp_client.setNoDelay(true);
      _tcp_connected = true;
      _session = Session::TCP;
      resetTcpParser();
    } else {
      rejectTcpClient(incoming);
    }
  }

  if (_session != Session::TCP) return;

  serviceTcpWrite();
  serviceTcpRead();
}

void SerialWebInterface::serviceTcpRead() {
  size_t budget = TCP_READ_BUDGET;
  while (budget-- > 0 && _tcp_client.available() > 0) {
    if (_tcp_rx_state == TcpRxState::MARKER && isWriteBusy()) return;

    const int value = _tcp_client.read();
    if (value < 0) return;
    const uint8_t byte = static_cast<uint8_t>(value);

    switch (_tcp_rx_state) {
      case TcpRxState::MARKER:
        if (byte == '<') _tcp_rx_state = TcpRxState::LENGTH_LOW;
        break;
      case TcpRxState::LENGTH_LOW:
        _tcp_rx_length = byte;
        _tcp_rx_state = TcpRxState::LENGTH_HIGH;
        break;
      case TcpRxState::LENGTH_HIGH:
        _tcp_rx_length |= static_cast<uint16_t>(byte) << 8;
        if (_tcp_rx_length < 1 || _tcp_rx_length > MAX_FRAME_SIZE) {
          _tcp_client.stop();
          clearSession();
          return;
        }
        _tcp_rx_offset = 0;
        _tcp_rx_state = TcpRxState::PAYLOAD;
        break;
      case TcpRxState::PAYLOAD:
        _tcp_rx_data[_tcp_rx_offset++] = byte;
        if (_tcp_rx_offset == _tcp_rx_length) {
          _recv_queue.push(_tcp_rx_data, _tcp_rx_length);
          resetTcpParser();
        }
        break;
    }
  }
}

void SerialWebInterface::serviceTcpWrite() {
  if (_tcp_tx_length == 0) {
    Frame* frame = _send_queue.front();
    if (frame == nullptr) return;
    _tcp_tx_data[0] = '>';
    _tcp_tx_data[1] = frame->len & 0xff;
    _tcp_tx_data[2] = frame->len >> 8;
    memcpy(&_tcp_tx_data[3], frame->data, frame->len);
    _tcp_tx_length = frame->len + 3;
    _tcp_tx_offset = 0;
  }

  const int fd = _tcp_client.fd();
  if (fd < 0) return;
  const int sent = ::send(fd, _tcp_tx_data + _tcp_tx_offset,
      _tcp_tx_length - _tcp_tx_offset, MSG_DONTWAIT);
  if (sent > 0) {
    _tcp_tx_offset += static_cast<uint16_t>(sent);
    if (_tcp_tx_offset == _tcp_tx_length) {
      _send_queue.pop();
      _tcp_tx_length = 0;
      _tcp_tx_offset = 0;
    }
  } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    _tcp_client.stop();
    clearSession();
  }
}

void SerialWebInterface::handleIndex() {
  _http_server.sendHeader("Content-Encoding", "gzip");
  _http_server.sendHeader("Cache-Control", "no-store");
  _http_server.sendHeader("X-Content-Type-Options", "nosniff");
  _http_server.send_P(200, PSTR("text/html; charset=utf-8"),
      reinterpret_cast<PGM_P>(RCC6_WEB_UI_INDEX_GZ), RCC6_WEB_UI_INDEX_GZ_LEN);
}

void SerialWebInterface::handleHttpGetFrame() {
  if (!claimHttpSession()) {
    sendHttpStatus(409);
    return;
  }

  acknowledgeHttpFrame();
  Frame* frame = _send_queue.front();
  if (frame == nullptr) {
    sendHttpStatus(204);
    return;
  }

  if (_http_inflight_seq == 0) _http_inflight_seq = nextHttpSequence();
  _http_server.sendHeader("Cache-Control", "no-store");
  _http_server.sendHeader(HTTP_SEQUENCE_HEADER, String(_http_inflight_seq));
  _http_server.setContentLength(frame->len);
  _http_server.send(200, "application/octet-stream", "");
  _http_server.sendContent(reinterpret_cast<const char*>(frame->data), frame->len);
}

void SerialWebInterface::handleHttpPostRaw() {
  HTTPRaw& raw = _http_server.raw();
  switch (raw.status) {
    case RAW_START:
      _http_post_length = 0;
      _http_post_expected = static_cast<uint16_t>(_http_server.clientContentLength());
      _http_server.client().setTimeout(HTTP_BODY_TIMEOUT_MS);
      if (_http_server.clientContentLength() < 1 ||
          _http_server.clientContentLength() > MAX_FRAME_SIZE) {
        _http_post_result = HttpPostResult::INVALID;
        _http_server.client().stop();
      } else if (!claimHttpSession()) {
        _http_post_result = HttpPostResult::CONFLICT;
      } else if (isWriteBusy()) {
        _http_post_result = HttpPostResult::FULL;
      } else {
        _http_post_result = HttpPostResult::PENDING;
      }
      break;
    case RAW_WRITE:
      if (_http_post_result == HttpPostResult::PENDING) {
        if (_http_post_length + raw.currentSize > MAX_FRAME_SIZE) {
          _http_post_result = HttpPostResult::INVALID;
        } else {
          memcpy(_http_post_data + _http_post_length, raw.buf, raw.currentSize);
          _http_post_length += raw.currentSize;
        }
      }
      break;
    case RAW_END:
      if (_http_post_result == HttpPostResult::PENDING) {
        if (_http_post_length != _http_post_expected) {
          _http_post_result = HttpPostResult::INVALID;
        } else if (_recv_queue.push(_http_post_data, _http_post_length)) {
          _http_post_result = HttpPostResult::ACCEPTED;
        } else {
          _http_post_result = HttpPostResult::FULL;
        }
      }
      break;
    case RAW_ABORTED:
      _http_post_result = HttpPostResult::ABORTED;
      break;
  }
}

void SerialWebInterface::handleHttpPostFrame() {
  switch (_http_post_result) {
    case HttpPostResult::ACCEPTED:
      sendHttpStatus(204);
      break;
    case HttpPostResult::CONFLICT:
      sendHttpStatus(409);
      break;
    case HttpPostResult::FULL:
      sendHttpStatus(429);
      break;
    case HttpPostResult::INVALID:
    case HttpPostResult::ABORTED:
    case HttpPostResult::NONE:
    case HttpPostResult::PENDING:
      sendHttpStatus(400);
      break;
  }
  _http_post_result = HttpPostResult::NONE;
}

void SerialWebInterface::sendHttpStatus(int status) {
  _http_server.sendHeader("Cache-Control", "no-store");
  _http_server.send(status, "text/plain", "");
}

#endif
