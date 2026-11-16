import pytest

from tensometer.wire import cobs


def test_round_trip_no_zeros():
    data = bytes(range(1, 250))
    assert cobs.decode(cobs.encode(data)) == data


def test_round_trip_with_zeros():
    data = bytes([1, 2, 0, 3, 0, 0, 4, 5])
    assert cobs.decode(cobs.encode(data)) == data


def test_empty_input_encodes_to_single_code_byte():
    assert cobs.encode(b"") == bytes([0x01])


def test_encoded_output_never_contains_zero():
    data = bytes([0, 0, 0, 1, 2, 3, 0])
    encoded = cobs.encode(data)
    assert 0 not in encoded


def test_block_of_254_non_zero_bytes_splits_correctly():
    data = bytes([i % 255 + 1 for i in range(254)])  # 254 bytes, none zero
    encoded = cobs.encode(data)
    assert cobs.decode(encoded) == data


def test_decode_rejects_zero_byte_in_input():
    with pytest.raises(cobs.ZeroInInputError):
        cobs.decode(bytes([0x02, 0x00]))


def test_decode_rejects_truncated_code():
    # Code byte 0x05 promises 4 more data bytes; only 2 follow.
    with pytest.raises(cobs.TruncatedError):
        cobs.decode(bytes([0x05, 0x01, 0x02]))


def test_decode_rejects_empty_input():
    with pytest.raises(cobs.CobsError):
        cobs.decode(b"")


def test_max_encoded_len_matches_worst_case():
    data = bytes([1] * 1000)  # no zeros: worst case for overhead
    assert len(cobs.encode(data)) <= cobs.max_encoded_len(len(data))
