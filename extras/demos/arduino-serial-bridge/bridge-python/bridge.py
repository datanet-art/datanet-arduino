"""
bridge.py — Arduino → DataNet serial bridge (Python)

Reads newline-delimited JSON from a serial port (e.g. an Arduino or Teensy)
and publishes each reading to DataNet.

Requires:
    pip install datanet-sdk pyserial python-dotenv

Usage:
    DATANET_API_KEY=ak_... SERIAL_PORT=/dev/tty.usbserial-0001 python bridge.py

Or copy .env.example → .env and fill in your values.

Arduino serial format (one JSON object per line at 115200 baud):
    {"sensor":"temperature","value":22.4,"unit":"C","n":1}
    {"sensor":"humidity","value":63.2,"unit":"%","n":1}
    {"status":"ready","sketch":"SerialSensor","baud":115200}

Each sensor reading is published to:
    <DATANET_CHANNEL_PREFIX>.<sensor>
e.g. project.abc.sensor.temperature
"""

import asyncio
import json
import os
import signal
import sys
from pathlib import Path

import serial
from dotenv import load_dotenv

# ── Load .env if present ───────────────────────────────────────────────────
load_dotenv(Path(__file__).parent / ".env")

# ── Config ─────────────────────────────────────────────────────────────────
API_KEY        = os.environ.get("DATANET_API_KEY")
SERIAL_PORT    = os.environ.get("SERIAL_PORT", "/dev/tty.usbserial-0001")
BAUD_RATE      = int(os.environ.get("BAUD_RATE", "115200"))
CHANNEL_PREFIX = os.environ.get("DATANET_CHANNEL_PREFIX", "project.demo.sensor")

if not API_KEY:
    print("Error: DATANET_API_KEY is required", file=sys.stderr)
    sys.exit(1)

# ── Bridge ─────────────────────────────────────────────────────────────────
async def main():
    from datanet import DataNet

    dn = DataNet(api_key=API_KEY, device_id="arduino-serial-bridge")

    @dn.on("connect")
    async def on_connect():
        print("[datanet] connected")

    @dn.on("disconnect")
    async def on_disconnect():
        print("[datanet] disconnected")

    @dn.on("error")
    async def on_error(exc):
        print(f"[datanet] error: {exc}", file=sys.stderr)

    async with dn:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        except serial.SerialException as e:
            print(f"[serial] error: {e}", file=sys.stderr)
            sys.exit(1)

        print(f"[serial] opened {SERIAL_PORT} @ {BAUD_RATE}")

        loop = asyncio.get_running_loop()
        stop = loop.create_future()

        def _handle_sigint():
            print("\n[bridge] shutting down...")
            if not stop.done():
                stop.set_result(None)

        loop.add_signal_handler(signal.SIGINT, _handle_sigint)

        async def read_serial():
            while not stop.done():
                line = await loop.run_in_executor(None, ser.readline)
                if not line:
                    continue

                try:
                    text = line.decode("utf-8").strip()
                except UnicodeDecodeError:
                    continue

                if not text:
                    continue

                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    print(f"[serial] non-JSON line: {text}")
                    continue

                # Startup / status messages — log but don't publish
                if "status" in msg:
                    print(f"[serial] status: {msg}")
                    continue

                if "sensor" not in msg or "value" not in msg:
                    print(f"[serial] unexpected shape: {msg}")
                    continue

                channel = f"{CHANNEL_PREFIX}.{msg['sensor']}"
                payload = {
                    "value": msg["value"],
                    "unit":  msg.get("unit"),
                    "n":     msg.get("n"),
                }

                try:
                    await dn.publish(channel, payload)
                    print(f"[publish] {channel} {payload}")
                except Exception as e:
                    print(f"[publish] error: {e}", file=sys.stderr)

            ser.close()

        await asyncio.gather(read_serial(), stop)


if __name__ == "__main__":
    asyncio.run(main())
