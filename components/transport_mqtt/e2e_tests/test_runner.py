#!/usr/bin/env python3
"""
ESP32 Transport MQTT E2E Test Runner

This script coordinates flashing ESP32 firmware and running E2E tests
against a real MQTT broker.
"""

import subprocess
import time
import sys
import os
import argparse
import serial
import threading
import queue
from pathlib import Path

class ESP32TestRunner:
    """Manages ESP32 flashing and test execution"""
    
    def __init__(self, esp32_port=None, build_dir=None):
        self.esp32_port = esp32_port
        self.build_dir = build_dir or self._find_build_dir()
        self.serial_output = queue.Queue()
        self.serial_thread = None
        self.esp32_ready = False
        
    def _find_build_dir(self):
        """Find the ESP32 build directory"""
        # Look for build directory relative to this script
        script_dir = Path(__file__).parent
        project_root = script_dir.parent.parent.parent.parent
        build_dir = project_root / "build"
        
        if build_dir.exists():
            return str(build_dir)
        
        raise FileNotFoundError("Could not find ESP32 build directory")
    
    def _find_esp32_port(self):
        """Try to auto-detect ESP32 port"""
        import serial.tools.list_ports
        
        # Common ESP32 USB-to-serial chip VIDs
        esp32_vids = [0x10C4, 0x1A86, 0x0403]  # CP210x, CH340, FTDI
        
        for port in serial.tools.list_ports.comports():
            if port.vid in esp32_vids:
                print(f"🔍 Found potential ESP32 at {port.device}")
                return port.device
        
        return None
    
    def flash_esp32(self, force_rebuild=False):
        """Flash ESP32 with current firmware"""
        print("🔨 Building and flashing ESP32 firmware...")
        
        # Change to project root directory
        project_root = Path(self.build_dir).parent
        original_cwd = os.getcwd()
        os.chdir(project_root)
        
        try:
            # Build if needed
            if force_rebuild or not Path(self.build_dir).exists():
                print("🏗️ Building firmware...")
                result = subprocess.run(
                    ["idf.py", "build"],
                    capture_output=True,
                    text=True
                )
                
                if result.returncode != 0:
                    print(f"❌ Build failed:\n{result.stderr}")
                    return False
                
                print("✅ Build completed")
            
            # Flash firmware
            flash_cmd = ["idf.py", "flash"]
            if self.esp32_port:
                flash_cmd.extend(["-p", self.esp32_port])
            
            print(f"📤 Flashing firmware...")
            result = subprocess.run(flash_cmd, capture_output=True, text=True)
            
            if result.returncode != 0:
                print(f"❌ Flash failed:\n{result.stderr}")
                return False
            
            print("✅ Firmware flashed successfully")
            return True
            
        finally:
            os.chdir(original_cwd)
    
    def _monitor_serial(self):
        """Monitor ESP32 serial output in background thread"""
        if not self.esp32_port:
            return
        
        try:
            ser = serial.Serial(self.esp32_port, 115200, timeout=1)
            
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.serial_output.put(line)
                    
                    # Check for ready indicators
                    if "MQTT connesso al broker" in line or "WiFi connesso" in line:
                        self.esp32_ready = True
                        
        except Exception as e:
            print(f"⚠️ Serial monitoring error: {e}")
    
    def wait_for_esp32_ready(self, timeout=60):
        """Wait for ESP32 to be ready for testing"""
        print("⏳ Waiting for ESP32 to connect to WiFi and MQTT...")
        
        # Auto-detect port if not specified
        if not self.esp32_port:
            self.esp32_port = self._find_esp32_port()
            if not self.esp32_port:
                print("⚠️ Could not auto-detect ESP32 port, skipping serial monitoring")
                print("🕐 Waiting 30s for manual startup...")
                time.sleep(30)
                return True
        
        # Start serial monitoring
        self.serial_thread = threading.Thread(target=self._monitor_serial, daemon=True)
        self.serial_thread.start()
        
        start_time = time.time()
        
        while (time.time() - start_time) < timeout:
            # Show recent serial output
            while not self.serial_output.empty():
                try:
                    line = self.serial_output.get_nowait()
                    print(f"📟 {line}")
                except queue.Empty:
                    break
            
            if self.esp32_ready:
                print("✅ ESP32 is ready for testing!")
                return True
            
            time.sleep(1)
        
        print("⚠️ Timeout waiting for ESP32 ready, proceeding anyway...")
        return False
    
    def run_pytest(self, test_files=None, verbose=True):
        """Run pytest on E2E tests"""
        print("🧪 Running E2E tests...")
        
        # Build pytest command
        pytest_cmd = ["python", "-m", "pytest"]
        
        if verbose:
            pytest_cmd.append("-v")
        
        if test_files:
            pytest_cmd.extend(test_files)
        else:
            # Run all tests in current directory
            pytest_cmd.append(".")
        
        # Add output options
        pytest_cmd.extend([
            "--tb=short",
            "--color=yes"
        ])
        
        # Run tests
        result = subprocess.run(pytest_cmd)
        
        return result.returncode == 0

def main():
    parser = argparse.ArgumentParser(description="ESP32 Transport MQTT E2E Test Runner")
    parser.add_argument("--port", "-p", help="ESP32 serial port (auto-detect if not specified)")
    parser.add_argument("--no-flash", action="store_true", help="Skip flashing, run tests only")
    parser.add_argument("--rebuild", action="store_true", help="Force rebuild before flashing")
    parser.add_argument("--tests", nargs="*", help="Specific test files to run")
    parser.add_argument("--timeout", type=int, default=60, help="Timeout for ESP32 ready (seconds)")
    
    args = parser.parse_args()
    
    print("🚀 ESP32 Transport MQTT E2E Test Runner")
    print("=" * 50)
    
    runner = ESP32TestRunner(esp32_port=args.port)
    
    try:
        # Flash firmware unless skipped
        if not args.no_flash:
            success = runner.flash_esp32(force_rebuild=args.rebuild)
            if not success:
                print("❌ Failed to flash ESP32")
                return 1
            
            # Wait for ESP32 to be ready
            time.sleep(3)  # Give time for boot
            runner.wait_for_esp32_ready(timeout=args.timeout)
        
        # Run tests
        success = runner.run_pytest(test_files=args.tests)
        
        if success:
            print("\n✅ All E2E tests passed!")
            return 0
        else:
            print("\n❌ Some E2E tests failed!")
            return 1
            
    except KeyboardInterrupt:
        print("\n🛑 Tests interrupted by user")
        return 1
    except Exception as e:
        print(f"\n💥 Unexpected error: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())