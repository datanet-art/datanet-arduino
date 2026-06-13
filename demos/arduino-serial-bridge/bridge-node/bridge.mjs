/**
 * bridge.mjs — Arduino → DataNet serial bridge (Node.js)
 *
 * Reads newline-delimited JSON from a serial port (e.g. an Arduino or Teensy)
 * and publishes each reading to DataNet.
 *
 * Requires:
 *   npm install @datanet/core serialport dotenv
 *
 * Usage:
 *   DATANET_API_KEY=ak_... SERIAL_PORT=/dev/tty.usbserial-0001 node bridge.mjs
 *
 * Or copy .env.example → .env and fill in your values.
 *
 * Arduino serial format (one JSON object per line at 115200 baud):
 *   {"sensor":"temperature","value":22.4,"unit":"C","n":1}
 *   {"sensor":"humidity","value":63.2,"unit":"%","n":1}
 *   {"status":"ready","sketch":"SerialSensor","baud":115200}
 *
 * Each sensor reading is published to:
 *   <DATANET_CHANNEL_PREFIX>.<sensor>
 * e.g. project.abc.sensor.temperature
 */

import { createRequire } from "module";
import { readFileSync, existsSync } from "fs";
import path from "path";
import { fileURLToPath } from "url";

const require = createRequire(import.meta.url);

// ── Load .env if present ───────────────────────────────────────────────────
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const envPath = path.join(__dirname, ".env");
if (existsSync(envPath)) {
  const lines = readFileSync(envPath, "utf8").split("\n");
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) continue;
    const eq = trimmed.indexOf("=");
    if (eq === -1) continue;
    const key = trimmed.slice(0, eq).trim();
    const val = trimmed.slice(eq + 1).trim();
    if (!process.env[key]) process.env[key] = val;
  }
}

// ── Config ─────────────────────────────────────────────────────────────────
const API_KEY        = process.env.DATANET_API_KEY;
const SERIAL_PORT    = process.env.SERIAL_PORT    || "/dev/tty.usbserial-0001";
const BAUD_RATE      = parseInt(process.env.BAUD_RATE || "115200", 10);
const CHANNEL_PREFIX = process.env.DATANET_CHANNEL_PREFIX || "project.demo.sensor";

if (!API_KEY) {
  console.error("Error: DATANET_API_KEY is required");
  process.exit(1);
}

// ── DataNet ────────────────────────────────────────────────────────────────
const { DataNet } = await import("@datanet/core");

const dn = new DataNet({ apiKey: API_KEY, deviceId: "arduino-serial-bridge" });

dn.on("connect",    ()      => console.log("[datanet] connected"));
dn.on("disconnect", ()      => console.log("[datanet] disconnected"));
dn.on("error",      (err)   => console.error("[datanet] error:", err.message));

await dn.connect();

// ── Serial ─────────────────────────────────────────────────────────────────
const { SerialPort } = require("serialport");
const { ReadlineParser } = require("@serialport/parser-readline");

const port = new SerialPort({ path: SERIAL_PORT, baudRate: BAUD_RATE });
const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));

port.on("open", () => console.log(`[serial] opened ${SERIAL_PORT} @ ${BAUD_RATE}`));
port.on("error", (err) => { console.error("[serial] error:", err.message); process.exit(1); });

parser.on("data", async (line) => {
  line = line.trim();
  if (!line) return;

  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    console.warn("[serial] non-JSON line:", line);
    return;
  }

  // Startup / status messages — log but don't publish
  if (msg.status) {
    console.log("[serial] status:", JSON.stringify(msg));
    return;
  }

  if (!msg.sensor || msg.value === undefined) {
    console.warn("[serial] unexpected shape:", JSON.stringify(msg));
    return;
  }

  const channel = `${CHANNEL_PREFIX}.${msg.sensor}`;
  const payload = { value: msg.value, unit: msg.unit ?? null, n: msg.n ?? null };

  try {
    await dn.publish(channel, payload);
    console.log(`[publish] ${channel}`, payload);
  } catch (err) {
    console.error("[publish] error:", err.message);
  }
});

// ── Shutdown ───────────────────────────────────────────────────────────────
process.on("SIGINT", async () => {
  console.log("\n[bridge] shutting down...");
  await dn.disconnect();
  port.close();
  process.exit(0);
});
