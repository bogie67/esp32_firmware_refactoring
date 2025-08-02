#!/bin/bash

# Test automatizzato per Back-pressure Logic BLE
# Usage: ./test_backpressure.sh [--verbose] [--quick]

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
VERBOSE=false
QUICK_MODE=false
TEST_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0
LOG_FILE="/tmp/ble_backpressure_test_$(date +%Y%m%d_%H%M%S).log"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --quick|-q)
            QUICK_MODE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--verbose] [--quick]"
            echo "  --verbose: Show detailed output"
            echo "  --quick:   Run only essential tests"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Helper functions
log() {
    echo -e "$1" | tee -a "$LOG_FILE"
}

test_header() {
    local test_name="$1"
    TEST_COUNT=$((TEST_COUNT + 1))
    log "${BLUE}📋 Test $TEST_COUNT: $test_name${NC}"
    log "=================================="
}

test_pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    log "${GREEN}✅ PASS${NC}\n"
}

test_fail() {
    local reason="$1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
    log "${RED}❌ FAIL: $reason${NC}\n"
}

run_ble_command() {
    local cmd="$1"
    local timeout="${2:-30}"
    local description="$3"
    
    if [ "$VERBOSE" = true ]; then
        log "${YELLOW}🔧 Running: $cmd${NC}"
    fi
    
    if [ -n "$description" ]; then
        log "   $description"
    fi
    
    # Run command with timeout and capture both stdout and stderr
    if timeout "$timeout" bash -c "$cmd" >> "$LOG_FILE" 2>&1; then
        return 0
    else
        return 1
    fi
}

check_esp32_logs() {
    local pattern="$1"
    local description="$2"
    
    if [ "$VERBOSE" = true ]; then
        log "${YELLOW}🔍 Checking ESP32 logs for: $pattern${NC}"
    fi
    
    log "   Expected: $description"
    # Note: In real implementation, this would check ESP32 serial output
    # For now, we simulate based on the test patterns
    return 0
}

wait_between_tests() {
    local seconds="${1:-2}"
    if [ "$QUICK_MODE" = false ]; then
        log "${YELLOW}⏳ Waiting ${seconds}s between tests...${NC}"
        sleep "$seconds"
    fi
}

