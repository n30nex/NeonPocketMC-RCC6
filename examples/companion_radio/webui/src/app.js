import Connection from "@liamcottle/meshcore.js/src/connection/connection.js";
import Constants from "@liamcottle/meshcore.js/src/constants.js";

const MAX_FRAME_BYTES = 176;
const MAX_MESSAGE_BYTES = 140;
const POLL_DELAY_MS = 90;
const $ = (id) => document.getElementById(id);
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const encoder = new TextEncoder();

function createSessionId() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
}

function loadSessionId() {
  try {
    const saved = sessionStorage.getItem("rcc6-session");
    if (/^[0-9a-f]{32}$/.test(saved || "")) return saved;
    const created = createSessionId();
    sessionStorage.setItem("rcc6-session", created);
    return created;
  } catch {
    return createSessionId();
  }
}

class HttpPollingConnection extends Connection {
  constructor() {
    super();
    this.running = false;
    this.failures = 0;
    this.interrupted = false;
    this.sessionId = loadSessionId();
    this.sequenceStorageKey = `rcc6-last-sequence-${this.sessionId}`;
    try { this.lastDispatchedSequence = Number(sessionStorage.getItem(this.sequenceStorageKey)) || 0; }
    catch { this.lastDispatchedSequence = 0; }
    this.pendingAck = this.lastDispatchedSequence;
  }

  async connect() {
    if (this.running) return;
    this.running = true;
    this.abortController = new AbortController();
    this.pollTask = this.poll();
    this.emit("connected");
  }

  async close() {
    if (!this.running) return;
    this.running = false;
    this.abortController.abort();
    try { await this.pollTask; } catch { /* aborted */ }
    this.onDisconnected();
  }

  async sendToRadioFrame(data) {
    const frame = data instanceof Uint8Array ? data : new Uint8Array(data);
    if (!frame.length || frame.length > MAX_FRAME_BYTES) throw new Error("Invalid companion frame size");
    try {
      const response = await fetch("/api/frame", {
        method: "POST",
        headers: {
          "Content-Type": "application/octet-stream",
          "X-RCC6-Session": this.sessionId,
        },
        body: frame,
        cache: "no-store",
        credentials: "same-origin",
        signal: this.abortController.signal,
      });
      if (!response.ok) throw new Error(`Frame POST failed (${response.status})`);
      this.markHealthy();
    } catch (error) {
      if (error.name !== "AbortError") this.markFailure(error);
      throw error;
    }
  }

  async poll() {
    while (this.running) {
      try {
        const headers = {
          Accept: "application/octet-stream",
          "X-RCC6-Session": this.sessionId,
        };
        if (this.pendingAck) headers["X-RCC6-Ack"] = String(this.pendingAck);
        const response = await fetch("/api/frame", {
          headers,
          cache: "no-store",
          credentials: "same-origin",
          signal: this.abortController.signal,
        });
        if (response.status === 204) {
          this.pendingAck = 0;
          this.markHealthy();
          await delay(POLL_DELAY_MS);
          continue;
        }
        if (!response.ok) throw new Error(`Frame poll failed (${response.status})`);
        const sequence = Number(response.headers.get("X-RCC6-Seq"));
        if (!Number.isInteger(sequence) || sequence < 1 || sequence > 0xffffffff) {
          throw new Error("Invalid frame sequence from RCC6");
        }
        const frame = new Uint8Array(await response.arrayBuffer());
        if (!frame.length || frame.length > MAX_FRAME_BYTES) throw new Error("Invalid frame from RCC6");
        if (sequence !== this.lastDispatchedSequence) {
          this.onFrameReceived(frame);
          this.lastDispatchedSequence = sequence;
          try { sessionStorage.setItem(this.sequenceStorageKey, String(sequence)); } catch { /* optional */ }
        }
        this.pendingAck = sequence;
        this.markHealthy();
      } catch (error) {
        if (!this.running || error.name === "AbortError") break;
        this.markFailure(error);
        await delay(Math.min(250 * 2 ** Math.min(this.failures, 3), 2000));
      }
    }
  }

  markFailure(error) {
    this.failures += 1;
    if (this.failures >= 3 && !this.interrupted) {
      this.interrupted = true;
      this.emit("transport-state", "reconnecting", error);
    }
  }

  markHealthy() {
    const restored = this.interrupted;
    this.failures = 0;
    this.interrupted = false;
    if (restored) this.emit("transport-restored");
  }
}

