import pytest

from tensometer.wire import cobs, frame


def test_encode_decode_round_trip():
    wire = frame.encode(msg_id=0x10, seq=7, body=b"hello")
    assert wire.endswith(b"\x00")
    decoded = frame.decode(wire[:-1])  # decode() takes the frame without the delimiter
    assert decoded.msg_id == 0x10
    assert decoded.seq == 7
    assert decoded.body == b"hello"


def test_empty_body_round_trips():
    wire = frame.encode(msg_id=0x21, seq=0, body=b"")
    decoded = frame.decode(wire[:-1])
    assert decoded.body == b""


def test_body_too_long_rejected():
    with pytest.raises(frame.BodyTooLongError):
        frame.encode(msg_id=0x01, seq=0, body=bytes(frame.MAX_BODY_LEN + 1))


def test_decode_rejects_bad_crc():
    # Construct a payload with a deliberately wrong CRC directly, rather than flipping a
    # bit in an already-COBS-encoded frame (which could instead corrupt COBS framing
    # itself and raise a cobs-layer error instead of exercising the CRC check).
    payload = bytearray((0x01, 1))  # msg_id, seq -- neither byte is zero
    payload.extend(b"abc")  # body -- no zero bytes
    wrong_crc = 0x1234
    payload.append(wrong_crc & 0xFF)
    payload.append((wrong_crc >> 8) & 0xFF)
    wire = cobs.encode(bytes(payload))
    with pytest.raises(frame.BadCrcError):
        frame.decode(wire)


def test_decode_rejects_too_short():
    with pytest.raises(frame.TooShortError):
        # A single-byte COBS payload decodes to zero bytes -- shorter than header+crc.
        frame.decode(cobs.encode(b""))


def test_reader_accumulates_and_yields_one_frame():
    r = frame.Reader()
    wire = frame.encode(msg_id=0x10, seq=1, body=b"xyz")
    result = None
    for b in wire:
        result = r.push(b)
    assert result is not None
    decoded = frame.decode(result)
    assert decoded.msg_id == 0x10
    assert decoded.body == b"xyz"


def test_reader_yields_nothing_mid_frame():
    r = frame.Reader()
    wire = frame.encode(msg_id=0x10, seq=1, body=b"xyz")
    for b in wire[:-1]:  # everything except the trailing delimiter
        assert r.push(b) is None


def test_reader_ignores_empty_frame_double_delimiter():
    r = frame.Reader()
    assert r.push(0x00) is None  # delimiter with nothing accumulated yet


def test_reader_discards_overlong_frame_and_counts_it():
    r = frame.Reader()  # default cap -- the recovery frame below must still fit in it
    junk = [b % 255 + 1 for b in range(frame.MAX_WIRE_LEN + 10)]  # no zeros, no delimiter
    for b in junk:
        r.push(b)
    assert r.overflow_count == 1
    # The reader stays in "discarding" mode until a delimiter is seen -- it swallows
    # everything up to and including the next 0x00, same as a real noisy byte stream
    # would need to resync on the next frame boundary (frame::Reader's own contract).
    assert r.push(0x00) is None
    # Now it recovers cleanly and resumes normal operation.
    wire = frame.encode(msg_id=0x02, seq=9, body=b"ok")
    result = None
    for b in wire:
        result = r.push(b)
    assert result is not None
    assert frame.decode(result).body == b"ok"


def test_reader_two_frames_back_to_back():
    r = frame.Reader()
    wire_a = frame.encode(msg_id=0x01, seq=1, body=b"a")
    wire_b = frame.encode(msg_id=0x02, seq=2, body=b"bb")
    results = []
    for b in wire_a + wire_b:
        res = r.push(b)
        if res is not None:
            results.append(res)
    assert len(results) == 2
    assert frame.decode(results[0]).body == b"a"
    assert frame.decode(results[1]).body == b"bb"