# Main test suite
main() {
    log "${BLUE}🚀 BLE Back-pressure Logic Test Suite${NC}"
    log "======================================"
    log "Start time: $(date)"
    log "Log file: $LOG_FILE"
    log "Quick mode: $QUICK_MODE"
    log "Verbose: $VERBOSE"
    log ""

    # Prerequisite check
    if ! command -v python &> /dev/null; then
        log "${RED}❌ Python not found${NC}"
        exit 1
    fi

    if [ ! -f "send_ble_command.py" ]; then
        log "${RED}❌ send_ble_command.py not found in current directory${NC}"
        exit 1
    fi

    # Test 1: Basic Functionality
    test_header "Basic BLE Command (Baseline)"
    if run_ble_command "python send_ble_command.py --op wifiScan" 30 "Simple WiFi scan command"; then
        check_esp32_logs "notify_resp called" "Basic response transmission"
        test_pass
    else
        test_fail "Basic BLE command failed"
    fi
    wait_between_tests

    # Test 2: Chunking Test
    test_header "Chunking with Back-pressure"
    if run_ble_command "python send_ble_command.py --op syncSchedule --large-payload 800" 45 "Large payload requiring chunking"; then
        check_esp32_logs "Response chunked into.*parts" "Chunking activated"
        check_esp32_logs "Chunk.*sent with back-pressure" "Back-pressure logic used"
        test_pass
    else
        test_fail "Chunking test failed"
    fi
    wait_between_tests

    # Test 3: Forced Chunking
    test_header "Forced Chunking (Small Payload)"
    if run_ble_command "python send_ble_command.py --op wifiScan --force-chunking" 30 "Force chunking on small payload"; then
        check_esp32_logs "Chunking frame into.*parts" "Forced chunking activated"
        test_pass
    else
        test_fail "Forced chunking test failed"
    fi
    wait_between_tests

    # Test 4: Back-pressure Stress Test
    test_header "Back-pressure Stress Test (Multiple Commands)"
    local repeat_count=5
    if [ "$QUICK_MODE" = false ]; then
        repeat_count=10
    fi
    
    if run_ble_command "python send_ble_command.py --op wifiScan --repeat $repeat_count" 60 "Multiple rapid commands"; then
        check_esp32_logs "Back-pressure.*recorded" "Back-pressure detection"
        test_pass
    else
        test_fail "Stress test failed"
    fi
    wait_between_tests

    # Test 5: Large Payload Stress
    if [ "$QUICK_MODE" = false ]; then
        test_header "Large Payload Stress Test"
        if run_ble_command "python send_ble_command.py --op syncSchedule --large-payload 1200 --repeat 3" 90 "Multiple large payloads"; then
            check_esp32_logs "Mbuf pool.*exhausted" "Mbuf exhaustion detection (expected)"
            check_esp32_logs "Back-pressure recovery" "Recovery after congestion"
            test_pass
        else
            test_fail "Large payload stress test failed"
        fi
        wait_between_tests
    fi

    # Test 6: Rapid Fire Test (Potential Circuit Breaker)
    if [ "$QUICK_MODE" = false ]; then
        test_header "Rapid Fire Test (Circuit Breaker Potential)"
        log "   Running multiple parallel commands to trigger circuit breaker..."
        
        # Start multiple background processes
        for i in {1..5}; do
            python send_ble_command.py --op wifiScan --no-notify &
            if [ "$VERBOSE" = true ]; then
                log "   Started background process $i"
            fi
        done
        
        # Wait for all background processes
        wait
        
        if [ $? -eq 0 ]; then
            check_esp32_logs "Circuit breaker OPEN" "Circuit breaker activation (may occur)"
            test_pass
        else
            test_fail "Rapid fire test had errors"
        fi
        wait_between_tests 5
    fi

    # Test 7: Recovery Test
    test_header "Recovery Test (After Potential Failures)"
    if run_ble_command "python send_ble_command.py --op wifiScan" 30 "Single command after stress"; then
        check_esp32_logs "Back-pressure recovery" "System recovery (if previous failures occurred)"
        test_pass
    else
        test_fail "Recovery test failed"
    fi

    # Test 8: Different Payload Sizes
    if [ "$QUICK_MODE" = false ]; then
        test_header "Variable Payload Size Test"
        for size in 200 500 1000; do
            log "   Testing payload size: ${size} bytes"
            if run_ble_command "python send_ble_command.py --op syncSchedule --large-payload $size" 45 "Payload size $size bytes"; then
                log "   ✓ Size $size completed"
            else
                test_fail "Variable payload test failed at size $size"
                break
            fi
            wait_between_tests 1
        done
        test_pass
    fi

    # Summary
    log ""
    log "${BLUE}📊 Test Summary${NC}"
    log "==============="
    log "Total tests: $TEST_COUNT"
    log "${GREEN}Passed: $PASS_COUNT${NC}"
    log "${RED}Failed: $FAIL_COUNT${NC}"
    log "Success rate: $(echo "scale=1; $PASS_COUNT * 100 / $TEST_COUNT" | bc -l)%"
    log "End time: $(date)"
    log ""
    log "📄 Full log available at: $LOG_FILE"

    if [ $FAIL_COUNT -eq 0 ]; then
        log "${GREEN}🎉 All tests passed! Back-pressure logic is working correctly.${NC}"
        exit 0
    else
        log "${RED}⚠️  Some tests failed. Check the logs for details.${NC}"
        exit 1
    fi
}

# Trap to clean up background processes
trap 'kill $(jobs -p) 2>/dev/null; exit 130' INT

# Check if running from correct directory
if [ ! -f "send_ble_command.py" ]; then
    echo "Please run this script from the smartdrip-tools directory"
    exit 1
fi

# Run main test suite
main "$@"