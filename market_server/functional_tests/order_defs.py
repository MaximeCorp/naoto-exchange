#!/usr/bin/env python3
"""order_defs.py — shared struct definitions matching the C++ wire format.

All structs are little-endian + packed to match x86-64 memcpy'd layout.
Verify with the sizeof() prints in the sender/receiver against C++ sizeof(...).
"""
import ctypes

LIMIT, MARKET = 0, 1
BUY, SELL = 0, 1


class Order(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("Id",        ctypes.c_uint32),
        ("Type",      ctypes.c_int32),   # enum class OrderType : int32_t
        ("Side",      ctypes.c_int32),   # enum class OrderSide : int32_t
        ("Price",     ctypes.c_int64),
        ("ClientId",  ctypes.c_int32),
        ("Amount",    ctypes.c_uint32),
        ("Asset",     ctypes.c_int32),
        ("Timestamp", ctypes.c_int64),
    ]


class OrderBookUpdate(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("SequenceId", ctypes.c_uint32),
        ("Depth",      ctypes.c_uint32),
        ("Price",      ctypes.c_int64),
        ("AssetId",    ctypes.c_uint16),
        ("Side",       ctypes.c_uint8),
    ]


class OrderStateReport(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("SequenceId",    ctypes.c_uint32),
        ("BoughtDelta",   ctypes.c_int64),
        ("SoldDelta",     ctypes.c_int64),
        ("ClientId",      ctypes.c_uint32),
        ("OrderId",       ctypes.c_uint32),
        ("TradeId",       ctypes.c_uint32),
        ("BoughtAssetId", ctypes.c_uint16),
        ("SoldAssetId",   ctypes.c_uint16),
    ]