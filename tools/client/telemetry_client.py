from dataclasses import dataclass, fields
from typing import List
import time
import socket
from threading import Thread, Event
from enum import IntEnum
import struct
from queue import Queue

from log_types import log_block_control_loop_t, log_type_t


LOG_TYPE_PID = 0
log_id = 0


@dataclass
class LogBlockHeader:
    type: log_type_t # log_type_t
    timestamp: int # uint32_t
    id: int # uint64_t

    fmt = '<BII'
    size = struct.calcsize(fmt)

@dataclass
class LogBlock:
    header: LogBlockHeader
    data: any

    def to_bytes(self) -> bytes:
        ''' Returns a log block as bytes. '''
        values = [self.header.type, int(self.header.timestamp), self.header.id]
        for field in fields(self.data):
            values.append(getattr(self.data, field.name))

        fmt = self.header.fmt + self.data.fmt.replace('<', '')
        return struct.pack(fmt, *values)


def gen_random_log_blocks() -> List[LogBlock]:
    global log_id
    blocks = []
    header  = LogBlockHeader(LOG_TYPE_PID, time.time(), log_id)
    data = log_block_control_loop_t()
    block = LogBlock(header, data)
    log_id += 1
    blocks.append(block)

    return blocks



class TelemetryClient:
    '''
    Client that connects to the telemetry node and read data from it.
    Once new data is received, it is parsed and log blocks python objects
    are assembled. Once these are assembled, they are put in a rx queue, which
    can be taken from by the HTTP server.
    '''

    class ParseState(IntEnum):
        HEADER = 0
        DATA = 1

    def __init__(self, ip: str, port: int) -> None:
        self.sock: socket.socket = None
        self.ip = ip
        self.port = port
        self._stop_flag = Event()
        self._rx = Queue()
        self._parse_state = self.ParseState.HEADER
        self.connect_retry_delay_s = 5

    def start(self) -> None:
        '''
        Starts the telemetry client. This functions starts a new background
        thread. To wait for this thread to complete, call wait_for_complete()
        '''
        self._stop_flag.clear()
        Thread(target=self._run, daemon=True).start()

    def stop(self) -> None:
        self._stop_flag.set()

    def wait_for_complete(self) -> None:
        ''' Wait until the telemetry client thread ends. '''
        self._stop_flag.wait()

    def get_log_blocks(self) -> List[LogBlock]:
        ''' Returns all logblocks available in the rx queue. '''
        log_blocks = []
        while not self._rx.empty():
            log_blocks.append(self._rx.get())
        return log_blocks

    def _run(self) -> None:
        print('Telemetry client thread started')
        while not self._stop_flag.is_set():
            if self.sock is None:
                if not self._connect():
                    time.sleep(self.connect_retry_delay_s)
                    continue

            # Parse log header
            header_raw = self.sock.recv(LogBlockHeader.size)
            header = LogBlockHeader(*struct.unpack(LogBlockHeader.fmt, header_raw))
            print(f'New log block received: {header}')

            log_block: LogBlock

            if header.type == log_type_t.LOG_TYPE_PID:
                data_raw = self.sock.recv(log_block_control_loop_t.size)
                data = log_block_control_loop_t(
                    *struct.unpack(log_block_control_loop_t.fmt, data_raw))
                log_block = LogBlock(header, data)

                print(f'gyro_x: {data.raw_gyro_x}')
            else:
                print(f'No support for log types {header.type} yet!')
                continue

            self._rx.put(log_block)
            print(f'Queue size: {len(self._rx.queue)}')

        print('Telem client thread ended')

    def _connect(self) -> None:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((self.ip, self.port))
            self.sock = sock
            print(f'Connected to telemetry node at: {self.ip}:{self.port}')
            return True
        except OSError as e:
            print(f'Failed to connect to {self.ip}:{self.port}: {e}')
            return False
