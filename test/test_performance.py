import sys
import time
from pathlib import Path

sys.path.append(str(Path(__file__).absolute().parent.parent))

from tools.fc_mock import FcMock


if __name__ == '__main__':
    port = '/dev/ttyUSB2'
    mock = FcMock(port, 'out.log')

    i = 0
    end = 1000
    mock.stream_start()
    for i in range(end):
        print(f'\r{i+1}/{end} ', end='')
        msg = 'As you can see, this payload contains: {} data! '
        msg += f'This is actually somewhat bigger payload with number: {i+1}\n'
        msg.format(len(msg))
        mock.write_custom(msg.encode('utf-8'))
        #time.sleep(.005)

    mock.stream_stop()