const state = {
  connected: false,
  startedAt: Date.now(),
  view: "home",
  self: null,
  device: null,
  battery: 0,
  contacts: [],
  channels: [],
  messages: [],
  nearby: [],
  selected: "",
  radio: null,
  packets: null,
  rfSamples: [],
};

const connection = new HttpPollingConnection();
let commandTail = Promise.resolve();
let syncing = false;

function exclusive(task) {
  const result = commandTail.then(task);
  commandTail = result.catch(() => {});
  return result;
}

function text(id, value) { $(id).textContent = value ?? "—"; }
function hex(bytes, length = bytes?.length ?? 0) { return bytes ? [...bytes.slice(0, length)].map((value) => value.toString(16).padStart(2, "0")).join("") : ""; }
function channelKey(channel) { return `ch:${channel.channelIdx}`; }
function contactKey(contact) { return `dm:${hex(contact.publicKey)}`; }
function initials(name = "?") { return name.trim().split(/\s+/).slice(0, 2).map((part) => part[0]).join("").toUpperCase() || "?"; }
function formatNumber(value) { return Number.isFinite(value) ? value.toLocaleString() : "—"; }
function formatClock(epochSecs) { return epochSecs ? new Date(epochSecs * 1000).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "now"; }
function formatAge(epochSecs) {
  if (!epochSecs) return "just now";
  const seconds = Math.max(0, Math.floor(Date.now() / 1000) - epochSecs);
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
  return `${Math.floor(seconds / 86400)}d ago`;
}
function formatSession() {
  const minutes = Math.floor((Date.now() - state.startedAt) / 60000);
  return minutes < 1 ? "Now" : minutes < 60 ? `${minutes}m` : `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
}
function radioFrequency(value) { return Number.isFinite(value) ? (value / 1e6).toFixed(3) : "—"; }
function radioBandwidth(value) { return Number.isFinite(value) ? (value / 1000).toFixed(value % 1000 ? 1 : 0) : "—"; }
function signalQuality(rssi) { return rssi >= -80 ? "Excellent" : rssi >= -95 ? "Good" : rssi >= -110 ? "Fair" : "Weak"; }

function element(tag, className, content) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (content != null) node.textContent = content;
  return node;
}

function toast(message, kind = "ok") {
  const item = element("div", `toast ${kind}`);
  item.append(element("i"), element("span", "", message));
  $("toasts").append(item);
  setTimeout(() => item.classList.add("out"), 3200);
  setTimeout(() => item.remove(), 3500);
}

function setLink(status) {
  const online = status === "connected";
  state.connected = online;
  const label = online ? "Connected" : status === "reconnecting" ? "Reconnecting" : "Offline";
  text("link-label", label);
  text("side-status", label);
  text("hero-link", label);
  $("connect-button").className = `link-chip ${status}`;
  $("side-dot").className = `status-dot ${status}`;
}

function targetInfo(key) {
  if (key.startsWith("ch:")) {
    const channel = state.channels.find((item) => channelKey(item) === key);
    return channel ? { key, name: `#${channel.name || `Channel ${channel.channelIdx}`}`, subtitle: "Mesh channel", avatar: "#", channel } : null;
  }
  const contact = state.contacts.find((item) => contactKey(item) === key);
  return contact ? { key, name: contact.advName || "Unnamed contact", subtitle: `Direct · ${hex(contact.publicKey, 4)}`, avatar: initials(contact.advName), contact } : null;
}

function targetForContactPrefix(prefix) {
  const contact = state.contacts.find((item) => prefix.every((byte, index) => item.publicKey[index] === byte));
  return contact ? contactKey(contact) : `dm-prefix:${hex(prefix)}`;
}

function normalizeTargets() {
  if (state.selected && targetInfo(state.selected)) return;
  state.selected = state.channels[0] ? channelKey(state.channels[0]) : state.contacts[0] ? contactKey(state.contacts[0]) : "";
}

function addMessage(message) {
  state.messages.push({ id: `${Date.now()}-${Math.random()}`, ...message });
  if (state.messages.length > 160) state.messages.splice(0, state.messages.length - 160);
  renderMessages();
  renderHome();
}

