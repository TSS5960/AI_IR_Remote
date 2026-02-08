#!/usr/bin/env python3
"""
Edge Impulse Audio Sample Capture Tool

Captures WAV audio samples from ESP32 via Serial and saves them
for upload to Edge Impulse.

Usage:
    python capture_samples.py                    # Interactive mode
    python capture_samples.py hey_bob            # Record one hey_bob sample
    python capture_samples.py noise --count 10   # Record 10 noise samples

Requirements:
    pip install pyserial

Setup:
    1. Upload audio_capture.ino to ESP32
    2. Find your COM port (e.g., COM3 on Windows, /dev/ttyUSB0 on Linux)
    3. Update SERIAL_PORT below or pass as argument
"""

import serial
import serial.tools.list_ports
import os
import sys
import time
from datetime import datetime

# ========== Configuration ==========
SERIAL_PORT = "COM5"  # Auto-detect if None, or set manually: "COM3" or "/dev/ttyUSB0"
BAUD_RATE = 115200
OUTPUT_DIR = "training_samples"
TIMEOUT = 15  # seconds to wait for recording


def find_esp32_port():
    """Auto-detect ESP32 serial port"""
    ports = serial.tools.list_ports.comports()

    for port in ports:
        # Common ESP32 identifiers
        if any(x in port.description.lower() for x in ['cp210', 'ch340', 'usb', 'serial']):
            return port.device
        if any(x in port.manufacturer.lower() if port.manufacturer else False
               for x in ['silicon', 'wch', 'ftdi']):
            return port.device

    # Return first available port if no ESP32-specific found
    if ports:
        return ports[0].device

    return None


