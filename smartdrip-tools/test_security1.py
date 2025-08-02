#!/usr/bin/env python3
"""
Security1 Handshake Test Suite

Test script per verificare l'handshake Security1 su BLE e MQTT
dell'ESP32 firmware con protocomm e crittografia X25519 + AES-CTR.

Dipendenze:
pip install bleak paho-mqtt cryptography pyserial

Utilizzo:
python test_security1.py --transport ble --device "ESP32_Security1"
python test_security1.py --transport mqtt --broker "192.168.1.100"
python test_security1.py --transport both --device "ESP32_Security1" --broker "192.168.1.100"
"""

import asyncio
import argparse
import json
import time
import logging
import struct
import uuid
from typing import Optional, Dict, Any
from dataclasses import dataclass
from enum import Enum

# BLE Dependencies
try:
    from bleak import BleakClient, BleakScanner
    BLE_AVAILABLE = True
except ImportError:
    print("⚠️ BLE support not available. Install with: pip install bleak")
    BLE_AVAILABLE = False

# MQTT Dependencies  
try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    print("⚠️ MQTT support not available. Install with: pip install paho-mqtt")
    MQTT_AVAILABLE = False

# WebSocket support for MQTT
try:
    import websocket
    WEBSOCKET_AVAILABLE = True
except ImportError:
    print("⚠️ WebSocket support not available. Install with: pip install websocket-client")
    WEBSOCKET_AVAILABLE = False

# Crypto Dependencies
try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives import hmac
    import secrets
    CRYPTO_AVAILABLE = True
except ImportError:
    print("⚠️ Crypto support not available. Install with: pip install cryptography")
    CRYPTO_AVAILABLE = False

def long_to_bytes(val):
    """Helper function from ESP-IDF"""
    return val.to_bytes(1, 'big')

def a_xor_b(a: bytes, b: bytes) -> bytes:
    """XOR function from ESP-IDF security1.py"""
    return b''.join(long_to_bytes(a[i] ^ b[i]) for i in range(0, len(b)))

# ==================== CONSTANTS ====================

# BLE UUIDs (corrispondono al firmware ESP32)
BLE_HANDSHAKE_SERVICE_UUID = "0000ff50-0000-1000-8000-00805f9b34fb"
BLE_HANDSHAKE_TX_CHAR_UUID = "0000ff51-0000-1000-8000-00805f9b34fb"  # Client -> ESP32
BLE_HANDSHAKE_RX_CHAR_UUID = "0000ff52-0000-1000-8000-00805f9b34fb"  # ESP32 -> Client

BLE_OPERATIONAL_SERVICE_UUID = "0000ff00-0000-1000-8000-00805f9b34fb"
BLE_OPERATIONAL_TX_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
BLE_OPERATIONAL_RX_CHAR_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"

# Security1 Protocol Constants
SECURITY1_VERSION = 1
SECURITY1_SESSION_ESTABLISH = 0x01
SECURITY1_SESSION_VERIFY = 0x02
SECURITY1_SESSION_DATA = 0x03

class TestResult(Enum):
    PENDING = "⏳"
    SUCCESS = "✅"
    FAILED = "❌"
    SKIPPED = "⏭️"

@dataclass
class Security1Session:
    """Gestisce stato sessione Security1"""
    client_private_key: Optional[object] = None  # X25519PrivateKey when available
    server_public_key: Optional[bytes] = None
    shared_secret: Optional[bytes] = None
    session_key: Optional[bytes] = None
    session_active: bool = False
    proof_of_possession: str = "test_pop_12345"  # Default, overridden by constructor

