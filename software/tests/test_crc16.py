from tensometer.wire import crc16


def test_check_value():
    # Standard CRC-16/CCITT-FALSE check value.
    assert crc16.compute(b"123456789") == 0x29B1


def test_empty_input_is_init():
    assert crc16.compute(b"") == crc16.INIT


def test_update_is_incremental():
    whole = crc16.compute(b"123456789")
    split = crc16.update(crc16.update(crc16.INIT, b"1234"), b"56789")
    assert whole == split


def test_single_bit_difference_changes_crc():
    a = crc16.compute(b"hello world")
    b = crc16.compute(b"hello worle")  # last byte differs by one bit
    assert a != b
