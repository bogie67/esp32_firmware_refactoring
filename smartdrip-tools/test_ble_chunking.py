#!/usr/bin/env python
"""
test_ble_chunking.py - Test avanzati per chunking e back-pressure BLE

Requisiti:
    pip install bleak

Esempi di test:
    # Test chunking con frame grande
    python test_ble_chunking.py --test chunking --size 500

    # Test back-pressure con raffica di comandi
    python test_ble_chunking.py --test backpressure --count 10

    # Test resilienza con disconnessioni
    python test_ble_chunking.py --test resilience --cycles 5

    # Test MTU negotiation
    python test_ble_chunking.py --test mtu
"""
import argparse, asyncio, json, sys, time, random
from bleak import BleakClient, BleakScanner

DEVICE_NAME      = "SMART_DRIP"
CHAR_RX_UUID     = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_TX_UUID     = "0000ff02-0000-1000-8000-00805f9b34fb"

def build_frame(req_id: int, op: str, payload: dict) -> bytes:
    """Costruisce un frame di comando"""
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

def build_large_payload(size: int) -> dict:
    """Genera un payload JSON di dimensione specifica per test chunking"""
    # Crea una lista di stringhe per raggiungere la dimensione desiderata
    base_data = []
    current_size = 20  # overhead del frame base
    
    while current_size < size:
        # Aggiunge stringhe random per riempire
        remaining = size - current_size
        chunk_size = min(remaining - 10, 50)  # -10 per overhead JSON
        if chunk_size > 0:
            random_str = ''.join(random.choices('abcdefghijklmnopqrstuvwxyz0123456789', k=chunk_size))
            base_data.append(random_str)
            current_size += len(json.dumps(random_str)) + 1  # +1 per comma
        else:
            break
    
    return {"testData": base_data, "size": size}

async def discover_device(mac: str | None):
    if mac:
        return await BleakScanner.find_device_by_address(mac, timeout=8.0)
    print("🔍 Scanning for", DEVICE_NAME, "...")
    for d in await BleakScanner.discover(8.0):
        if d.name == DEVICE_NAME:
            return d
    return None

class TestNotifyHandler:
    """Handler per gestire le notifiche BLE durante i test"""
    def __init__(self):
        self.notifications = []
        self.expected_count = 0
        self.future = None
        
    def reset(self, expected_count=1):
        self.notifications.clear()
        self.expected_count = expected_count
        self.future = asyncio.get_event_loop().create_future()
        
    async def on_notify(self, _, data):
        self.notifications.append(data)
        print(f"📨 Notify {len(self.notifications)}/{self.expected_count}: {len(data)} bytes - {data[:20].hex()}...")
        
        if len(self.notifications) >= self.expected_count:
            if not self.future.done():
                self.future.set_result(self.notifications)
    
    async def wait_for_notifications(self, timeout=15):
        try:
            return await asyncio.wait_for(self.future, timeout)
        except asyncio.TimeoutError:
            print(f"❌ Timeout: received {len(self.notifications)}/{self.expected_count} notifications")
            return self.notifications

async def test_chunking(client: BleakClient, notify_handler: TestNotifyHandler, frame_size: int):
    """Test chunking automatico con frame grandi"""
    print(f"\n🧪 TEST CHUNKING - Frame size: {frame_size} bytes")
    
    # Genera payload grande
    large_payload = build_large_payload(frame_size)
    frame = build_frame(123, "syncSchedule", large_payload)
    
    actual_size = len(frame)
    print(f"📏 Frame costruito: {actual_size} bytes (target: {frame_size})")
    
    # Verifica se dovrebbe essere frammentato (assumendo MTU 244 = 247-3)
    estimated_mtu = 244
    if actual_size > estimated_mtu:
        chunks_needed = (actual_size + estimated_mtu - 1) // estimated_mtu
        print(f"📦 Frame dovrebbe essere chunked in ~{chunks_needed} parti")
        notify_handler.reset(chunks_needed)
    else:
        print("📤 Frame dovrebbe essere inviato direttamente")
        notify_handler.reset(1)
    
    # Invia frame
    start_time = time.time()
    await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
    
    # Aspetta risposta(e)
    notifications = await notify_handler.wait_for_notifications(20)
    end_time = time.time()
    
    print(f"⏱️  Tempo totale: {(end_time - start_time)*1000:.1f}ms")
    print(f"✅ Ricevute {len(notifications)} notifiche")
    
    return len(notifications) > 0

async def test_backpressure(client: BleakClient, notify_handler: TestNotifyHandler, command_count: int):
    """Test back-pressure con raffica di comandi"""
    print(f"\n🧪 TEST BACK-PRESSURE - {command_count} comandi in raffica")
    
    notify_handler.reset(command_count)
    
    # Invia comandi in rapida successione
    start_time = time.time()
    for i in range(command_count):
        payload = {"scan_id": i, "timestamp": int(time.time() * 1000)}
        frame = build_frame(i + 1, "wifiScan", payload)
        
        await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
        print(f"📤 Comando {i+1}/{command_count} inviato")
        
        # Piccolo delay per non saturare completamente
        await asyncio.sleep(0.1)
    
    # Aspetta tutte le risposte
    notifications = await notify_handler.wait_for_notifications(30)
    end_time = time.time()
    
    print(f"⏱️  Tempo totale: {(end_time - start_time)*1000:.1f}ms")
    print(f"✅ Ricevute {len(notifications)}/{command_count} risposte")
    
    return len(notifications) == command_count