class Security1Tester:
    """Test suite completa per Security1 handshake"""
    
    def __init__(self, verbose: bool = True, pop: str = "test_pop_12345"):
        self.verbose = verbose
        self.session = Security1Session()
        self.session.proof_of_possession = pop  # Override default PoP
        self.results: Dict[str, TestResult] = {}
        
        # Setup logging
        level = logging.DEBUG if verbose else logging.INFO
        logging.basicConfig(
            level=level,
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
        )
        self.logger = logging.getLogger(__name__)
        
    def log(self, message: str, level: str = "info"):
        """Logging con emoji"""
        if level == "debug" and self.verbose:
            self.logger.debug(f"🔍 {message}")
        elif level == "info":
            self.logger.info(f"ℹ️ {message}")
        elif level == "success":
            self.logger.info(f"✅ {message}")
        elif level == "warning":
            self.logger.warning(f"⚠️ {message}")
        elif level == "error":
            self.logger.error(f"❌ {message}")

    # ==================== CRYPTO OPERATIONS ====================
    
    def generate_client_keypair(self) -> bytes:
        """Genera coppia chiavi con approccio deterministico compatibile con ESP32"""
        # Genera chiavi X25519 reali (ma useremo approccio deterministico per shared secret)
        self.session.client_private_key = X25519PrivateKey.generate()
        client_public_key = self.session.client_private_key.public_key()
        
        public_key_bytes = client_public_key.public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw
        )
        
        self.log(f"Generated client keypair with deterministic compatibility (public: {public_key_bytes.hex()[:16]}...)")
        return public_key_bytes
    
    def derive_shared_secret(self, server_public_key: bytes) -> bytes:
        """Deriva shared secret usando VERO X25519 ECDH come nel sequence diagram"""
        if not self.session.client_private_key:
            raise ValueError("Client private key not generated")
            
        self.log(f"Computing REAL X25519 shared secret following Security1 protocol")
        self.log(f"🔍 Server public key received: {server_public_key.hex()}")
        
        # Deserializza chiave pubblica server
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PublicKey
        server_pub_key = X25519PublicKey.from_public_bytes(server_public_key)
        
        # Deriva shared secret con VERA X25519 ECDH
        shared_secret = self.session.client_private_key.exchange(server_pub_key)
        self.session.shared_secret = shared_secret
        
        self.log(f"🔍 REAL X25519 shared secret computed: {shared_secret.hex()}")
        return shared_secret
    
    def derive_session_key_authentic(self, pop: str) -> bytes:
        """Deriva session key con protocollo Security1 autentico: curve25519_result XOR SHA256(PoP)"""
        if not self.session.shared_secret:
            raise ValueError("Shared secret not available")
            
        self.log(f"Deriving session key with ESP-IDF Security1 protocol", "info")
        
        # Step 1: Start with the shared secret (ESP-IDF pattern)
        sharedK = self.session.shared_secret
        self.log(f"Shared Key: {sharedK.hex()}", "debug")
        
        # Step 2: If PoP is provided, XOR SHA256 of PoP with the shared key (ESP-IDF pattern)
        if len(pop) > 0:
            # Calculate SHA256 of PoP
            pop_hash = hashes.Hash(hashes.SHA256())
            pop_hash.update(pop.encode('utf-8'))
            digest = pop_hash.finalize()
            self.log(f"SHA256(PoP): {digest.hex()[:16]}...", "debug")
            
            # XOR with and update Shared Key (using ESP-IDF a_xor_b function)
            sharedK = a_xor_b(sharedK, digest)
            self.log(f"Updated Shared Key (Shared key XORed with PoP): {sharedK.hex()[:16]}...", "debug")
        
        self.session.session_key = sharedK
        self.session.session_active = True
        
        self.log(f"ESP-IDF session key derived: {sharedK.hex()[:16]}...", "success")
        return sharedK
    
    def generate_verification_token_aes_ctr(self, device_public_key: bytes, device_random: bytes, session_key: bytes) -> bytes:
        """Genera verification token AUTENTICO con AES-CTR come da protocollo ESP-IDF Security1"""
        self.log(f"Generating AUTHENTIC AES-CTR verification token", "info")
        
        # Protocollo ESP-IDF Security1: client_verify = AES_CTR_encrypt(device_public_key, session_key, device_random_IV)
        # Reference: /Users/bogie/esp/esp-idf/tools/esp_prov/security/security1.py lines 114-116
        
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        
        # Initialize AES-CTR cipher using session_key as key and device_random as IV
        cipher = Cipher(
            algorithms.AES(session_key), 
            modes.CTR(device_random)
        )
        encryptor = cipher.encryptor()
        
        # Encrypt device_public_key to create verification token
        verification_token = encryptor.update(device_public_key) + encryptor.finalize()
        
        self.log(f"AES-CTR verification inputs:", "debug")
        self.log(f"  Device public key: {device_public_key.hex()}", "debug")
        self.log(f"  Session key: {session_key.hex()}", "debug") 
        self.log(f"  Device random (IV): {device_random.hex()}", "debug")
        self.log(f"  Verification token: {verification_token.hex()}", "debug")
        
        return verification_token
    
    def encrypt_data(self, plaintext: bytes) -> bytes:
        """Critta dati con AES-CTR usando session key"""
        if not self.session.session_key:
            raise ValueError("Session key not available")
            
        # Genera IV random
        iv = secrets.token_bytes(16)
        
        # AES-CTR encryption
        cipher = Cipher(
            algorithms.AES(self.session.session_key),
            modes.CTR(iv)
        )
        encryptor = cipher.encryptor()
        ciphertext = encryptor.update(plaintext) + encryptor.finalize()
        
        # HMAC per integrità
        h = hmac.HMAC(self.session.session_key, hashes.SHA256())
        h.update(iv + ciphertext)
        mac = h.finalize()
        
        # Formato: IV (16) + MAC (32) + Ciphertext
        encrypted_data = iv + mac + ciphertext
        
        self.log(f"Encrypted {len(plaintext)} bytes → {len(encrypted_data)} bytes")
        return encrypted_data
    
    def decrypt_data(self, encrypted_data: bytes) -> bytes:
        """Decritta dati ricevuti"""
        if not self.session.session_key:
            raise ValueError("Session key not available")
            
        if len(encrypted_data) < 48:  # IV + MAC minimum
            raise ValueError("Encrypted data too short")
            
        # Estrai componenti
        iv = encrypted_data[:16]
        mac = encrypted_data[16:48]
        ciphertext = encrypted_data[48:]
        
        # Verifica HMAC
        h = hmac.HMAC(self.session.session_key, hashes.SHA256())
        h.update(iv + ciphertext)
        h.verify(mac)
        
        # AES-CTR decryption
        cipher = Cipher(
            algorithms.AES(self.session.session_key),
            modes.CTR(iv)
        )
        decryptor = cipher.decryptor()
        plaintext = decryptor.update(ciphertext) + decryptor.finalize()
        
        self.log(f"Decrypted {len(encrypted_data)} bytes → {len(plaintext)} bytes")
        return plaintext

    # ==================== URL PARSING HELPER ====================
    
    def parse_mqtt_websocket_url(self, broker_url: str) -> tuple[str, int, str]:
        """Parse WebSocket MQTT URL and return (host, port, path)"""
        if broker_url.startswith("ws://"):
            # Parse ws://broker.emqx.io:8083/mqtt
            url_without_protocol = broker_url.replace("ws://", "")  # broker.emqx.io:8083/mqtt
            if ":" in url_without_protocol:
                broker_host = url_without_protocol.split(":")[0]  # broker.emqx.io
                port_and_path = url_without_protocol.split(":", 1)[1]  # 8083/mqtt
                if "/" in port_and_path:
                    broker_port = int(port_and_path.split("/")[0])  # 8083
                    ws_path = "/" + "/".join(port_and_path.split("/")[1:])  # /mqtt
                else:
                    broker_port = int(port_and_path)
                    ws_path = "/mqtt"
            else:
                broker_host = url_without_protocol.split("/")[0]
                broker_port = 8083
                ws_path = "/" + "/".join(url_without_protocol.split("/")[1:])
        elif broker_url.startswith("wss://"):
            # Parse wss://secure.broker.com:8084/mqtt  
            url_without_protocol = broker_url.replace("wss://", "")
            if ":" in url_without_protocol:
                broker_host = url_without_protocol.split(":")[0]
                port_and_path = url_without_protocol.split(":", 1)[1]
                if "/" in port_and_path:
                    broker_port = int(port_and_path.split("/")[0])
                    ws_path = "/" + "/".join(port_and_path.split("/")[1:])
                else:
                    broker_port = int(port_and_path)
                    ws_path = "/mqtt"
            else:
                broker_host = url_without_protocol.split("/")[0]
                broker_port = 8084
                ws_path = "/" + "/".join(url_without_protocol.split("/")[1:])
        else:
            # Fallback to standard MQTT
            broker_host = broker_url
            broker_port = 1883
            ws_path = None
            
        return broker_host, broker_port, ws_path

    # ==================== BLE TESTING ====================
    
    async def test_ble_discovery(self, device_name: str) -> Optional[str]:
        """Scopre dispositivi BLE con Security1"""
        if not BLE_AVAILABLE:
            self.results["ble_discovery"] = TestResult.SKIPPED
            return None
            
        self.log(f"🔍 Scanning for BLE device: {device_name}")
        
        try:
            devices = await BleakScanner.discover(timeout=10.0)
            
            for device in devices:
                if device.name and device_name.lower() in device.name.lower():
                    self.log(f"Found device: {device.name} ({device.address})", "success")
                    self.results["ble_discovery"] = TestResult.SUCCESS
                    return device.address
                    
            self.log(f"Device '{device_name}' not found", "error")
            self.results["ble_discovery"] = TestResult.FAILED
            return None
            
        except Exception as e:
            self.log(f"BLE discovery failed: {e}", "error")
            self.results["ble_discovery"] = TestResult.FAILED
            return None
    
    async def test_ble_handshake(self, device_address: str) -> bool:
        """Test completo handshake Security1 su BLE"""
        if not BLE_AVAILABLE or not CRYPTO_AVAILABLE:
            self.results["ble_handshake"] = TestResult.SKIPPED
            return False
            
        self.log(f"🤝 Starting BLE Security1 handshake with {device_address}")
        
        try:
            async with BleakClient(device_address) as client:
                self.log("Connected to BLE device")
                
                # Verifica servizi disponibili
                services = await client.get_services()
                handshake_service = None
                
                for service in services:
                    if service.uuid.lower() == BLE_HANDSHAKE_SERVICE_UUID.lower():
                        handshake_service = service
                        self.log(f"Found handshake service: {service.uuid}")
                        break
                
                if not handshake_service:
                    self.log("Handshake service not found", "error")
                    self.results["ble_handshake"] = TestResult.FAILED
                    return False
                
                # Setup notifiche su RX characteristic
                rx_responses = []
                
                def notification_handler(characteristic, data):
                    self.log(f"Received notification: {data.hex()}")
                    rx_responses.append(data)
                
                await client.start_notify(BLE_HANDSHAKE_RX_CHAR_UUID, notification_handler)
                
                # Step 1: Invia SESSION_ESTABLISH con chiave pubblica client
                client_public_key = self.generate_client_keypair()
                
                establish_msg = struct.pack(
                    "<BBB32s", 
                    SECURITY1_VERSION, 
                    SECURITY1_SESSION_ESTABLISH, 
                    len(client_public_key),
                    client_public_key.ljust(32, b'\x00')
                )
                
                self.log(f"Sending SESSION_ESTABLISH ({len(establish_msg)} bytes)")
                await client.write_gatt_char(BLE_HANDSHAKE_TX_CHAR_UUID, establish_msg)
                
                # Attendi risposta server
                await asyncio.sleep(2.0)
                
                if not rx_responses:
                    self.log("No response from server", "error")
                    self.results["ble_handshake"] = TestResult.FAILED
                    return False
                
                # Parse risposta server
                server_response = rx_responses[0]
                # Parse AUTHENTIC Security1 response: version + type + key_len + device_public_key(32) + device_random(16)
                if len(server_response) < 51:  # Expected: 3 + 32 + 16 = 51 bytes
                    self.log(f"Invalid server response length: {len(server_response)}, expected 51", "error")
                    self.results["ble_handshake"] = TestResult.FAILED
                    return False
                
                version, msg_type, key_len = struct.unpack("<BBB", server_response[:3])
                server_public_key = server_response[3:3+key_len]  # bytes 3-35
                device_random = server_response[3+key_len:3+key_len+16]  # bytes 35-51
                
                self.log(f"Server response: version={version}, type={msg_type}, key_len={key_len}")
                self.log(f"Device public key: {server_public_key.hex()}")
                self.log(f"Device random (AES-CTR IV): {device_random.hex()}")
                
                # Step 2: Deriva shared secret e session key con protocollo AUTENTICO
                self.derive_shared_secret(server_public_key)
                self.derive_session_key_authentic(self.session.proof_of_possession)
                
                # Step 3: Genera verification token AUTENTICO usando AES-CTR con device_random come IV
                verification_token = self.generate_verification_token_aes_ctr(
                    server_public_key, 
                    device_random,
                    self.session.session_key
                )
                
                self.log(f"Generated AUTHENTIC AES-CTR verification token: {verification_token.hex()}")
                pop_encrypted = verification_token
                
                verify_msg = struct.pack(
                    f">BBH{len(pop_encrypted)}s",
                    SECURITY1_VERSION,
                    SECURITY1_SESSION_VERIFY,
                    len(pop_encrypted),
                    pop_encrypted
                )
                
                self.log(f"Sending SESSION_VERIFY ({len(verify_msg)} bytes)")
                rx_responses.clear()
                await client.write_gatt_char(BLE_HANDSHAKE_TX_CHAR_UUID, verify_msg)
                
                # Attendi conferma
                await asyncio.sleep(2.0)
                
                if rx_responses and len(rx_responses[0]) >= 3:
                    _, msg_type, status = struct.unpack("<BBB", rx_responses[0][:3])
                    if msg_type == SECURITY1_SESSION_VERIFY and status == 0:
                        self.log("BLE Security1 handshake completed successfully!", "success")
                        self.results["ble_handshake"] = TestResult.SUCCESS
                        return True
                
                self.log("BLE handshake verification failed", "error")
                self.results["ble_handshake"] = TestResult.FAILED
                return False
                
        except Exception as e:
            self.log(f"BLE handshake failed: {e}", "error")
            self.results["ble_handshake"] = TestResult.FAILED
            return False
    
    async def test_ble_encrypted_communication(self, device_address: str) -> bool:
        """Test comunicazione crittografata su servizio operativo"""
        if not self.session.session_active:
            self.log("Session not active, skipping encrypted communication test", "warning")
            self.results["ble_encrypted_comm"] = TestResult.SKIPPED
            return False
            
        self.log("🔒 Testing BLE encrypted communication")
        
        try:
            async with BleakClient(device_address) as client:
                # Setup notifiche su servizio operativo
                operational_responses = []
                
                def operational_notification_handler(characteristic, data):
                    self.log(f"Operational notification: {data.hex()}")
                    operational_responses.append(data)
                
                await client.start_notify(BLE_OPERATIONAL_RX_CHAR_UUID, operational_notification_handler)
                
                # Invia comando crittografato
                test_command = {
                    "cmd": "get_device_info",
                    "id": 12345
                }
                
                command_json = json.dumps(test_command)
                encrypted_command = self.encrypt_data(command_json.encode())
                
                self.log(f"Sending encrypted command ({len(encrypted_command)} bytes)")
                await client.write_gatt_char(BLE_OPERATIONAL_TX_CHAR_UUID, encrypted_command)
                
                # Attendi risposta crittografata
                await asyncio.sleep(3.0)
                
                if operational_responses:
                    encrypted_response = operational_responses[0]
                    decrypted_response = self.decrypt_data(encrypted_response)
                    
                    try:
                        response_json = json.loads(decrypted_response.decode())
                        self.log(f"Decrypted response: {response_json}", "success")
                        self.results["ble_encrypted_comm"] = TestResult.SUCCESS
                        return True
                    except Exception as e:
                        self.log(f"Failed to parse decrypted response: {e}", "error")
                
                self.results["ble_encrypted_comm"] = TestResult.FAILED
                return False
                
        except Exception as e:
            self.log(f"BLE encrypted communication failed: {e}", "error")
            self.results["ble_encrypted_comm"] = TestResult.FAILED
            return False

    # ==================== MQTT TESTING ====================
    
    def test_mqtt_connection(self, broker_url: str = "ws://broker.emqx.io:8083/mqtt") -> bool:
        """Test connessione MQTT broker via WebSocket"""
        if not MQTT_AVAILABLE:
            self.results["mqtt_connection"] = TestResult.SKIPPED
            return False
            
        self.log(f"🌐 Testing MQTT WebSocket connection to {broker_url}")
        
        try:
            # Parse WebSocket URL
            broker_host, broker_port, ws_path = self.parse_mqtt_websocket_url(broker_url)
            
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets" if ws_path else "tcp")
            
            # WebSocket specific configuration
            if ws_path:
                client.ws_set_options(path=ws_path)
            
            client.connect(broker_host, broker_port, 60)
            client.loop_start()
            time.sleep(2.0)
            client.loop_stop()
            client.disconnect()
            
            self.log("MQTT WebSocket connection successful", "success")
            self.results["mqtt_connection"] = TestResult.SUCCESS
            return True
            
        except Exception as e:
            self.log(f"MQTT connection failed: {e}", "error")
            self.results["mqtt_connection"] = TestResult.FAILED
            return False
    
    def test_mqtt_handshake(self, broker_url: str = "ws://broker.emqx.io:8083/mqtt", topic_prefix: str = "security1/esp32") -> bool:
        """Test handshake Security1 su MQTT WebSocket"""
        if not MQTT_AVAILABLE or not CRYPTO_AVAILABLE:
            self.results["mqtt_handshake"] = TestResult.SKIPPED
            return False
            
        self.log(f"🤝 Starting MQTT Security1 handshake via {broker_url}")
        
        handshake_completed = False
        handshake_request_topic = f"{topic_prefix}/handshake/request"
        handshake_response_topic = f"{topic_prefix}/handshake/response"
        
        received_messages = []
        
        try:
            # Parse WebSocket URL
            broker_host, broker_port, ws_path = self.parse_mqtt_websocket_url(broker_url)
            
            def on_connect(client, userdata, flags, rc, properties=None):
                if rc == 0:
                    self.log(f"Connected to MQTT broker, subscribing to {handshake_response_topic}")
                    client.subscribe(handshake_response_topic)
                else:
                    self.log(f"Failed to connect, return code {rc}", "error")
            
            def on_message(client, userdata, msg, properties=None):
                self.log(f"Received MQTT message on {msg.topic}: {len(msg.payload)} bytes")
                received_messages.append(msg.payload)
            
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets" if ws_path else "tcp")
            
            # WebSocket configuration
            if ws_path:
                client.ws_set_options(path=ws_path)
            
            client.on_connect = on_connect
            client.on_message = on_message
            
            client.connect(broker_host, broker_port, 60)
            client.loop_start()
            
            # Attendi connessione
            time.sleep(2.0)
            
            # Step 1: Invia SESSION_ESTABLISH
            client_public_key = self.generate_client_keypair()
            
            establish_msg = struct.pack(
                "<BBB32s", 
                SECURITY1_VERSION, 
                SECURITY1_SESSION_ESTABLISH, 
                len(client_public_key),
                client_public_key.ljust(32, b'\x00')
            )
            
            self.log(f"Publishing SESSION_ESTABLISH to {handshake_request_topic}")
            client.publish(handshake_request_topic, establish_msg)
            
            # Attendi risposta
            time.sleep(3.0)
            
            if received_messages:
                server_response = received_messages[0]
                
                # Parse AUTHENTIC Security1 response: version + type + key_len + device_public_key(32) + device_random(16)
                if len(server_response) < 51:  # Expected: 3 + 32 + 16 = 51 bytes
                    self.log(f"Invalid server response length: {len(server_response)}, expected 51", "error")
                    return False
                    
                version, msg_type, key_len = struct.unpack("<BBB", server_response[:3])
                server_public_key = server_response[3:3+key_len]  # bytes 3-35
                device_random = server_response[3+key_len:3+key_len+16]  # bytes 35-51
                
                self.log(f"Server response: version={version}, type={msg_type}, key_len={key_len}")
                self.log(f"Device public key: {server_public_key.hex()}")
                self.log(f"Device random (AES-CTR IV): {device_random.hex()}")
                
                # Deriva shared secret con protocollo AUTENTICO
                self.derive_shared_secret(server_public_key)
                self.derive_session_key_authentic(self.session.proof_of_possession)
                
                # Step 2: Genera verification token AUTENTICO usando AES-CTR con device_random come IV
                verification_token = self.generate_verification_token_aes_ctr(
                    server_public_key, 
                    device_random,
                    self.session.session_key
                )
                
                self.log(f"Generated AUTHENTIC AES-CTR verification token: {verification_token.hex()}")
                pop_encrypted = verification_token
                
                verify_msg = struct.pack(
                    f">BBH{len(pop_encrypted)}s",
                    SECURITY1_VERSION,
                    SECURITY1_SESSION_VERIFY,
                    len(pop_encrypted),
                    pop_encrypted
                )
                
                self.log(f"Publishing SESSION_VERIFY")
                received_messages.clear()
                client.publish(handshake_request_topic, verify_msg)
                
                time.sleep(3.0)
                
                if received_messages and len(received_messages[0]) >= 3:
                    _, msg_type, status = struct.unpack("<BBB", received_messages[0][:3])
                    if msg_type == SECURITY1_SESSION_VERIFY and status == 0:
                        handshake_completed = True
            
            client.loop_stop()
            client.disconnect()
            
            if handshake_completed:
                self.log("MQTT Security1 handshake completed successfully!", "success")
                self.results["mqtt_handshake"] = TestResult.SUCCESS
                return True
            else:
                self.log("MQTT handshake failed", "error")
                self.results["mqtt_handshake"] = TestResult.FAILED
                return False
                
        except Exception as e:
            self.log(f"MQTT handshake failed: {e}", "error")
            self.results["mqtt_handshake"] = TestResult.FAILED
            return False
    
    def test_mqtt_encrypted_communication(self, broker_url: str = "ws://broker.emqx.io:8083/mqtt", topic_prefix: str = "security1/esp32") -> bool:
        """Test comunicazione crittografata MQTT WebSocket"""
        if not self.session.session_active:
            self.log("Session not active, skipping MQTT encrypted communication test", "warning")
            self.results["mqtt_encrypted_comm"] = TestResult.SKIPPED
            return False
            
        self.log("🔒 Testing MQTT encrypted communication")
        
        data_request_topic = f"{topic_prefix}/data/request"
        data_response_topic = f"{topic_prefix}/data/response"
        
        received_responses = []
        
        try:
            # Parse WebSocket URL
            broker_host, broker_port, ws_path = self.parse_mqtt_websocket_url(broker_url)
            
            def on_connect(client, userdata, flags, rc, properties=None):
                if rc == 0:
                    client.subscribe(data_response_topic)
                    self.log(f"Subscribed to {data_response_topic}")
            
            def on_message(client, userdata, msg, properties=None):
                self.log(f"Received encrypted response: {len(msg.payload)} bytes")
                received_responses.append(msg.payload)
            
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets" if ws_path else "tcp")
            
            # WebSocket configuration
            if ws_path:
                client.ws_set_options(path=ws_path)
            
            client.on_connect = on_connect
            client.on_message = on_message
            
            client.connect(broker_host, broker_port, 60)
            client.loop_start()
            
            time.sleep(5.0)  # Più tempo per ESP32 operational mode
            
            # Invia comando crittografato
            test_command = {
                "op": "get_device_status",
                "id": 67890
            }
            
            command_json = json.dumps(test_command)
            encrypted_command = self.encrypt_data(command_json.encode())
            
            self.log(f"Publishing encrypted command to {data_request_topic}")
            client.publish(data_request_topic, encrypted_command)
            
            # Attendi risposta (più tempo per debug)
            time.sleep(8.0)
            
            client.loop_stop()
            client.disconnect()
            
            if received_responses:
                encrypted_response = received_responses[0]
                decrypted_response = self.decrypt_data(encrypted_response)
                
                try:
                    response_json = json.loads(decrypted_response.decode())
                    self.log(f"Decrypted MQTT response: {response_json}", "success")
                    self.results["mqtt_encrypted_comm"] = TestResult.SUCCESS
                    return True
                except Exception as e:
                    self.log(f"Failed to parse MQTT decrypted response: {e}", "error")
            
            self.results["mqtt_encrypted_comm"] = TestResult.FAILED
            return False
            
        except Exception as e:
            self.log(f"MQTT encrypted communication failed: {e}", "error")
            self.results["mqtt_encrypted_comm"] = TestResult.FAILED
            return False

    # ==================== MAIN TEST RUNNER ====================
    
    async def run_ble_tests(self, device_name: str) -> bool:
        """Esegue tutti i test BLE"""
        self.log("🔵 Starting BLE Security1 tests")
        
        # Discovery
        device_address = await self.test_ble_discovery(device_name)
        if not device_address:
            return False
        
        # Handshake
        handshake_success = await self.test_ble_handshake(device_address)
        if not handshake_success:
            return False
        
        # Encrypted communication
        comm_success = await self.test_ble_encrypted_communication(device_address)
        
        return handshake_success and comm_success
    
    def run_mqtt_tests(self, broker_url: str = "ws://broker.emqx.io:8083/mqtt", topic_prefix: str = "security1/esp32") -> bool:
        """Esegue tutti i test MQTT"""
        self.log("🟠 Starting MQTT Security1 tests")
        
        # Connection
        if not self.test_mqtt_connection(broker_url):
            return False
        
        # Reset session per MQTT
        self.session = Security1Session()
        
        # Handshake
        handshake_success = self.test_mqtt_handshake(broker_url, topic_prefix)
        if not handshake_success:
            return False
        
        # Encrypted communication
        comm_success = self.test_mqtt_encrypted_communication(broker_url, topic_prefix)
        
        return handshake_success and comm_success
    
    def print_results(self):
        """Stampa riepilogo risultati"""
        print("\\n" + "="*60)
        print("🧪 SECURITY1 TEST RESULTS")
        print("="*60)
        
        for test_name, result in self.results.items():
            status = result.value
            print(f"{status} {test_name.replace('_', ' ').title()}")
        
        total_tests = len(self.results)
        passed_tests = sum(1 for r in self.results.values() if r == TestResult.SUCCESS)
        failed_tests = sum(1 for r in self.results.values() if r == TestResult.FAILED)
        skipped_tests = sum(1 for r in self.results.values() if r == TestResult.SKIPPED)
        
        print(f"\\n📊 Summary: {passed_tests}/{total_tests} passed, {failed_tests} failed, {skipped_tests} skipped")
        
        if passed_tests == total_tests - skipped_tests:
            print("🎉 All tests passed!")
        elif failed_tests > 0:
            print("⚠️ Some tests failed. Check logs for details.")

