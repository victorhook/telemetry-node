from log_types import log_block_data_control_loop_t, log_block_header_t, log_type_t


DESIRED_LOG_PARAMS = [
    'raw_gyro_x',
    'raw_gyro_y',
    'raw_gyro_z',
]


class TelemetryClientLogger:

    def log(self, log_block: log_block_data_control_loop_t) -> None:
        print(f'[{log_block.id}] ', end='')
        for param in DESIRED_LOG_PARAMS:
            print(f'{param}: {getattr(log_block, param)}', end=' ')
        print()
