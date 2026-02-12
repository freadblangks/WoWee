#!/usr/bin/env python3
"""
Extract Warden RSA public key modulus from WoW 3.3.5a executable.

The RSA-2048 public key consists of:
- Exponent: 0x010001 (65537) - always the same
- Modulus: 256 bytes - hardcoded in WoW.exe

This script searches for the modulus by looking for known patterns.
"""

import sys
import struct

def find_warden_modulus(exe_path):
    """
    Find Warden RSA modulus in WoW.exe

    The modulus is typically stored as a 256-byte array in the .rdata or .data section.
    It's near Warden-related code and often preceded by the exponent (0x010001).
    """

    with open(exe_path, 'rb') as f:
        data = f.read()

    print(f"[*] Loaded {len(data)} bytes from {exe_path}")

    # Search for RSA exponent (0x010001 = 65537)
    # In little-endian: 01 00 01 00
    exponent_pattern = b'\x01\x00\x01\x00'

    print("[*] Searching for RSA exponent pattern (0x010001)...")

    matches = []
    offset = 0
    while True:
        offset = data.find(exponent_pattern, offset)
        if offset == -1:
            break
        matches.append(offset)
        offset += 1

    print(f"[*] Found {len(matches)} potential exponent locations")

    # For each match, check if there's a 256-byte modulus nearby
    for exp_offset in matches:
        # Modulus typically comes after exponent or within 256 bytes
        for modulus_offset in range(max(0, exp_offset - 512), min(len(data), exp_offset + 512)):
            # Check if we have space for 256 bytes
            if modulus_offset + 256 > len(data):
                continue

            modulus_candidate = data[modulus_offset:modulus_offset + 256]

            # Heuristic: RSA modulus should have high entropy (appears random)
            # Check for non-zero bytes and variety
            non_zero = sum(1 for b in modulus_candidate if b != 0)
            unique_bytes = len(set(modulus_candidate))

            if non_zero > 200 and unique_bytes > 100:
                print(f"\n[+] Potential modulus at offset 0x{modulus_offset:08x} (near exponent at 0x{exp_offset:08x})")
                print(f"    Non-zero bytes: {non_zero}/256")
                print(f"    Unique bytes: {unique_bytes}")
                print(f"    First 32 bytes: {modulus_candidate[:32].hex()}")
                print(f"    Last 32 bytes: {modulus_candidate[-32:].hex()}")

                # Check if it looks like a valid RSA modulus (high bit set)
                if modulus_candidate[-1] & 0x80:
                    print(f"    [✓] High bit set (typical for RSA modulus)")
                else:
                    print(f"    [!] High bit not set (unusual)")

                # Write to C++ array format
                print(f"\n[*] C++ array format:")
                print_cpp_array(modulus_candidate)

    return None

def print_cpp_array(data):
    """Print byte array in C++ format"""
    print("const uint8_t modulus[256] = {")
    for i in range(0, 256, 16):
        chunk = data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02X}' for b in chunk)
        print(f"    {hex_bytes},")
    print("};")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path_to_Wow.exe>")
        sys.exit(1)

    exe_path = sys.argv[1]
    find_warden_modulus(exe_path)
