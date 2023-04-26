from serial import Serial
import sys
from threading import Thread, Event
from dataclasses import dataclass, field
import struct
from enum import IntEnum
import time
from pathlib import Path


class VSTP_Cmd(IntEnum):
    LOG_START = 0
    LOG_STOP = 1
    LOG_DATA = 2
    LOG_SD_START = 3
    LOG_SD_STOP = 4

@dataclass
class VSTP_Packet:
    cmd: int
    len: int = field(init=False)
    crc: int = field(init=False)
    buf: bytes = field(default=b'')

    def __post_init__(self) -> None:
        # Set len
        self.len = len(self.buf)
        # Calculate CRC
        crc = self.cmd ^ self.len
        for byte in self.buf:
            crc ^= byte
        self.crc = crc

    def to_bytes(self) -> bytes:
        return struct.pack('BBB', self.cmd, self.len, self.crc) + self.buf


def bytes_to_hex_string(data) -> str:
    return ' '.join(hex(b)[2:].zfill(2) for b in data)


LOG_DEFAULT_PATH = str(Path(__file__).absolute().parent.joinpath('out.log'))


class FcMock(Serial):

    def __init__(self, port: str, log_path: str = LOG_DEFAULT_PATH, baudrate=115200) -> None:
        self._read_thread_stop = Event()
        self.log = open(log_path, 'w')
        super().__init__(port, baudrate=baudrate)
        print(f'Started logging to: {log_path}')

    def close(self) -> None:
        self._read_thread_stop.set()
        super().close()

    def open(self) -> None:
        super().open()
        print(f'Connected to serial port: {self.port}')
        self._read_thread_stop.clear()
        Thread(target=self._read_thread, daemon=True).start()

    def _read_thread(self) -> None:
        self.timeout = 1
        print('Read thread started')
        while not self._read_thread_stop.is_set():
            try:
                data = self.read(1)
                if data:
                    try:
                        out = data.decode('ascii')
                    except UnicodeDecodeError:
                        out = str(data)
                    #out = hex(ord(data))[2:].zfill(2) + ' '
                    self.log.write(out)
                    self.log.flush()
            except TypeError:
                # Occurs if we try to read after disconnected
                pass
        print('Read thread ended')

    def write_full_log_buff(self) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_DATA, b'hello world'))

    def write_custom(self, data: bytes) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_DATA, data))

    def write_many(self, nbr: int, delay_between_pkts: float) -> None:
        print(f'Sending {nbr} packets')
        for i in range(nbr):
            self.write_full_log_buff()
            time.sleep(delay_between_pkts)

    def stream_start(self) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_START))

    def stream_stop(self) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_STOP))

    def stream_sd_start(self) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_SD_START))

    def stream_sd_stop(self) -> None:
        self._send(VSTP_Packet(VSTP_Cmd.LOG_SD_STOP))

    def _send(self, packet: VSTP_Packet) -> None:
        print(f'TX: {packet}')
        self.write(packet.to_bytes())

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Must give serial port name!')
        sys.exit(0)

    port = sys.argv[1]
    mock = FcMock(port)