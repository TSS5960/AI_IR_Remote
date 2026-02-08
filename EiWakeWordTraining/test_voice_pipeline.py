#!/usr/bin/env python3
"""
Complete Voice Command Pipeline Test

Tests the full pipeline: Audio -> Speech-to-Text -> LLM Intent Parsing

This simulates what will happen on the ESP32:
1. Wake word detected ("Hey Bob")
2. Record voice command
3. Send to Groq Whisper for transcription
4. Send text to Groq LLaMA for intent parsing
5. Execute the parsed command

Setup:
    1. Get API key from https://console.groq.com
    2. pip install requests
    3. python test_voice_pipeline.py <audio_file.wav>

Groq Free Tier Limits:
    - 30 requests per minute
    - 14,400 requests per day
    - No credit card required
"""

import requests
import os
import sys
import json
import time

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

GROQ_API_KEY = "YOUR_GROQ_API_KEY_HERE"  # <-- PASTE YOUR KEY HERE

# ==============================================================================
# END OF CONFIGURATION - Don't modify below unless you know what you're doing
# ==============================================================================

# API endpoints
WHISPER_URL = "https://api.groq.com/openai/v1/audio/transcriptions"
LLM_URL = "https://api.groq.com/openai/v1/chat/completions"

# Models
WHISPER_MODEL = "whisper-large-v3"
LLM_MODEL = "llama3-8b-8192"

# System prompt for intent parsing
SYSTEM_PROMPT = """You are a smart home assistant. Parse user commands and return JSON.

Available actions:
- ac_on: Turn on air conditioner
- ac_off: Turn off air conditioner
- ac_temp: Set temperature (value: 16-30)
- ac_mode: Set mode (value: cool, heat, dry, fan, auto)
- light_on: Turn on lights
- light_off: Turn off lights
- ir_send: Send learned IR signal (value: 1-40)

Respond with ONLY valid JSON in this format:
{"actions": [{"type": "action_name", "value": optional_value}]}

Examples:
- "turn on the AC" -> {"actions": [{"type": "ac_on"}]}
- "set to 22 degrees" -> {"actions": [{"type": "ac_temp", "value": 22}]}
- "it's hot" -> {"actions": [{"type": "ac_on"}, {"type": "ac_mode", "value": "cool"}]}
"""


# ========== Functions ==========

def check_api_key():
    """Check if API key is configured"""
    if GROQ_API_KEY == "YOUR_GROQ_API_KEY_HERE":
        print("=" * 50)
        print("ERROR: Groq API key not configured!")
        print("=" * 50)
        print("\nSteps to get your free API key:")
        print("1. Go to: https://console.groq.com")
        print("2. Sign up with Google/GitHub")
        print("3. Click 'API Keys' -> 'Create API Key'")
        print("4. Copy the key (starts with 'gsk_')")
        print("\nThen either:")
        print("  - Set environment variable: set GROQ_API_KEY=gsk_xxx")
        print("  - Or edit this file and replace YOUR_GROQ_API_KEY_HERE")
        print()
        return False
    return True


def transcribe_audio(audio_path):
    """
    Step 1: Convert audio to text using Groq Whisper
    """
    print(f"[1/3] Transcribing audio: {os.path.basename(audio_path)}")

    if not os.path.exists(audio_path):
        print(f"      ERROR: File not found: {audio_path}")
        return None

    headers = {"Authorization": f"Bearer {GROQ_API_KEY}"}

    try:
        with open(audio_path, "rb") as f:
            files = {"file": (os.path.basename(audio_path), f, "audio/wav")}
            data = {"model": WHISPER_MODEL, "language": "en"}

            start_time = time.time()
            response = requests.post(WHISPER_URL, headers=headers, files=files, data=data, timeout=30)
            elapsed = time.time() - start_time

        if response.status_code == 200:
            result = response.json()
            text = result.get("text", "").strip()
            print(f"      Transcription: \"{text}\"")
            print(f"      Time: {elapsed:.2f}s")
            return text
        else:
            print(f"      ERROR: API returned {response.status_code}")
            print(f"      {response.text}")
            return None

    except Exception as e:
        print(f"      ERROR: {e}")
        return None


def parse_intent(text):
    """
    Step 2: Parse intent using Groq LLaMA
    """
    print(f"[2/3] Parsing intent...")

    headers = {
        "Authorization": f"Bearer {GROQ_API_KEY}",
        "Content-Type": "application/json"
    }

    payload = {
        "model": LLM_MODEL,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": text}
        ],
        "temperature": 0.1,
        "max_tokens": 150
    }

    try:
        start_time = time.time()
        response = requests.post(LLM_URL, headers=headers, json=payload, timeout=30)
        elapsed = time.time() - start_time

        if response.status_code == 200:
            result = response.json()
            content = result["choices"][0]["message"]["content"]

            # Try to parse JSON from response
            try:
                # Find JSON in response
                json_start = content.find("{")
                json_end = content.rfind("}") + 1
                if json_start >= 0 and json_end > json_start:
                    json_str = content[json_start:json_end]
                    intent = json.loads(json_str)
                else:
                    intent = json.loads(content)

                print(f"      Intent: {json.dumps(intent)}")
                print(f"      Time: {elapsed:.2f}s")
                return intent

            except json.JSONDecodeError:
                print(f"      WARNING: Could not parse JSON from: {content}")
                return {"raw": content, "actions": []}

        else:
            print(f"      ERROR: API returned {response.status_code}")
            print(f"      {response.text}")
            return None

    except Exception as e:
        print(f"      ERROR: {e}")
        return None


