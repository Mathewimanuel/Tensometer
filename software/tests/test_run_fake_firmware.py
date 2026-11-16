import socket

from tensometer.sim.run_fake_firmware import SocketPort


def test_socket_port_round_trips_bytes():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    host, port = server.getsockname()

    client_raw = socket.create_connection((host, port), timeout=1.0)
    server_raw, _addr = server.accept()

    client = SocketPort(client_raw)
    server_port = SocketPort(server_raw)
    try:
        client.write(b"hello")
        received = b""
        for _ in range(20):  # a few short-timeout reads, in case it arrives in parts
            received += server_port.read(256)
            if received:
                break
        assert received == b"hello"
    finally:
        client.close()
        server_port.close()
        server.close()


def test_socket_port_read_returns_empty_on_timeout_not_blocking_forever():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    host, port = server.getsockname()
    client_raw = socket.create_connection((host, port), timeout=1.0)
    server_raw, _addr = server.accept()

    port_obj = SocketPort(server_raw, timeout=0.05)
    try:
        assert port_obj.read(256) == b""  # nothing sent -- must not hang
    finally:
        client_raw.close()
        server_raw.close()
        server.close()
