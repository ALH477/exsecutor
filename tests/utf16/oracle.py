#!/usr/bin/env python3
"""tests/utf16/oracle.py -- the reference every UTF-8 -> UTF-16 library is held to.

Two independent answers, which must agree, and which neither share a line with
any library under examples/utf16/:

  py_convert   Python's own codec. This is the ORACLE: the libraries are held to it.
  ref_convert  A hand-written WHATWG Encoding Standard UTF-8 decoder, written
               here so the oracle itself can be checked. `audit_oracle` runs
               both over exhaustive and fuzzed input and fails on any
               disagreement. If they ever disagree, the libraries are not the
               thing to doubt first.

The contract being checked is examples/utf16/contractus.exsc. In short, per
record (modus, bytes):

  modus 0 (strict)    stop at the first ill-formed subpart: status 1, positio
                      the offset of its first byte, units = the conversion of
                      everything before it.
  modus 1 (substitue) one U+FFFD per MAXIMAL SUBPART, never fails.

And the wire format of examples/utf16/probatio.exsc, input and output.
"""
import struct

STRICT, SUBSTITUE = 0, 1
CAPACITY = 4096


def _units(text):
    raw = text.encode('utf-16-le')
    return list(struct.unpack('<%dH' % (len(raw) // 2), raw))


def py_convert(data, modus):
    """(status, positio, units) by Python's codec."""
    if modus == SUBSTITUE:
        return 0, 0, _units(data.decode('utf-8', errors='replace'))
    try:
        return 0, 0, _units(data.decode('utf-8'))
    except UnicodeDecodeError as e:
        return 1, e.start, _units(data[:e.start].decode('utf-8'))


def ref_convert(data, modus):
    """(status, positio, units) by a from-scratch WHATWG-style decoder.

    Deliberately a different shape from every library: state registers for
    bytes needed, bytes seen and the allowed range of the next byte, and an
    explicit re-process of the byte that broke a sequence.
    """
    units = []
    i = 0
    n = len(data)
    while i < n:
        start = i
        b = data[i]
        i += 1
        if b < 0x80:
            units.append(b)
            continue
        if 0xC2 <= b <= 0xDF:
            need, cp, lower, upper = 1, b & 0x1F, 0x80, 0xBF
        elif 0xE0 <= b <= 0xEF:
            need, cp, lower, upper = 2, b & 0x0F, 0x80, 0xBF
            if b == 0xE0:
                lower = 0xA0
            if b == 0xED:
                upper = 0x9F
        elif 0xF0 <= b <= 0xF4:
            need, cp, lower, upper = 3, b & 0x07, 0x80, 0xBF
            if b == 0xF0:
                lower = 0x90
            if b == 0xF4:
                upper = 0x8F
        else:
            need = -1
        ok = need > 0
        seen = 0
        while ok and seen < need:
            if i >= n or not (lower <= data[i] <= upper):
                ok = False
                break
            cp = (cp << 6) | (data[i] & 0x3F)
            lower, upper = 0x80, 0xBF
            i += 1
            seen += 1
        if ok:
            if cp >= 0x10000:
                v = cp - 0x10000
                units.append(0xD800 + (v >> 10))
                units.append(0xDC00 + (v & 0x3FF))
            else:
                units.append(cp)
        else:
            if modus == STRICT:
                return 1, start, units
            units.append(0xFFFD)
    return 0, 0, units


def record_out(status, positio, units, order):
    out = bytearray([status])
    out += struct.pack('<HH', positio, len(units))
    fmt = '>H' if order == 'B' else '<H'
    for u in units:
        out += struct.pack(fmt, u)
    return bytes(out)


def frame(order, records):
    """probatio.exsc's standard input: the order byte, then one record each."""
    assert order in ('L', 'B')
    out = bytearray(order.encode('ascii'))
    for modus, data in records:
        assert 0 <= len(data) <= CAPACITY and modus in (STRICT, SUBSTITUE)
        out.append(modus)
        out += struct.pack('<H', len(data))
        out += data
    return bytes(out)


def expected(order, records, convert=py_convert):
    """probatio.exsc's standard output for those records."""
    return b''.join(record_out(*convert(data, modus), order)
                    for modus, data in records)


def audit_oracle(records):
    """Return the first (modus, data, py, ref) on which the two oracles
    disagree, or None."""
    for modus, data in records:
        a = py_convert(data, modus)
        b = ref_convert(data, modus)
        if a != b:
            return modus, data, a, b
    return None


if __name__ == '__main__':
    import sys
    sys.exit('oracle.py is a library: see matrix.py and corpus.py')
