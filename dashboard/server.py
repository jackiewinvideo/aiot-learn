import argparse
import json
import re
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import urlopen


PROJECT_DIR = Path(__file__).resolve().parent.parent
DASHBOARD_DIR = Path(__file__).resolve().parent


def load_dotenv(path):
    values = {}
    if not path.is_file():
        raise RuntimeError("Missing .env file in the project root.")

    for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError("Invalid .env line: expected KEY=value.")

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        if key in values:
            raise ValueError("Duplicate .env key: " + key)
        values[key] = value
    return values


ENV_VALUES = load_dotenv(PROJECT_DIR / ".env")
CHANNEL_ID = ENV_VALUES.get("THINGSPEAK_CHANNEL_ID", "")
READ_API_KEY = ENV_VALUES.get("THINGSPEAK_READ_API_KEY", "")
if not re.fullmatch(r"[0-9]+", CHANNEL_ID):
    raise ValueError("THINGSPEAK_CHANNEL_ID must be set to a numeric channel ID in .env.")
if READ_API_KEY and not re.fullmatch(r"[A-Za-z0-9]+", READ_API_KEY):
    raise ValueError("THINGSPEAK_READ_API_KEY must contain only ASCII letters and digits.")


class DashboardHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DASHBOARD_DIR), **kwargs)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/feeds":
            self.handle_feeds()
            return
        if path == "/":
            self.path = "/index.html"
        elif path != "/index.html":
            self.send_error(404)
            return
        super().do_GET()

    def handle_feeds(self):
        query = parse_qs(urlsplit(self.path).query)
        try:
            results = int(query.get("results", ["24"])[0])
        except ValueError:
            self.send_json(400, {"error": "results must be a number."})
            return
        results = max(1, min(results, 100))

        params = {"results": results}
        if READ_API_KEY:
            params["api_key"] = READ_API_KEY
        url = (
            "https://api.thingspeak.com/channels/"
            + CHANNEL_ID
            + "/feeds.json?"
            + urlencode(params)
        )
        try:
            with urlopen(url, timeout=10) as response:
                body = response.read()
            data = json.loads(body)
        except HTTPError as error:
            self.send_json(502, {"error": "ThingSpeak returned HTTP " + str(error.code) + "."})
            return
        except (URLError, TimeoutError):
            self.send_json(502, {"error": "Could not connect to ThingSpeak. Check the network connection."})
            return
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_json(502, {"error": "ThingSpeak returned an invalid response."})
            return

        self.send_json(200, data)

    def send_json(self, status, data):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run the ThingSpeak dashboard.")
    parser.add_argument("--host", default="127.0.0.1", help="IP address to listen on (default: 127.0.0.1)")
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, 8000), DashboardHandler)
    print("ThingSpeak dashboard: http://{}:8000/".format(args.host), flush=True)
    server.serve_forever()
