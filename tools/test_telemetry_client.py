import sys
from pathlib import Path

sys.path.append(str(Path(__file__).absolute().parent.joinpath('client')))

from client.telemetry_client import TelemetryClient


if __name__ == '__main__':
    client = TelemetryClient('192.168.10.207', 80)
    client.start()
    #3client.wait_for_complete()