async def main():
    parser = argparse.ArgumentParser(description="Security1 Handshake Test Suite")
    parser.add_argument("--transport", choices=["ble", "mqtt", "both"], default="both",
                       help="Transport to test")
    parser.add_argument("--device", default="ESP32_Security1",
                       help="BLE device name to connect to")
    parser.add_argument("--broker", default="ws://broker.emqx.io:8083/mqtt",
                       help="MQTT broker WebSocket URL")
    parser.add_argument("--topic", default="security1/esp32",
                       help="MQTT topic prefix")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="Enable verbose logging")
    parser.add_argument("--pop", default="test_pop_12345",
                       help="Proof of Possession string (must match ESP32 configuration)")
    
    args = parser.parse_args()
    
    # Verifica dipendenze
    missing_deps = []
    if args.transport in ["ble", "both"] and not BLE_AVAILABLE:
        missing_deps.append("bleak")
    if args.transport in ["mqtt", "both"] and not MQTT_AVAILABLE:
        missing_deps.append("paho-mqtt")
    if not CRYPTO_AVAILABLE:
        missing_deps.append("cryptography")
    
    if missing_deps:
        print(f"❌ Missing dependencies: {', '.join(missing_deps)}")
        print(f"Install with: pip install {' '.join(missing_deps)}")
        return 1
    
    # Inizializza tester
    tester = Security1Tester(verbose=args.verbose, pop=args.pop)
    
    print("🚀 Starting Security1 Test Suite")
    print(f"Transport: {args.transport}")
    print(f"BLE Device: {args.device}")
    print(f"MQTT Broker: {args.broker}")
    print(f"MQTT Topic: {args.topic}")
    print(f"PoP: {args.pop}")
    print("-" * 50)
    
    overall_success = True
    
    # Esegui test BLE
    if args.transport in ["ble", "both"]:
        ble_success = await tester.run_ble_tests(args.device)
        overall_success = overall_success and ble_success
    
    # Esegui test MQTT
    if args.transport in ["mqtt", "both"]:
        mqtt_success = tester.run_mqtt_tests(args.broker, args.topic)
        overall_success = overall_success and mqtt_success
    
    # Stampa risultati
    tester.print_results()
    
    return 0 if overall_success else 1

if __name__ == "__main__":
    exit(asyncio.run(main()))