function ingestWaiting(waiting) {
  for (const item of waiting) {
    if (item.contactMessage) {
      const message = item.contactMessage;
      const key = targetForContactPrefix(message.pubKeyPrefix);
      addMessage({ key, text: message.text, timestamp: message.senderTimestamp, direction: "in", status: "received", unread: state.view !== "messages" || state.selected !== key, snr: message.snr });
    } else if (item.channelMessage) {
      const message = item.channelMessage;
      const key = `ch:${message.channelIdx}`;
      addMessage({ key, text: message.text, timestamp: message.senderTimestamp, direction: "in", status: "received", unread: state.view !== "messages" || state.selected !== key, snr: message.snr });
    }
  }
  if (waiting.length) toast(`${waiting.length} new message${waiting.length === 1 ? "" : "s"}`, "warn");
}

function refreshNearbyFromContacts() {
  state.nearby = [...state.contacts].filter((item) => item.lastAdvert).sort((a, b) => b.lastAdvert - a.lastAdvert).slice(0, 24);
}

async function refreshContacts() {
  const contacts = await connection.getContacts();
  state.contacts = contacts.sort((a, b) => (b.lastAdvert || 0) - (a.lastAdvert || 0));
  refreshNearbyFromContacts();
  normalizeTargets();
  renderAll();
}

async function syncMessages() {
  const waiting = await connection.getWaitingMessages();
  ingestWaiting(waiting);
}

async function refreshStats() {
  if (!state.connected) return;
  try {
    const battery = await connection.getBatteryVoltage();
    state.battery = battery.batteryMilliVolts;
  } catch { /* optional */ }
  try {
    const radio = await connection.getStatsRadio();
    state.radio = radio.data;
    if (Number.isFinite(state.radio.lastRssi)) state.rfSamples.push(state.radio.lastRssi);
    state.rfSamples = state.rfSamples.slice(-36);
  } catch { /* optional */ }
  try { state.packets = (await connection.getStatsPackets()).data; } catch { /* optional */ }
  renderAll();
}

async function syncAll() {
  if (syncing) return;
  syncing = true;
  $("refresh-button").disabled = true;
  try {
    try { state.device = await connection.deviceQuery(Constants.SupportedCompanionProtocolVersion); } catch { /* older firmware */ }
    state.self = await connection.getSelfInfo(6000);
    await refreshContacts();
    try { state.channels = (await connection.getChannels()).filter((item) => item.name); } catch { state.channels = []; }
    normalizeTargets();
    await syncMessages();
    await refreshStats();
    renderAll();
  } finally {
    syncing = false;
    $("refresh-button").disabled = false;
  }
}

function renderHome() {
  const self = state.self;
  text("node-name", self?.name || "RCC6");
  text("node-id", self?.publicKey ? `ID ${hex(self.publicKey, 8)}…` : "Waiting for companion identity…");
  text("hero-contacts", state.contacts.length || "—");
  text("hero-uptime", formatSession());
  const unread = state.messages.filter((item) => item.unread);
  text("home-unread", unread.length);
  text("nav-unread", unread.length);
  $("nav-unread").hidden = !unread.length;
  const latest = unread.at(-1);
  const preview = $("message-preview");
  preview.replaceChildren();
  if (latest) {
    const target = targetInfo(latest.key);
    preview.className = "preview";
    preview.append(element("strong", "", target?.name || "New message"), element("span", "", latest.text));
  } else {
    preview.className = "preview empty";
    preview.textContent = "No unread messages";
  }
  const nearby = state.nearby[0];
  const nearbyPreview = $("nearby-preview");
  nearbyPreview.replaceChildren();
  if (nearby) {
    nearbyPreview.className = "preview";
    nearbyPreview.append(element("strong", "", nearby.advName || "Unnamed node"), element("span", "", `${formatAge(nearby.lastAdvert)} · ${hex(nearby.publicKey, 4)}`));
  } else {
    nearbyPreview.className = "preview empty";
    nearbyPreview.textContent = "Waiting for adverts";
  }
  const rssi = state.radio?.lastRssi;
  text("home-rssi", Number.isFinite(rssi) ? `${rssi} dBm` : "— dBm");
  text("home-snr", Number.isFinite(state.radio?.lastSnr) ? `SNR ${state.radio.lastSnr.toFixed(1)}` : "SNR —");
  text("home-frequency", self?.radioFreq ? `${radioFrequency(self.radioFreq)} MHz` : "— MHz");
  text("home-packets", state.packets ? `${formatNumber(state.packets.recv)} packets` : "— packets");
  $("radio-meter-fill").style.width = Number.isFinite(rssi) ? `${Math.max(4, Math.min(100, (rssi + 125) * 2))}%` : "0";
}

