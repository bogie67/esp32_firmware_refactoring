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

    # Test chunking con frame grande (500 bytes)
    python send_cmd.py --op syncSchedule --large-payload 500

    # Test back-pressure con 10 comandi in sequenza
    python send_cmd.py --op wifiScan --repeat 10

    # Test chunking bidirezionale con frame grande
    python send_cmd.py --op syncSchedule --large-payload 800

    # Test chunking forzato (anche per frame piccoli)
    python send_cmd.py --op wifiScan --force-chunking

    # Connessione a MAC specifico
    python send_cmd.py --mac AA:BB:CC:DD:EE:FF --op wifiScan
"""
import argparse, asyncio, json, sys, pathlib, time, random, struct
from bleak import BleakClient, BleakScanner

DEVICE_NAME      = "SMART_DRIP"
CHAR_RX_UUID     = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_TX_UUID     = "0000ff02-0000-1000-8000-00805f9b34fb"

# Chunk flags (must match ESP32)
CHUNK_FLAG_CHUNKED = 0x01
CHUNK_FLAG_FINAL   = 0x02
CHUNK_FLAG_MORE    = 0x04

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

def build_large_payload(target_size: int) -> dict:
    """Genera un payload JSON di dimensione specifica per test chunking"""
    base_data = []
    current_size = 50  # overhead stimato
    
    while current_size < target_size:
        remaining = target_size - current_size
        chunk_size = min(remaining - 10, 40)
        if chunk_size > 0:
            random_str = ''.join(random.choices('abcdefghijklmnopqrstuvwxyz0123456789', k=chunk_size))
            base_data.append(random_str)
            current_size += len(json.dumps(random_str)) + 2
        else:
            break
    
    return {"testData": base_data, "payloadSize": target_size}

def create_chunk_header(flags: int, chunk_idx: int, total_chunks: int, frame_id: int, chunk_size: int) -> bytes:
    """Creates a chunk header matching ESP32 format"""
    # struct chunk_header_t { uint8_t flags; uint8_t chunk_idx; uint8_t total_chunks; uint16_t frame_id; uint16_t chunk_size; }
    return struct.pack('<BBBHH', flags, chunk_idx, total_chunks, frame_id, chunk_size)

def chunk_frame(frame_data: bytes, max_chunk_size: int, frame_id: int) -> list[bytes]:
    """Split a frame into chunks for BLE transmission"""
    chunk_header_size = 7  # sizeof(chunk_header_t)
    effective_chunk_size = max_chunk_size - chunk_header_size
    
    if len(frame_data) <= max_chunk_size:
        # No chunking needed
        return [frame_data]
    
    chunks = []
    total_chunks = (len(frame_data) + effective_chunk_size - 1) // effective_chunk_size
    
    if total_chunks > 8:
        raise ValueError(f"Frame too large: needs {total_chunks} chunks (max 8)")
    
    offset = 0
    for i in range(total_chunks):
        remaining = len(frame_data) - offset
        chunk_payload_size = min(remaining, effective_chunk_size)
        
        # Determine flags
        flags = CHUNK_FLAG_CHUNKED
        if i == total_chunks - 1:
            flags |= CHUNK_FLAG_FINAL
        else:
            flags |= CHUNK_FLAG_MORE
        
        # Create chunk
        header = create_chunk_header(flags, i, total_chunks, frame_id, chunk_payload_size)
        payload = frame_data[offset:offset + chunk_payload_size]
        chunk = header + payload
        
        chunks.append(chunk)
        offset += chunk_payload_size
    
    return chunks

async def discover_device(mac: str | None):
    if mac:
        return await BleakScanner.find_device_by_address(mac, timeout=6.0)
    print("Scanning for", DEVICE_NAME, "…")
    for d in await BleakScanner.discover(6.0):
        if d.name == DEVICE_NAME:
            return d
    return None

def get_test_counter():
    """Get a persistent test counter to track pattern"""
    counter_file = "/tmp/ble_test_counter.txt"
    try:
        with open(counter_file, "r") as f:
            counter = int(f.read().strip())
    except (FileNotFoundError, ValueError):
        counter = 0
    
    counter += 1
    with open(counter_file, "w") as f:
        f.write(str(counter))
    
    return counter

async def main():
    test_num = get_test_counter()
    print(f"🧪 TEST #{test_num} - Pattern tracking")
    
    ap = argparse.ArgumentParser()
    ap.add_argument("--mac",       help="MAC address del dispositivo")
    ap.add_argument("--op",        required=True, choices=["syncSchedule","wifiScan","wifiConfigure"])
    ap.add_argument("--id",        type=int, default=1, help="request-id (default 1)")
    ap.add_argument("--ssid",      help="SSID per wifiConfigure")
    ap.add_argument("--pass",      dest="password", help="Password per wifiConfigure")
    ap.add_argument("--json",      help="File JSON per syncSchedule")
    ap.add_argument("--no-notify", action="store_true", help="non ascoltare la notifica di risposta")
    # Test parameters
    ap.add_argument("--large-payload", type=int, metavar="SIZE", 
                   help="Genera payload grande di SIZE bytes per test chunking")
    ap.add_argument("--repeat", type=int, default=1, metavar="N",
                   help="Ripete il comando N volte per test back-pressure")
    ap.add_argument("--force-chunking", action="store_true", 
                   help="Forza chunking anche per frame piccoli (test)")
    args = ap.parse_args()

    # ---- payload ----------------------------------------------------------------------------------------------------
    if args.large_payload:
        # Override payload per test chunking
        payload = build_large_payload(args.large_payload)
        if args.op != "syncSchedule":
            print(f"⚠️  Large payload test: switching to syncSchedule operation")
            args.op = "syncSchedule"
    elif args.op == "wifiScan":
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
    
    if args.large_payload:
        print(f"📏 Frame generato per test chunking: {len(frame)} bytes (target: {args.large_payload})")

    # ---- connessione ------------------------------------------------------------------------------------------------
    dev = await discover_device(args.mac)
    if dev is None:
        print("Dispositivo non trovato"); sys.exit(1)

    async with BleakClient(dev) as client:
        connect_time = time.time()
        print(f"🔗 Connesso a {dev.address} – MTU: {client.mtu_size}, invio op: {args.op}")
        print(f"🔗 Connection established at {connect_time:.1f}")
        
        notifications_received = []
        
        if not args.no_notify:
            # 1. abilita notify PRIMA di scrivere
            fut = asyncio.get_event_loop().create_future()

            async def on_notify(_, data):
                notifications_received.append(data)
                timestamp = time.time() - total_start_time
                print(f"📨 Notify {len(notifications_received)}: {len(data)} bytes at +{timestamp:.1f}s - {data[:20].hex()}...")
                
                # Decode potential chunk header for debugging
                if len(data) >= 7:  # sizeof(chunk_header_t)
                    flags, chunk_idx, total_chunks, frame_id, chunk_size = struct.unpack('<BBBHH', data[:7])
                    if flags & CHUNK_FLAG_CHUNKED:
                        print(f"   📦 Chunk header: idx={chunk_idx}, total={total_chunks}, frame_id={frame_id}, size={chunk_size}")
                
                # Per test con repeat, aspetta multiple notifiche
                if len(notifications_received) >= args.repeat:
                    if not fut.done():
                        fut.set_result(notifications_received)

            await client.start_notify(CHAR_TX_UUID, on_notify)   # ① subscribe PRIMA

        # 2. invia il comando (ripetuto se richiesto)
        total_start_time = time.time()
        mtu = client.mtu_size
        max_chunk_size = mtu - 3  # ATT header overhead
        
        for i in range(args.repeat):
            if args.repeat > 1:
                # Per test back-pressure, modifica request ID
                current_frame = build_frame(args.id + i, args.op, payload)
                print(f"📤 Invio comando {i+1}/{args.repeat}")
            else:
                current_frame = frame
                print(f"📤 Frame to send: {len(current_frame)} bytes")
            
            # Decide se chuncare
            should_chunk = args.force_chunking or len(current_frame) > max_chunk_size
            
            if should_chunk:
                # Chunk the frame
                frame_id = 1000 + i  # Unique frame ID
                chunks = chunk_frame(current_frame, max_chunk_size, frame_id)
                print(f"📦 Chunking frame into {len(chunks)} parts (MTU: {mtu})")
                
                # Send all chunks
                for j, chunk in enumerate(chunks):
                    print(f"📤 Sending chunk {j+1}/{len(chunks)}: {len(chunk)} bytes")
                    await client.write_gatt_char(CHAR_RX_UUID, chunk, response=False)
                    
                    # Small delay between chunks
                    if j < len(chunks) - 1:
                        await asyncio.sleep(0.05)
                        
            else:
                # Send directly
                send_timestamp = time.time()
                print(f"📤 Sending direct frame: {len(current_frame)} bytes at +{send_timestamp - total_start_time:.1f}s")
                await client.write_gatt_char(CHAR_RX_UUID, current_frame, response=False)
                print(f"📤 Frame sent successfully at +{time.time() - total_start_time:.1f}s")
            
            if args.repeat > 1 and i < args.repeat - 1:
                await asyncio.sleep(0.2)  # Delay tra comandi per test back-pressure

        # 3. aspetta notifica(e) o timeout
        if not args.no_notify:
            timeout = 30  # Timeout più lungo per WiFi scan e operazioni lunghe
            try:
                print(f"⏳ Waiting for {args.repeat} notification(s) with {timeout}s timeout...")
                await asyncio.wait_for(fut, timeout)
                total_time = (time.time() - total_start_time) * 1000
                print(f"✅ Ricevute {len(notifications_received)} notifiche in {total_time:.1f}ms")
                
                if args.repeat > 1:
                    avg_time = total_time / len(notifications_received) if notifications_received else 0
                    print(f"📊 Tempo medio per comando: {avg_time:.1f}ms")
                
                # Summary of received data
                total_bytes = sum(len(notif) for notif in notifications_received)
                print(f"📊 Total data received: {total_bytes} bytes")
                    
            except asyncio.TimeoutError:
                timeout_time = time.time() - total_start_time
                print(f"❌ Timeout after {timeout_time:.1f}s: {len(notifications_received)}/{args.repeat} notifiche ricevute")
                if notifications_received:
                    total_bytes = sum(len(notif) for notif in notifications_received)
                    print(f"📊 Partial data received: {total_bytes} bytes")

            await client.stop_notify(CHAR_TX_UUID)

if __name__ == "__main__":
    asyncio.run(main())
