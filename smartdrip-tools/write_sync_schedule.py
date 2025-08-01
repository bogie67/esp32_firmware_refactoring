"""
Send a test `syncSchedule` command (id=1) to SMART_DRIP BLE device.
Requires: bleak  (pip install bleak)

Usage:
    python write_sync_schedule.py           # auto-discovers first SMART_DRIP
    python write_sync_schedule.py <MAC>     # connect to specific address
"""
import asyncio, json, sys
from bleak import BleakClient, BleakScanner

DEVICE_NAME   = "SMART_DRIP"
CHAR_RX_UUID  = "0000ff01-0000-1000-8000-00805f9b34fb"   # RX characteristic (simple 16-bit UUID)

def build_frame(request_id: int, op: str, payload: dict) -> bytes:
    op_bytes = op.encode()
    if len(op_bytes) > 15:
        raise ValueError("op string too long (max 15)")
    payload_bytes = json.dumps(payload).encode()

    frame = bytearray()
    frame += request_id.to_bytes(2, "little")   # id
    frame.append(len(op_bytes))                 # opLen
    frame += op_bytes                           # op
    frame += payload_bytes                      # JSON payload
    return bytes(frame)

async def main():
    target_addr = sys.argv[1] if len(sys.argv) == 2 else None

    # ── 1. Trova dispositivo ──────────────────────────────────────────────
    if target_addr:
        dev = await BleakScanner.find_device_by_address(target_addr, timeout=5.0)
    else:
        print("Scanning for device named", DEVICE_NAME, "…")
        devices = await BleakScanner.discover(5.0)
        print("Found devices:")
        for d in devices:
            print(f"  {d.address} - {d.name}")
        dev = next((d for d in devices if d.name == DEVICE_NAME), None)

    if dev is None:
        print("Device not found."); return

    # ── 2. Connessione e invio ────────────────────────────────────────────
    async with BleakClient(dev) as client:
        if not client.is_connected:
            print("Connection failed"); return
        print("Connected to", dev.address, "- Name:", dev.name)
        
        # Lista tutti i servizi e caratteristiche
        services = client.services
        print("Available services:")
        for service in services:
            print(f"  Service {service.uuid}")
            for char in service.characteristics:
                print(f"    Char {char.uuid} - Properties: {char.properties}")

        # Verifica che la caratteristica esista
        char = None
        for service in services:
            for c in service.characteristics:
                if str(c.uuid).lower() == CHAR_RX_UUID.lower():
                    char = c
                    print(f"✅ Found RX characteristic: {c.uuid}")
                    break
            if char:
                break
        
        if not char:
            print(f"❌ RX characteristic {CHAR_RX_UUID} not found!")
            return
        
        frame = build_frame(
            request_id=1,
            op="syncSchedule",
            payload={
                "zones": [
                    {"id": 1, "dur": 300},
                    {"id": 2, "dur": 180}
                ],
                "name": "Spring"
            }
        )
        
        print(f"📝 Writing {len(frame)} bytes to characteristic {char.uuid}")
        await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
        print("✅ Frame sent successfully.")

        # Facoltativo: ascolta una notifica di risposta (caratteristica TX 0xFF02)
        async def on_notify(_, data): 
            print("📨 Notification received:", data.hex())
        await client.start_notify("0000ff02-0000-1000-8000-00805f9b34fb", on_notify)
        await asyncio.sleep(3)
        await client.stop_notify("0000ff02-0000-1000-8000-00805f9b34fb")

if __name__ == "__main__":
    asyncio.run(main())
