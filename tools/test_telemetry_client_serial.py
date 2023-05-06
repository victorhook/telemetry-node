import sys

from fc_mock import FcMock
from client.telemetry_client import gen_random_log_blocks

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Must give serial port name!')
        sys.exit(0)

    port = sys.argv[1]
    mock = FcMock(port)

    log_blocks = gen_random_log_blocks()

    def write():
        for i in log_blocks[:1]:
            mock.write_custom(i.to_bytes())