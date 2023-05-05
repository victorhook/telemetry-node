from flask import Flask, jsonify
from flask_cors import CORS

from telemetry_client import TelemetryClient, LogBlock, gen_random_log_blocks


app = Flask(__name__)
CORS(app)


telem_client = TelemetryClient('192.168.10.207', 80)
telem_client.start()


API_BASE = '/api'

class API:

    @app.route(API_BASE + '/get_log_blocks')
    def get_log_blocks() -> None:
        return jsonify(gen_random_log_blocks())


@app.route("/")
def index():
  return "Hello, cross-origin-world!"