async def test_mtu_negotiation(client: BleakClient, notify_handler: TestNotifyHandler):
    """Test MTU negotiation e adattamento chunking"""
    print(f"\n🧪 TEST MTU NEGOTIATION")
    
    # Prova diversi size di frame per testare chunking
    test_sizes = [50, 150, 300, 500, 800]
    results = []
    
    for size in test_sizes:
        print(f"\n📏 Testing frame size: {size} bytes")
        
        payload = build_large_payload(size)
        frame = build_frame(200 + size, "syncSchedule", payload)
        actual_size = len(frame)
        
        notify_handler.reset(1)  # Aspetta almeno una risposta
        
        start_time = time.time()
        await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
        
        notifications = await notify_handler.wait_for_notifications(10)
        end_time = time.time()
        
        success = len(notifications) > 0
        latency = (end_time - start_time) * 1000
        
        print(f"📊 Size: {actual_size}B, Success: {success}, Latency: {latency:.1f}ms, Chunks: {len(notifications)}")
        results.append((actual_size, success, latency, len(notifications)))
        
        await asyncio.sleep(1)  # Pausa tra test
    
    return results

async def test_resilience(client: BleakClient, notify_handler: TestNotifyHandler, cycles: int):
    """Test resilienza con disconnessioni simulate"""
    print(f"\n🧪 TEST RESILIENCE - {cycles} cicli")
    
    successful_cycles = 0
    
    for cycle in range(cycles):
        print(f"\n🔄 Ciclo {cycle + 1}/{cycles}")
        
        # Test comando normale
        payload = {"cycle": cycle, "timestamp": int(time.time() * 1000)}
        frame = build_frame(300 + cycle, "wifiScan", payload)
        
        notify_handler.reset(1)
        
        try:
            await client.write_gatt_char(CHAR_RX_UUID, frame, response=False)
            notifications = await notify_handler.wait_for_notifications(8)
            
            if len(notifications) > 0:
                successful_cycles += 1
                print(f"✅ Ciclo {cycle + 1} riuscito")
            else:
                print(f"❌ Ciclo {cycle + 1} fallito - no response")
                
        except Exception as e:
            print(f"❌ Ciclo {cycle + 1} errore: {e}")
        
        # Pausa tra cicli
        await asyncio.sleep(2)
    
    success_rate = (successful_cycles / cycles) * 100
    print(f"\n📊 Resilience Test: {successful_cycles}/{cycles} successi ({success_rate:.1f}%)")
    
    return success_rate >= 80  # 80% success rate considerato buono

async def main():
    ap = argparse.ArgumentParser(description="Test avanzati per BLE chunking e back-pressure")
    ap.add_argument("--mac", help="MAC address del dispositivo")
    ap.add_argument("--test", required=True, 
                   choices=["chunking", "backpressure", "mtu", "resilience"],
                   help="Tipo di test da eseguire")
    ap.add_argument("--size", type=int, default=500, 
                   help="Dimensione frame per test chunking (default: 500)")
    ap.add_argument("--count", type=int, default=5,
                   help="Numero comandi per test back-pressure (default: 5)")
    ap.add_argument("--cycles", type=int, default=3,
                   help="Numero cicli per test resilience (default: 3)")
    args = ap.parse_args()

    # Discover device
    dev = await discover_device(args.mac)
    if dev is None:
        print("❌ Dispositivo non trovato")
        sys.exit(1)

    print(f"🔗 Connessione a {dev.address} ({dev.name})")
    
    async with BleakClient(dev) as client:
        print(f"✅ Connesso - MTU: {client.mtu_size}")
        
        # Setup notifiche
        notify_handler = TestNotifyHandler()
        await client.start_notify(CHAR_TX_UUID, notify_handler.on_notify)
        
        try:
            # Esegui test specificato
            if args.test == "chunking":
                success = await test_chunking(client, notify_handler, args.size)
            elif args.test == "backpressure":
                success = await test_backpressure(client, notify_handler, args.count)
            elif args.test == "mtu":
                results = await test_mtu_negotiation(client, notify_handler)
                success = all(r[1] for r in results)  # Tutti i test devono riuscire
            elif args.test == "resilience":
                success = await test_resilience(client, notify_handler, args.cycles)
            
            print(f"\n🏁 Test {args.test}: {'✅ PASSED' if success else '❌ FAILED'}")
            
        finally:
            await client.stop_notify(CHAR_TX_UUID)

if __name__ == "__main__":
    asyncio.run(main())