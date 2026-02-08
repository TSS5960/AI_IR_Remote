#!/usr/bin/env python3
"""
Groq Speech-to-Text Test Script

Tests Groq's Whisper API for transcribing audio files.
Use this to verify your API setup before integrating with the ESP32 system.

Setup:
    1. Get API key from https://console.groq.com
    2. Set your key below or use environment variable GROQ_API_KEY
    3. pip install requests
    4. python test_groq_stt.py <audio_file.wav>

Usage:
    python test_groq_stt.py                           # Interactive mode
    python test_groq_stt.py test.wav                  # Transcribe single file
    python test_groq_stt.py training_samples/hey_bob  # Transcribe all WAVs in folder
"""

import requests
import os
import sys
import json
from pathlib import Path

# ==============================================================================
# CONFIGURATION - PASTE YOUR API KEY HERE
# ==============================================================================
#
# Step 1: Get your FREE Groq API key
#         Go to: https://console.groq.com
#         Sign up -> API Keys -> Create API Key
#
# Step 2: Paste your key below (replace the placeholder)
#         Your key looks like: gsk_xxxxxxxxxxxxxxxxxxxxxxxxxxxx
#
# ==============================================================================

GROQ_API_KEY = "YOUR API KEY HERE"  # <-- PASTE YOUR KEY HERE

# ==============================================================================
# END OF CONFIGURATION - Don't modify below unless you know what you're doing
# ==============================================================================

# Groq Whisper API endpoint
GROQ_API_URL = "https://api.groq.com/openai/v1/audio/transcriptions"

# Available Whisper models on Groq
WHISPER_MODELS = {
    "large": "whisper-large-v3",      # Best accuracy, slower
    "medium": "distil-whisper-large-v3-en",  # Good balance
}

# Default model
DEFAULT_MODEL = "whisper-large-v3"


# ========== Functions ==========

def transcribe_audio(audio_path, model=DEFAULT_MODEL):
    """
    Transcribe an audio file using Groq's Whisper API

    Args:
        audio_path: Path to WAV/MP3/etc audio file
        model: Whisper model to use

    Returns:
        dict with 'text' key containing transcription
    """
    if GROQ_API_KEY == "YOUR_GROQ_API_KEY_HERE":
        print("ERROR: Please set your Groq API key!")
        print("  1. Get key from: https://console.groq.com")
        print("  2. Set GROQ_API_KEY environment variable, or")
        print("  3. Edit this file and replace YOUR_GROQ_API_KEY_HERE")
        return None

    if not os.path.exists(audio_path):
        print(f"ERROR: File not found: {audio_path}")
        return None

    headers = {
        "Authorization": f"Bearer {GROQ_API_KEY}"
    }

    try:
        with open(audio_path, "rb") as audio_file:
            files = {
                "file": (os.path.basename(audio_path), audio_file, "audio/wav")
            }
            data = {
                "model": model,
                "response_format": "json",
                "language": "en"  # Force English to avoid wrong language detection
            }

            response = requests.post(
                GROQ_API_URL,
                headers=headers,
                files=files,
                data=data,
                timeout=30
            )

        if response.status_code == 200:
            return response.json()
        else:
            print(f"ERROR: API returned {response.status_code}")
            print(f"Response: {response.text}")
            return None

    except requests.exceptions.Timeout:
        print("ERROR: Request timed out")
        return None
    except Exception as e:
        print(f"ERROR: {e}")
        return None


def transcribe_folder(folder_path, model=DEFAULT_MODEL):
    """Transcribe all WAV files in a folder"""
    folder = Path(folder_path)

    if not folder.exists():
        print(f"ERROR: Folder not found: {folder_path}")
        return

    wav_files = list(folder.glob("*.wav"))

    if not wav_files:
        print(f"No WAV files found in {folder_path}")
        return

    print(f"\nTranscribing {len(wav_files)} files from {folder_path}\n")
    print("=" * 60)

    results = []

    for i, wav_file in enumerate(wav_files, 1):
        print(f"\n[{i}/{len(wav_files)}] {wav_file.name}")

        result = transcribe_audio(str(wav_file), model)

        if result:
            text = result.get("text", "")
            print(f"    -> \"{text}\"")
            results.append({
                "file": wav_file.name,
                "text": text
            })
        else:
            print(f"    -> FAILED")
            results.append({
                "file": wav_file.name,
                "text": None,
                "error": True
            })

    print("\n" + "=" * 60)
    print(f"Completed: {len([r for r in results if r.get('text')])} / {len(wav_files)}")

    return results


def interactive_mode():
    """Interactive mode for testing transcription"""
    print("\n" + "=" * 50)
    print("  Groq Speech-to-Text Test")
    print("=" * 50)
    print(f"\nAPI Key: {'*' * 20}{GROQ_API_KEY[-8:]}" if GROQ_API_KEY != "YOUR_GROQ_API_KEY_HERE" else "\nAPI Key: NOT SET")
    print(f"Model: {DEFAULT_MODEL}")
    print("\nCommands:")
    print("  <filepath>  - Transcribe a single file")
    print("  folder      - Transcribe all WAVs in training_samples/")
    print("  test        - Run test with sample file")
    print("  q           - Quit")
    print()

    while True:
        try:
            cmd = input("Enter file path or command: ").strip()
        except (KeyboardInterrupt, EOFError):
            break

        if not cmd:
            continue

        if cmd.lower() in ['q', 'quit', 'exit']:
            break

        if cmd.lower() == 'folder':
            # Transcribe training samples
            for label in ['hey_bob', 'noise', 'unknown']:
                folder = f"training_samples/{label}"
                if os.path.exists(folder):
                    transcribe_folder(folder)

        elif cmd.lower() == 'test':
            # Find a test file
            test_files = list(Path("training_samples").rglob("*.wav"))
            if test_files:
                print(f"\nTesting with: {test_files[0]}")
                result = transcribe_audio(str(test_files[0]))
                if result:
                    print(f"Transcription: \"{result.get('text', '')}\"")
            else:
                print("No test files found. Record some samples first.")

        elif os.path.exists(cmd):
            if os.path.isdir(cmd):
                transcribe_folder(cmd)
            else:
                result = transcribe_audio(cmd)
                if result:
                    print(f"\nTranscription: \"{result.get('text', '')}\"")

        else:
            print(f"File not found: {cmd}")

        print()

    print("\nGoodbye!")


def main():
    """Main entry point"""

    # Check for command line arguments
    if len(sys.argv) > 1:
        target = sys.argv[1]

        if target in ['-h', '--help']:
            print(__doc__)
            return

        if os.path.isdir(target):
            transcribe_folder(target)
        elif os.path.isfile(target):
            result = transcribe_audio(target)
            if result:
                print(f"\nTranscription: \"{result.get('text', '')}\"")
        else:
            print(f"ERROR: Path not found: {target}")
    else:
        # Interactive mode
        interactive_mode()


if __name__ == "__main__":
    main()
