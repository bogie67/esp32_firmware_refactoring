#!/usr/bin/env python
"""
send_cmd.py  –  invia un comando BLE al tuo ESP32 SMART_DRIP

Requisiti:
    pip install bleak

Esempi:
    # Scansione reti Wi-Fi
    python send_cmd.py --op wifiScan

    # Provision reti Wi-Fi (con password)
    python send_cmd.py --op wifiConfigure --ssid MyNet --pass secret

    # Sync schedule (JSON da file)
    python send_cmd.py --op syncSchedule --json schedule.json

    # Connessione a MAC specifico
    python send_cmd.py --mac AA:BB:CC:DD:EE:FF --op wifiScan
"""
import argparse, asyncio, json, sys, pathlib
from bleak import BleakClient, BleakScanner

DEVICE_NAME      = "SMART_DRIP"
CHAR_RX_UUID     = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_TX_UUID     = "0000ff02-0000-1000-8000-00805f9b34fb"

def build_frame(req_id: int, op: str, payload: dict) -> bytes:
    op_b = op.encode()
    if len(op_b) > 15:
        raise ValueError("op string too long (max 15 bytes)")
    data_b = json.dumps(payload, separators=(",", ":")).encode()
    frame  = bytearray()
    frame += req_id.to_bytes(2, "little")
    frame.append(len(op_b))
    frame += op_b
    frame += data_b
    return bytes(frame)

async def discover_device(mac: str | None):
    if mac:
        return await BleakScanner.find_device_by_address(mac, timeout=6.0)
    print("Scanning for", DEVICE_NAME, "…")
    for d in await BleakScanner.discover(6.0):
        if d.name == DEVICE_NAME:
            return d
    return None

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mac",       help="MAC address del dispositivo")
    ap.add_argument("--op",        required=True, choices=["syncSchedule","wifiScan","wifiConfigure"])
    ap.add_argument("--id",        type=int, default=1, help="request-id (default 1)")
    ap.add_argument("--ssid",      help="SSID per wifiConfigure")
    ap.add_argument("--pass",      dest="password", help="Password per wifiConfigure")
    ap.add_argument("--json",      help="File JSON per syncSchedule")
    ap.add_argument("--no-notify", action="store_true", help="non ascoltare la notifica di risposta")
    args = ap.parse_args()

    # ---- payload ----------------------------------------------------------------------------------------------------
    if args.op == "wifiScan":
        payload = {}
    elif args.op == "wifiConfigure":
        if not args.ssid:
            ap.error("--ssid obbligatorio con wifiConfigure")
        payload = {"ssid": args.ssid}
        if args.password:
            payload["pass"] = args.password
    else:  # syncSchedule
        if not args.json:
            ap.error("--json obbligatorio con syncSchedule")
        path = pathlib.Path(args.json)
        payload = json.loads(path.read_text(encoding="utf-8"))

    frame = build_frame(args.id, args.op, payload)

    # ---- connessione ------------------------------------------------------------------------------------------------
    dev = await discover_device(args.mac)
    if dev is None:
        print("Dispositivo non trovato"); sys.exit(1)

    async with BleakClient(dev) as client:
        print("Connesso a", dev.address, "– invio op:", args.op)
        
        if not args.no_notify:
            # 1. abilita notify PRIMA di scrivere
            fut = asyncio.get_event_loop().create_future()

            async def on_notify(_, data):
                print("Notify:", data.hex())
                if not fut.done():
                    fut.set_result(None)

            await client.start_notify(CHAR_TX_UUID, on_notify)   # ① subscribe PRIMA

        # 2. invia il comando
        await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
        print("Frame sent; waiting notification …")

        # 3. aspetta notifica o timeout 15 s
        if not args.no_notify:
            try:
                await asyncio.wait_for(fut, 15)                      # ② aspetta fino a 15 s
                print("✅ Notifica ricevuta!")
            except asyncio.TimeoutError:
                print("❌ No notify within 15 s")

            await client.stop_notify(CHAR_TX_UUID)

if __name__ == "__main__":
    asyncio.run(main())