function renderMessages() {
  normalizeTargets();
  const targets = [
    ...state.channels.map((channel) => ({ key: channelKey(channel), name: `#${channel.name}`, subtitle: "Channel", avatar: "#" })),
    ...state.contacts.map((contact) => ({ key: contactKey(contact), name: contact.advName || "Unnamed contact", subtitle: "Direct", avatar: initials(contact.advName) })),
  ];
  text("thread-count", targets.length);
  const threadList = $("thread-list");
  threadList.replaceChildren();
  if (!targets.length) threadList.append(element("p", "empty-state", "Contacts and channels appear after sync."));
  for (const target of targets) {
    const messages = state.messages.filter((item) => item.key === target.key);
    const latest = messages.at(-1);
    const unread = messages.filter((item) => item.unread).length;
    const button = element("button", `thread${state.selected === target.key ? " active" : ""}`);
    button.type = "button";
    const avatar = element("span", "avatar", target.avatar);
    const copy = element("span", "thread-copy");
    copy.append(element("strong", "", target.name), element("small", "", latest?.text || target.subtitle));
    const meta = unread ? element("span", "thread-badge", unread) : element("time", "thread-time", latest ? formatClock(latest.timestamp) : "");
    button.append(avatar, copy, meta);
    button.addEventListener("click", () => selectTarget(target.key));
    threadList.append(button);
  }

  const select = $("target-select");
  select.replaceChildren(new Option("Choose recipient", ""));
  if (state.channels.length) {
    const group = document.createElement("optgroup"); group.label = "Channels";
    for (const channel of state.channels) group.append(new Option(`#${channel.name}`, channelKey(channel)));
    select.append(group);
  }
  if (state.contacts.length) {
    const group = document.createElement("optgroup"); group.label = "Direct contacts";
    for (const contact of state.contacts) group.append(new Option(contact.advName || "Unnamed contact", contactKey(contact)));
    select.append(group);
  }
  select.value = state.selected;
  const info = targetInfo(state.selected);
  text("conversation-title", info?.name || "Choose a conversation");
  text("conversation-subtitle", info?.subtitle || "Channel or direct contact");
  text("conversation-avatar", info?.avatar || "#");
  const list = $("message-list");
  list.replaceChildren();
  const messages = state.messages.filter((item) => item.key === state.selected);
  if (!messages.length) list.append(element("div", "welcome-bubble", info ? `No messages with ${info.name} yet.` : "Select a channel or contact to start talking across the mesh."));
  for (const message of messages) {
    const bubble = element("article", `bubble ${message.direction === "out" ? "out" : "in"} ${message.status === "failed" ? "failed" : ""}`);
    const status = message.direction === "out" ? ` · ${message.status}` : Number.isFinite(message.snr) ? ` · SNR ${message.snr.toFixed(1)}` : "";
    bubble.append(element("p", "", message.text), element("small", "", `${formatClock(message.timestamp)}${status}`));
    list.append(bubble);
  }
  requestAnimationFrame(() => { list.scrollTop = list.scrollHeight; });
  $("message-input").disabled = !info || !state.connected;
  $("composer").querySelector("button").disabled = !info || !state.connected;
}

function selectTarget(key) {
  state.selected = key;
  for (const message of state.messages) if (message.key === key) message.unread = false;
  renderMessages();
  renderHome();
}

function renderNearby() {
  const list = $("nearby-list");
  list.replaceChildren();
  if (!state.nearby.length) {
    list.append(element("article", "card empty-state", "Nearby nodes appear as adverts arrive."));
    return;
  }
  for (const contact of state.nearby) {
    const card = element("article", "card nearby-card");
    const avatar = element("span", "avatar", initials(contact.advName));
    const body = element("div");
    body.append(element("p", "eyebrow", contact.type === 2 ? "REPEATER" : contact.type === 3 ? "ROOM" : "CONTACT"), element("h3", "", contact.advName || "Unnamed node"), element("p", "", `ID ${hex(contact.publicKey, 5)} · ${contact.outPathLen > 0 ? `${contact.outPathLen} hop path` : "flood path"}`));
    const time = element("time", "", formatAge(contact.lastAdvert));
    const action = element("button", "contact-action", "Message →");
    action.type = "button";
    action.addEventListener("click", () => { state.selected = contactKey(contact); showView("messages"); });
    card.append(avatar, body, time, action);
    list.append(card);
  }
}

