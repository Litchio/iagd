#!/usr/bin/env python3
"""
Test client for GDIA TCP backend
Connects to 127.0.0.1:1337 and sends sample item data
"""

import json
import socket
import time
import sys

def send_message(sock, message_type: str, item_data: dict = None):
    """Send a message to the TCP server"""
    msg = {
        "type": message_type,
        "timestamp": int(time.time() * 1000),
        "dataLength": len(json.dumps(item_data)) if item_data else 0,
    }
    if item_data:
        msg["item_data"] = item_data
    
    json_str = json.dumps(msg) + "\n"
    print(f"[CLIENT] Sending: {json_str.strip()}")
    sock.sendall(json_str.encode())
    
    # Read response
    response = sock.recv(1024).decode()
    print(f"[SERVER] Response: {response.strip()}")
    return response

def main():
    HOST = "127.0.0.1"
    PORT = 1337
    
    try:
        print(f"[CLIENT] Connecting to {HOST}:{PORT}...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print("[CLIENT] ✅ Connected!")
        
        # Test 1: Send a basic game engine update
        print("\n[TEST 1] Sending TYPE_GAMEENGINE_UPDATE...")
        send_message(sock, "TYPE_GAMEENGINE_UPDATE")
        time.sleep(0.5)
        
        # Test 2: Send a stash item with stats
        print("\n[TEST 2] Sending TYPE_Stash_Item_BasicInfo (Legendary Sword)...")
        item_data = {
            "record": "x1_loot_legendary_sword_01",
            "character_name": "TestHero",
            "stash_tab": 0,
            "slot_x": 5,
            "slot_y": 10,
            "stats": [
                {
                    "stat": "LifeLeech",
                    "value": 25.5,
                    "text_value": "25% Life Leech"
                },
                {
                    "stat": "ElementalDamage",
                    "value": 150.0,
                    "text_value": "+150 Elemental Damage"
                },
                {
                    "stat": "PhysicalDamage",
                    "value": 200.0,
                    "text_value": "+200 Physical Damage"
                }
            ]
        }
        send_message(sock, "TYPE_Stash_Item_BasicInfo", item_data)
        time.sleep(0.5)
        
        # Test 3: Send another item
        print("\n[TEST 3] Sending TYPE_Stash_Item_BasicInfo (Unique Ring)...")
        item_data = {
            "record": "x1_loot_unique_ring_01",
            "character_name": "TestHero",
            "stash_tab": 1,
            "slot_x": 2,
            "slot_y": 3,
            "stats": [
                {
                    "stat": "IntelligenceBonus",
                    "value": 5.0,
                    "text_value": "+5 Intelligence"
                },
                {
                    "stat": "ColdResistance",
                    "value": 30.0,
                    "text_value": "+30% Cold Resistance"
                }
            ]
        }
        send_message(sock, "TYPE_Stash_Item_BasicInfo", item_data)
        time.sleep(0.5)
        
        print("\n[CLIENT] ✅ All tests sent!")
        sock.close()
        
    except ConnectionRefusedError:
        print(f"[ERROR] Could not connect to {HOST}:{PORT}")
        print("[ERROR] Make sure the backend is running: cargo run")
        sys.exit(1)
    except Exception as e:
        print(f"[ERROR] {type(e).__name__}: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
