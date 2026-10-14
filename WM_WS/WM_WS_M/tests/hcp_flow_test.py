"""Run with: python3 hcp_flow_test.py /path/to/WM_WS_M (local TCP required)."""
import socket
import subprocess
import sys
import tempfile
import time

binary = sys.argv[1]
def exact(connection, count):
    result = b''
    while len(result) < count:
        part = connection.recv(count - len(result))
        assert part, 'Unexpected disconnect'
        result += part
    return result

def identify(server):
    connection, _ = server.accept()
    connection.settimeout(3)
    assert exact(connection, len(b'ID test-station\n')) == b'ID test-station\n'
    return connection

for option in ['--hcp-timeout-ms', '--hcp-retry-ms']:
    result = subprocess.run([binary, '--ws-id', 'test-station', '--engine-ip',
                             '127.0.0.1', option, '0'], capture_output=True)
    assert result.returncode == 1

with socket.socket() as server, tempfile.TemporaryFile(mode='w+') as output:
    server.bind(('127.0.0.1', 0))
    server.settimeout(4)
    command = [binary, '--ws-id', 'test-station', '--engine-ip', '127.0.0.1',
               '--engine-port', str(server.getsockname()[1]),
               '--hcp-timeout-ms', '200', '--hcp-retry-ms', '50']
    process = subprocess.Popen(command, stdout=output, stderr=output)
    try:
        time.sleep(.15)  # Initial connection refusal must not terminate Monitor.
        server.listen()
        # No acknowledgement: bounded handshake followed by a fresh connection.
        with identify(server) as connection:
            start = time.monotonic()
            assert connection.recv(1) == b''
            assert time.monotonic() - start < 1.5
        with identify(server) as connection:
            connection.sendall(b'O')
            time.sleep(.03)
            connection.sendall(b'K')
            # KO continues on the same connection; recovery requires health OK.
            for status in [b'KO', b'KO', b'OK']:
                assert exact(connection, 2) == b'HC'
                connection.sendall(status[:1])
                time.sleep(.03)
                connection.sendall(status[1:])
            assert exact(connection, 2) == b'HC'
            # A partial reply must not reset the whole-response deadline.
            start = time.monotonic()
            time.sleep(.12)
            connection.sendall(b'O')
            assert connection.recv(1) == b''
            assert time.monotonic() - start < .30
        with identify(server) as connection:
            connection.sendall(b'OK')
            assert exact(connection, 2) == b'HC'
            connection.sendall(b'XX')
            assert connection.recv(1) == b''
        # Disconnect during identification, then confirm retry again.
        with identify(server):
            pass
        with identify(server):
            pass
        time.sleep(.1)
        assert process.poll() is None
    finally:
        process.terminate()
        process.wait(timeout=3)
    output.seek(0)
    log = output.read()
    assert 'connect:' in log, log
    assert 'identification response: HCP operation timed out' in log, log
    assert 'health response: HCP operation timed out' in log, log
    assert 'invalid HCP health response' in log, log
    assert log.count('state=healthy') == 1, log
    assert log.count('state=fault') == 2, log
print('HCP flow tests passed: deadlines, partial reads, KO/recovery, invalid frames, EOF, retries')