def execute_actions(intent):
    """
    Step 3: Execute parsed actions (simulation)
    """
    print(f"[3/3] Executing actions...")

    if not intent or "actions" not in intent:
        print("      No actions to execute")
        return

    actions = intent.get("actions", [])

    if not actions:
        print("      No actions found")
        return

    for action in actions:
        action_type = action.get("type", "unknown")
        value = action.get("value")

        if action_type == "ac_on":
            print("      -> AC: Turning ON")
        elif action_type == "ac_off":
            print("      -> AC: Turning OFF")
        elif action_type == "ac_temp":
            temp = max(16, min(30, value)) if value else 24
            print(f"      -> AC: Setting temperature to {temp}°C")
        elif action_type == "ac_mode":
            print(f"      -> AC: Setting mode to {value}")
        elif action_type == "light_on":
            print("      -> Lights: Turning ON")
        elif action_type == "light_off":
            print("      -> Lights: Turning OFF")
        elif action_type == "ir_send":
            print(f"      -> IR: Sending signal {value}")
        else:
            print(f"      -> Unknown action: {action_type}")


def process_voice_command(audio_path):
    """
    Full pipeline: Audio -> Text -> Intent -> Execute
    """
    print("\n" + "=" * 60)
    print("  VOICE COMMAND PIPELINE TEST")
    print("=" * 60)
    print(f"  Audio: {audio_path}")
    print(f"  STT Model: {WHISPER_MODEL}")
    print(f"  LLM Model: {LLM_MODEL}")
    print("=" * 60 + "\n")

    total_start = time.time()

    # Step 1: Transcribe
    text = transcribe_audio(audio_path)
    if not text:
        print("\nPipeline FAILED at transcription step")
        return False

    print()

    # Step 2: Parse intent
    intent = parse_intent(text)
    if not intent:
        print("\nPipeline FAILED at intent parsing step")
        return False

    print()

    # Step 3: Execute
    execute_actions(intent)

    total_time = time.time() - total_start

    print("\n" + "=" * 60)
    print(f"  PIPELINE COMPLETE!")
    print(f"  Total time: {total_time:.2f}s")
    print("=" * 60 + "\n")

    return True


def test_llm_only(text):
    """Test LLM parsing with text input (no audio)"""
    if not check_api_key():
        return

    print("\n" + "=" * 60)
    print("  LLM INTENT PARSING TEST")
    print("=" * 60)
    print(f"  Input: \"{text}\"")
    print("=" * 60 + "\n")

    intent = parse_intent(text)
    if intent:
        print()
        execute_actions(intent)


def interactive_mode():
    """Interactive testing mode"""
    if not check_api_key():
        return

    print("\n" + "=" * 50)
    print("  Voice Command Pipeline - Interactive Mode")
    print("=" * 50)
    print("\nCommands:")
    print("  <filepath.wav>  - Process audio file")
    print("  text <message>  - Test LLM with text input")
    print("  demo            - Run demo with sample phrases")
    print("  q               - Quit")
    print()

    while True:
        try:
            cmd = input(">> ").strip()
        except (KeyboardInterrupt, EOFError):
            break

        if not cmd:
            continue

        if cmd.lower() in ['q', 'quit', 'exit']:
            break

        if cmd.lower().startswith('text '):
            text = cmd[5:].strip()
            if text:
                test_llm_only(text)

        elif cmd.lower() == 'demo':
            demo_phrases = [
                "turn on the AC",
                "set temperature to 22 degrees",
                "it's too hot in here",
                "switch to cooling mode",
                "turn off all devices"
            ]
            print("\n--- Running Demo ---\n")
            for phrase in demo_phrases:
                print(f"\nTesting: \"{phrase}\"")
                print("-" * 40)
                intent = parse_intent(phrase)
                if intent:
                    execute_actions(intent)
                print()

        elif os.path.exists(cmd):
            process_voice_command(cmd)

        else:
            # Try as direct text input
            test_llm_only(cmd)

        print()

    print("\nGoodbye!")


def main():
    """Main entry point"""
    if not check_api_key():
        return

    if len(sys.argv) > 1:
        arg = sys.argv[1]

        if arg in ['-h', '--help']:
            print(__doc__)
            return

        if arg == '--demo':
            demo_phrases = [
                "turn on the AC",
                "set temperature to 22",
                "it's too cold, make it warmer",
                "turn off everything"
            ]
            for phrase in demo_phrases:
                print(f"\n{'='*50}")
                print(f"Testing: \"{phrase}\"")
                print('='*50)
                intent = parse_intent(phrase)
                if intent:
                    execute_actions(intent)
            return

        if os.path.exists(arg):
            process_voice_command(arg)
        else:
            # Treat as text input
            test_llm_only(arg)
    else:
        interactive_mode()


if __name__ == "__main__":
    main()