def list_ports():
    """List all available serial ports"""
    ports = serial.tools.list_ports.comports()

    if not ports:
        print("No serial ports found!")
        return

    print("\nAvailable serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
    print()


def capture_sample(ser, label):
    """Send record command and capture WAV data"""

    # Clear any pending data
    ser.reset_input_buffer()

    # Send record command
    command = f"{label}\n"
    ser.write(command.encode())
    print(f"\nSent command: {label}")
    print("Waiting for ESP32 to record...")

    # Wait for WAV data
    capturing = False
    hex_data = ""
    actual_label = label

    start_time = time.time()

    while True:
        # Check timeout
        if time.time() - start_time > TIMEOUT:
            print("ERROR: Timeout waiting for data")
            return False

        # Read line
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
        except Exception as e:
            print(f"Read error: {e}")
            continue

        if not line:
            continue

        # Print ESP32 output (except hex data)
        if not capturing:
            print(f"  {line}")

        # Check for WAV start marker
        if line.startswith("---WAV_START"):
            capturing = True
            # Extract label from marker if present
            if ":" in line:
                actual_label = line.split(":")[1].replace("---", "").strip()
            print("\n  Receiving WAV data...")
            continue

        # Check for WAV end marker
        if "---WAV_END---" in line:
            break

        # Capture hex data
        if capturing:
            hex_data += line

    # Validate data
    if not hex_data:
        print("ERROR: No audio data received")
        return False

    # Convert hex to binary
    try:
        binary_data = bytes.fromhex(hex_data)
    except ValueError as e:
        print(f"ERROR: Invalid hex data: {e}")
        return False

    # Create output directory
    label_dir = os.path.join(OUTPUT_DIR, actual_label)
    os.makedirs(label_dir, exist_ok=True)

    # Generate filename with timestamp
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = os.path.join(label_dir, f"{actual_label}_{timestamp}.wav")

    # Save WAV file
    with open(filename, 'wb') as f:
        f.write(binary_data)

    file_size = len(binary_data)
    duration = (file_size - 44) / (16000 * 2)  # 16kHz, 16-bit

    print(f"\n  Saved: {filename}")
    print(f"  Size: {file_size} bytes ({duration:.2f} seconds)")

    return True


def interactive_mode(ser):
    """Interactive recording session"""

    print("\n" + "=" * 50)
    print("  Interactive Recording Mode")
    print("=" * 50)
    print("\nCommands:")
    print("  1 or h  - Record 'hey_bob' sample")
    print("  2 or n  - Record 'noise' sample")
    print("  3 or u  - Record 'unknown' sample")
    print("  t       - Test microphone level")
    print("  s       - Show sample counts")
    print("  q       - Quit")
    print()

    counts = {"hey_bob": 0, "noise": 0, "unknown": 0}

    # Count existing samples
    for label in counts.keys():
        label_dir = os.path.join(OUTPUT_DIR, label)
        if os.path.exists(label_dir):
            counts[label] = len([f for f in os.listdir(label_dir) if f.endswith('.wav')])

    show_counts(counts)

    while True:
        try:
            cmd = input("\nCommand (1/2/3/t/s/q): ").strip().lower()
        except (KeyboardInterrupt, EOFError):
            break

        if cmd in ['1', 'h', 'hey_bob']:
            if capture_sample(ser, "hey_bob"):
                counts["hey_bob"] += 1
                show_counts(counts)

        elif cmd in ['2', 'n', 'noise']:
            if capture_sample(ser, "noise"):
                counts["noise"] += 1
                show_counts(counts)

        elif cmd in ['3', 'u', 'unknown']:
            if capture_sample(ser, "unknown"):
                counts["unknown"] += 1
                show_counts(counts)

        elif cmd == 't':
            ser.write(b"test\n")
            print("\nMicrophone test started on ESP32")
            print("Press Enter to stop...")
            input()
            ser.write(b"\n")  # Send any key to stop

        elif cmd == 's':
            show_counts(counts)

        elif cmd in ['q', 'quit', 'exit']:
            break

        else:
            print("Unknown command. Use 1, 2, 3, t, s, or q")

    print("\nRecording session complete!")
    show_counts(counts)
    print(f"\nSamples saved to: {os.path.abspath(OUTPUT_DIR)}")
    print("Upload these folders to Edge Impulse for training.")


def show_counts(counts):
    """Display sample counts with recommendations"""

    print("\n  Sample Counts:")
    print(f"    hey_bob: {counts['hey_bob']:3d} / 50-100 recommended")
    print(f"    noise:   {counts['noise']:3d} / 200+ recommended")
    print(f"    unknown: {counts['unknown']:3d} / 30-50 recommended")

    total = sum(counts.values())
    print(f"    ─────────────────────")
    print(f"    Total:   {total:3d} samples")


def batch_record(ser, label, count):
    """Record multiple samples of the same type"""

    print(f"\nBatch recording {count} '{label}' samples...")
    print("Press Ctrl+C to cancel\n")

    successful = 0

    for i in range(count):
        print(f"\n--- Sample {i+1}/{count} ---")
        try:
            if capture_sample(ser, label):
                successful += 1

            if i < count - 1:
                print("\nNext sample in 2 seconds...")
                time.sleep(2)

        except KeyboardInterrupt:
            print("\n\nBatch recording cancelled")
            break

    print(f"\nCompleted: {successful}/{count} samples recorded")


def main():
    """Main entry point"""

    global SERIAL_PORT

    # Parse arguments
    label = None
    count = 1
    port_override = None

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]

        if arg in ['--port', '-p']:
            port_override = args[i + 1]
            i += 2
            continue

        if arg in ['--count', '-c']:
            count = int(args[i + 1])
            i += 2
            continue

        if arg in ['--list', '-l']:
            list_ports()
            return

        if arg in ['--help', '-h']:
            print(__doc__)
            return

        if arg in ['hey_bob', 'noise', 'unknown']:
            label = arg

        i += 1

    # Determine serial port
    if port_override:
        SERIAL_PORT = port_override
    elif SERIAL_PORT is None:
        SERIAL_PORT = find_esp32_port()

    if not SERIAL_PORT:
        print("ERROR: No serial port found!")
        print("Use --port to specify, or --list to see available ports")
        list_ports()
        return

    print(f"Using serial port: {SERIAL_PORT}")

    # Connect to ESP32
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Connected to ESP32")
        time.sleep(2)  # Wait for ESP32 to reset

        # Clear startup messages
        ser.reset_input_buffer()

    except serial.SerialException as e:
        print(f"ERROR: Could not open serial port: {e}")
        return

    try:
        if label:
            # Single label mode
            if count > 1:
                batch_record(ser, label, count)
            else:
                capture_sample(ser, label)
        else:
            # Interactive mode
            interactive_mode(ser)

    finally:
        ser.close()
        print("\nSerial connection closed")


if __name__ == "__main__":
    main()