function renderRadio() {
  const self = state.self;
  const radio = state.radio;
  const packets = state.packets;
  text("radio-rssi", Number.isFinite(radio?.lastRssi) ? radio.lastRssi : "—");
  text("radio-quality", Number.isFinite(radio?.lastRssi) ? signalQuality(radio.lastRssi) : "Waiting for sample");
  text("radio-snr", Number.isFinite(radio?.lastSnr) ? `SNR ${radio.lastSnr.toFixed(1)} dB` : "SNR —");
  text("radio-noise", Number.isFinite(radio?.noiseFloor) ? `Noise ${radio.noiseFloor} dBm` : "Noise —");
  text("radio-frequency", radioFrequency(self?.radioFreq));
  text("radio-bandwidth", radioBandwidth(self?.radioBw));
  text("radio-spreading", self?.radioSf ? `SF${self.radioSf}` : "—");
  text("radio-coding", self?.radioCr ? `CR 4/${self.radioCr}` : "CR —");
  text("radio-power", Number.isFinite(self?.txPower) ? self.txPower : "—");
  text("packet-received", formatNumber(packets?.recv));
  text("packet-sent", formatNumber(packets?.sent));
  text("packet-direct", formatNumber(packets?.nRecvDirect));
  text("packet-flood", formatNumber(packets?.nRecvFlood));
  const errors = packets?.nRecvErrors ?? 0;
  text("packet-errors", `${formatNumber(errors)} error${errors === 1 ? "" : "s"}`);
  $("packet-errors").className = errors ? "error" : "quiet";
  if (state.view === "radio") requestAnimationFrame(drawSignalChart);
}

