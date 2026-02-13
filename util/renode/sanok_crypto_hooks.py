# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Renode crypto hooks for Sanok. See b/482426716 and b/484348778."""

import hashlib
import hmac


def hook_hmac_sha256_step(cpu, _):
    """Hook for HMAC SHA256 step."""
    bus = cpu.Bus

    input_len = cpu.GetRegister(15).RawValue
    input_addr = cpu.GetRegister(14).RawValue
    key_len = cpu.GetRegister(13).RawValue
    key = cpu.GetRegister(12).RawValue
    mask = cpu.GetRegister(11).RawValue
    output = cpu.GetRegister(10).RawValue

    # TODO(b/484348778): Not sure about this.
    if mask == 0x5C:
        cpu.PC = cpu.RA
        return

    _list = []
    for _ in range(key_len):
        _list.append(bus.ReadByte(key))
        key += 1
    _key = bytes(_list)

    _list = []
    for _ in range(input_len):
        _list.append(bus.ReadByte(input_addr))
        input_addr += 1
    _input = bytes(_list)

    signature = hmac.new(_key, _input, hashlib.sha256).digest()
    signature_bytes = list(bytearray(signature))

    for b in signature_bytes:
        bus.WriteByte(output, b)
        output += 1
    cpu.PC = cpu.RA


def hook__test_sha256(cpu, _):
    """Hook for SHA256 test."""
    bus = cpu.Bus

    output = cpu.GetRegister(12).RawValue
    input_len = cpu.GetRegister(11).RawValue
    input_addr = cpu.GetRegister(10).RawValue

    _list = []
    for _ in range(input_len):
        _list.append(bus.ReadByte(input_addr))
        input_addr += 1

    data = bytes(_list)
    sha256_digest = hashlib.sha256(data).digest()
    digest_bytes = list(bytearray(sha256_digest))

    for b in digest_bytes:
        bus.WriteByte(output, b)
        output += 1
    cpu.PC = cpu.RA


class SxHashHooks:
    """Hooks for sx_hash."""

    def __init__(self):
        self._hasher = hashlib.sha256()

    def hook_feed(self, cpu, _):
        """Hook for sx_hash_feed."""
        bus = cpu.Bus

        cnt = cpu.GetRegister(12).RawValue
        addr = cpu.GetRegister(11).RawValue

        data = bytes([bus.ReadByte(addr + i) for i in range(cnt)])

        self._hasher.update(data)

        cpu.PC = cpu.RA

    def hook_digest(self, cpu, _):
        """Hook for sx_hash_digest."""
        bus = cpu.Bus

        addr = cpu.GetRegister(11).RawValue

        digest_bytes = list(bytearray(self._hasher.digest()))

        for i, b in enumerate(digest_bytes):
            bus.WriteByte(addr + i, b)

        self._hasher = hashlib.sha256()

        cpu.PC = cpu.RA


def mc_AddCustomPythonHooks(cpu):  # pylint: disable=invalid-name
    """Adds custom Python hooks."""
    sx_hash_hooks = SxHashHooks()

    try:
        results = cpu.Bus.GetAllSymbolAddresses("sx_hash_feed")
        for addr in results:
            cpu.AddHook(addr, sx_hash_hooks.hook_feed)
    except Exception as e:  # pylint: disable=broad-except
        print("Error adding hook for sx_hash_feed", e)

    try:
        results = cpu.Bus.GetAllSymbolAddresses("sx_hash_digest")
        for addr in results:
            cpu.AddHook(addr, sx_hash_hooks.hook_digest)
    except Exception as e:  # pylint: disable=broad-except
        print("Error adding hook for sx_hash_digest", e)

    try:
        results = cpu.Bus.GetAllSymbolAddresses("hmac_SHA256_step")
        for addr in results:
            cpu.AddHook(addr, hook_hmac_sha256_step)
    except Exception as e:  # pylint: disable=broad-except
        print("Error adding hook for hmac_SHA256_step", e)

    try:
        results = cpu.Bus.GetAllSymbolAddresses("test_sha256")
        for addr in results:
            cpu.AddHook(addr, hook__test_sha256)
    except Exception as e:  # pylint: disable=broad-except
        print("Error adding hook for test_sha256", e)
