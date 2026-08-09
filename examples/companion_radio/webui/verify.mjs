import { gunzipSync } from "node:zlib";
import { readFile } from "node:fs/promises";

const header = await readFile(new URL("./Rcc6WebUiAssets.h", import.meta.url), "utf8");
const appSource = await readFile(new URL("./src/app.js", import.meta.url), "utf8");
const body = header.match(/RCC6_WEB_UI_INDEX_GZ\[\] PROGMEM = \{([\s\S]*?)\n\};/);
if (!body) throw new Error("Generated gzip array is missing");
const bytes = Uint8Array.from([...body[1].matchAll(/0x([0-9a-f]{2})/g)], (match) => Number.parseInt(match[1], 16));
const html = gunzipSync(bytes).toString("utf8");

for (const required of [
  "Home", "Messages", "Nearby", "Radio", "More", "/api/frame", "/api/network",
  "X-RCC6-Session", "application/x-www-form-urlencoded", "Set up local Wi-Fi",
  "meshcore", "localStorage", "rcc6-radio-owner", "rcc6-history-pending-", "syncNextMessage", "MeshCore.js",
]) {
  if (!html.includes(required)) throw new Error(`Built UI is missing ${required}`);
}
if (!header.includes("extern const size_t RCC6_WEB_UI_INDEX_GZ_LEN;")) throw new Error("Length declaration is missing");
if (/<script\s+[^>]*src=|<link\s+[^>]*rel=[\"']stylesheet/i.test(html)) throw new Error("External runtime asset detected");
if (html.includes("location.reload")) throw new Error("Reload-based transport recovery detected");
if (html.includes("navigator.locks")) throw new Error("Secure-context Web Locks dependency detected");
if (appSource.includes(".getWaitingMessages(")) throw new Error("Destructive batch message drain detected");
for (const required of ["this.drainTask = this.drainRetainedFrames()", "if (!this.ready) throw"]) {
  if (!appSource.includes(required)) throw new Error(`Takeover drain guard is missing ${required}`);
}
console.log(`Verified ${bytes.length}-byte RCC6 WebUI gzip asset`);