function drawSignalChart() {
  const canvas = $("signal-chart");
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (width < 20 || height < 20) return;
  const ratio = Math.min(devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
  const context = canvas.getContext("2d"); context.scale(ratio, ratio);
  context.clearRect(0, 0, width, height);
  context.strokeStyle = "rgba(141,152,167,.12)"; context.lineWidth = 1;
  for (let row = 1; row < 4; row += 1) { context.beginPath(); context.moveTo(0, row * height / 4); context.lineTo(width, row * height / 4); context.stroke(); }
  const samples = state.rfSamples.length > 1 ? state.rfSamples : [-120, -120];
  context.beginPath();
  samples.forEach((sample, index) => {
    const x = index * width / Math.max(1, samples.length - 1);
    const y = height - Math.max(0, Math.min(1, (sample + 130) / 60)) * height;
    index ? context.lineTo(x, y) : context.moveTo(x, y);
  });
  context.strokeStyle = "#44f08a"; context.lineWidth = 2; context.shadowColor = "#44f08a"; context.shadowBlur = 8; context.stroke();
}

function renderMore() {
  const self = state.self;
  const device = state.device;
  text("more-name", self?.name || "RCC6");
  text("more-model", device?.manufacturerModel || "MeshCore Companion");
  text("more-firmware", device?.firmwareVer != null ? `Protocol ${device.firmwareVer}` : "—");
  text("more-build", device?.firmware_build_date || "—");
  text("more-key", self?.publicKey ? `${hex(self.publicKey, 8)}…` : "—");
  text("app-version", __APP_VERSION__);
  text("library-version", __MESHCORE_JS_VERSION__);
  $("advert-button").disabled = !state.connected;
}

function renderAll() {
  if (state.battery) {
    const volts = `${(state.battery / 1000).toFixed(2)} V`;
    text("battery-chip", volts);
  }
  renderHome(); renderMessages(); renderNearby(); renderRadio(); renderMore();
}

function showView(view) {
  state.view = view;
  document.querySelectorAll(".screen").forEach((screen) => screen.classList.toggle("active", screen.dataset.screen === view));
  document.querySelectorAll(".nav button").forEach((button) => button.classList.toggle("active", button.dataset.view === view));
  text("screen-title", view[0].toUpperCase() + view.slice(1));
  text("screen-kicker", view === "home" ? "COMPANION" : view === "messages" ? "MESH CHAT" : view === "nearby" ? "DISCOVERY" : view === "radio" ? "RF HEALTH" : "DEVICE");
  if (view === "messages" && state.selected) selectTarget(state.selected);
  if (view === "radio") renderRadio();
  window.scrollTo({ top: 0, behavior: "smooth" });
}

async function sendMessage(event) {
  event.preventDefault();
  const input = $("message-input");
  const messageText = input.value.trim();
  const info = targetInfo(state.selected);
  if (!messageText || !info) return;
  if (encoder.encode(messageText).length > MAX_MESSAGE_BYTES) return toast("Message is over 140 bytes", "error");
  const outgoing = { key: state.selected, text: messageText, timestamp: Math.floor(Date.now() / 1000), direction: "out", status: "sending", unread: false };
  addMessage(outgoing);
  input.value = ""; updateComposeCount();
  try {
    await exclusive(() => info.channel ? connection.sendChannelTextMessage(info.channel.channelIdx, messageText) : connection.sendTextMessage(info.contact.publicKey, messageText));
    const stored = state.messages.find((item) => item.key === outgoing.key && item.text === outgoing.text && item.status === "sending");
    if (stored) stored.status = "queued";
    toast("Message queued");
  } catch {
    const stored = state.messages.find((item) => item.key === outgoing.key && item.text === outgoing.text && item.status === "sending");
    if (stored) stored.status = "failed";
    toast("Message failed", "error");
  }
  renderMessages();
}

function updateComposeCount() {
  const count = encoder.encode($("message-input").value).length;
  text("compose-count", `${count} / ${MAX_MESSAGE_BYTES} bytes`);
  $("compose-count").className = count > MAX_MESSAGE_BYTES ? "error" : "";
}

connection.on("connected", () => {
  setLink("connected");
  toast("RCC6 connected");
  setTimeout(() => $("boot").classList.add("hidden"), 220);
  exclusive(syncAll).catch(() => toast("Initial sync failed", "error"));
});
connection.on("disconnected", () => setLink("offline"));
connection.on("transport-state", () => { setLink("reconnecting"); toast("Link interrupted — reconnecting", "warn"); });
connection.on("transport-restored", () => { toast("Link restored — resyncing"); setTimeout(() => location.reload(), 450); });
connection.on(Constants.PushCodes.MsgWaiting, () => exclusive(syncMessages).catch(() => toast("Message sync failed", "error")));
connection.on(Constants.PushCodes.NewAdvert, (advert) => {
  const existing = state.contacts.findIndex((item) => hex(item.publicKey) === hex(advert.publicKey));
  if (existing >= 0) state.contacts[existing] = advert; else state.contacts.unshift(advert);
  refreshNearbyFromContacts(); renderAll(); toast(`${advert.advName || "A node"} is nearby`, "warn");
});
connection.on(Constants.PushCodes.Advert, () => exclusive(refreshContacts).catch(() => {}));
connection.on(Constants.PushCodes.SendConfirmed, () => {
  const pending = [...state.messages].reverse().find((item) => item.direction === "out" && item.key.startsWith("dm:") && item.status === "queued");
  if (pending) { pending.status = "delivered"; renderMessages(); toast("Direct message delivered"); }
});
connection.on(Constants.PushCodes.LogRxData, (sample) => {
  state.radio = { ...(state.radio || {}), lastRssi: sample.lastRssi, lastSnr: sample.lastSnr };
  state.rfSamples.push(sample.lastRssi); state.rfSamples = state.rfSamples.slice(-36); renderHome(); renderRadio();
});

document.querySelectorAll(".nav button").forEach((button) => button.addEventListener("click", () => showView(button.dataset.view)));
document.querySelectorAll("[data-open]").forEach((button) => button.addEventListener("click", () => showView(button.dataset.open)));
$("target-select").addEventListener("change", (event) => selectTarget(event.target.value));
$("composer").addEventListener("submit", sendMessage);
$("message-input").addEventListener("input", updateComposeCount);
$("nearby-refresh").addEventListener("click", () => exclusive(refreshContacts).then(() => toast("Nearby refreshed")).catch(() => toast("Refresh failed", "error")));
$("refresh-button").addEventListener("click", () => exclusive(syncAll).then(() => toast("Sync complete")).catch(() => toast("Sync failed", "error")));
$("advert-button").addEventListener("click", async () => {
  $("advert-button").disabled = true;
  try { await exclusive(() => connection.sendFloodAdvert()); toast("Advert queued", "warn"); }
  catch { toast("Advert failed", "error"); }
  finally { $("advert-button").disabled = false; }
});
$("connect-button").addEventListener("click", () => state.connected ? exclusive(syncAll).catch(() => toast("Sync failed", "error")) : location.reload());
window.addEventListener("resize", () => state.view === "radio" && drawSignalChart());
document.addEventListener("visibilitychange", () => { if (!document.hidden && state.connected) exclusive(refreshStats).catch(() => {}); });

renderAll();
setInterval(() => { text("hero-uptime", formatSession()); if (!document.hidden && state.connected) exclusive(refreshStats).catch(() => {}); }, 30000);
connection.connect().catch(() => { setLink("offline"); $("boot").classList.add("hidden"); toast("Could not open companion link", "error"); });
