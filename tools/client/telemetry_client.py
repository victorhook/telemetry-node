from dataclasses import dataclass, fields
from typing import List
import time
import socket
from threading import Thread, Event
from enum import IntEnum
import struct
from queue import Queue
import os


from log_types import log_block_data_control_loop_t, log_block_header_t, log_type_t
from telemetry_client_logger import TelemetryClientLogger

LOG_TYPE_PID = 0
log_id = 0



def gen_random_log_block() -> List[log_type_t]:
    return log_block_data_control_loop_t(LOG_TYPE_PID, int(time.time()), log_id)


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
        self.logger = TelemetryClientLogger()

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

    def get_log_blocks(self) -> List[log_type_t]:
        ''' Returns all logblocks available in the rx queue. '''
        log_blocks = []
        while not self._rx.empty():
            log_blocks.append(self._rx.get())
        return log_blocks

    def _run(self) -> None:
        print('Telemetry client thread started')
        #self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        #self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        #self.sock.bind(('192.168.4.2', 1234))

        i = 0
        t0 = time.time()

        while not self._stop_flag.is_set():
            if self.sock is None:
                if not self._connect():
                    time.sleep(self.connect_retry_delay_s)
                    continue

            # Parse log header
            try:
                header_raw = self.sock.recv(log_block_header_t.size)
                header_args = struct.unpack(log_block_header_t.fmt, header_raw)
                header = log_block_header_t(*header_args)
                #print(f'New log block received: {header}')

                log_block: log_type_t

                if header.type == log_type_t.LOG_TYPE_PID:
                    data_raw = self.sock.recv(log_block_data_control_loop_t.size)
                    data_args = struct.unpack(log_block_data_control_loop_t.fmt, data_raw)
                    log_block = log_block_data_control_loop_t(*(header_args + data_args))
                else:
                    print(f'No support for log types {header.type} yet!')
                    continue

                self.logger.log(log_block)
                self._rx.put(log_block)

                i += log_block_header_t.size + log_block_data_control_loop_t.size

                if (time.time() - t0) >= 1:
                    t0 = time.time()
                    print(f'RX: {i / 1000} kB')
                    i = 0
                #print(f'Queue size: {len(self._rx.queue)}')

            except struct.error:
                print('err')

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



if __name__ == '__main__':
    # Ensure that we are on the telemetry nodes network
    target_ssid = 'telemetrynode'
    ssid_name = os.popen('iwgetid -r').read().strip()
    if ssid_name != target_ssid:
        print(f'Not connected to target SSID "{target_ssid}", connecting...')
        os.system('nmcli con up telemetrynode')

    #telem_client = TelemetryClient('192.168.10.204', 80)
    telem_client = TelemetryClient('192.168.4.1', 80)
    telem_client.start()
    telem_client.wait_for_complete()
