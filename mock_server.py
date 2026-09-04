import http.server
import socketserver
import json
import urllib.parse
import time
import random
import os
import sys

DEFAULT_PORT = 8080
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
INDEX_FILE = os.path.join(BASE_DIR, "index.html")

state = {
    "voltage": 230.2,
    "current": 1.25,
    "power": 287.75,
    "energy": 1.450,
    "balance": 250.00,
    "relayState": True,
    "faultDetected": False,
    "theftDetected": False,
    "overVoltage": 260.0,
    "overCurrent": 10.0,
    "theftCurrent": 0.02,
    "minBalance": 0.0,
    "costPerKWh": 0.20
}

class DashboardHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

    def handle_telemetry_update(self, query):
        if "voltage" in query:
            try: state["voltage"] = float(query["voltage"][0])
            except: pass
        if "current" in query:
            try: state["current"] = float(query["current"][0])
            except: pass
        if "power" in query:
            try: state["power"] = float(query["power"][0])
            except: pass
        if "energy" in query:
            try: state["energy"] = float(query["energy"][0])
            except: pass
        if "faultDetected" in query:
            state["faultDetected"] = (int(query["faultDetected"][0]) == 1)
        if "theftDetected" in query:
            state["theftDetected"] = (int(query["theftDetected"][0]) == 1)

        # Return latest cloud balance and target relayState back to ESP32
        return {
            "success": True,
            "balance": round(state["balance"], 2),
            "relayState": state["relayState"],
            "costPerKWh": state["costPerKWh"],
            "overVoltage": state["overVoltage"],
            "overCurrent": state["overCurrent"]
        }

    def do_GET(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        query = urllib.parse.parse_qs(parsed_url.query)

        if path == "/data":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(state).encode("utf-8"))
        elif path in ["/update", "/updateTelemetry"]:
            resp = self.handle_telemetry_update(query)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))
        elif path in ["/", "/index.html"]:
            if os.path.exists(INDEX_FILE):
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                with open(INDEX_FILE, "rb") as f:
                    self.wfile.write(f.read())
            else:
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b"index.html not found.")
        else:
            self.directory = BASE_DIR
            super().do_GET()

    def do_POST(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        query = urllib.parse.parse_qs(parsed_url.query)

        content_length = int(self.headers.get('Content-Length', 0))
        if content_length > 0:
            body = self.rfile.read(content_length).decode('utf-8', errors='ignore')
            try:
                json_data = json.loads(body)
                for k, v in json_data.items():
                    query[k] = [str(v)]
            except Exception:
                body_query = urllib.parse.parse_qs(body)
                for k, v in body_query.items():
                    query[k] = v

        if path == "/recharge":
            amount = float(query.get("amount", [100])[0])
            state["balance"] = round(state["balance"] + amount, 2)
            if state["balance"] > state["minBalance"]:
                state["relayState"] = True
            response = {"success": True, "balance": state["balance"], "message": f"Recharge of Rs. {amount:.2f} processed"}
        elif path in ["/update", "/updateTelemetry"]:
            response = self.handle_telemetry_update(query)
        elif path == "/setRelay":
            relay_val = int(query.get("state", [1])[0])
            state["relayState"] = (relay_val == 1)
            response = {"success": True, "relayState": state["relayState"]}
        elif path == "/setThresholds":
            if "overVoltage" in query: state["overVoltage"] = float(query["overVoltage"][0])
            if "overCurrent" in query: state["overCurrent"] = float(query["overCurrent"][0])
            if "theftCurrent" in query: state["theftCurrent"] = float(query["theftCurrent"][0])
            if "minBalance" in query: state["minBalance"] = float(query["minBalance"][0])
            if "costPerKWh" in query: state["costPerKWh"] = float(query["costPerKWh"][0])
            response = {"success": True, "message": "Thresholds saved"}
        elif path == "/factoryReset":
            state["balance"] = 0.0
            state["energy"] = 0.0
            state["overVoltage"] = 260.0
            state["overCurrent"] = 10.0
            state["theftCurrent"] = 0.02
            state["minBalance"] = 0.0
            state["costPerKWh"] = 0.20
            state["relayState"] = False
            response = {"success": True, "message": "Factory reset complete"}
        else:
            response = {"success": False, "message": f"Endpoint {path} not found"}

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response).encode("utf-8"))

class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

def start_server(port=DEFAULT_PORT):
    for p in range(port, port + 10):
        try:
            httpd = ThreadingHTTPServer(("", p), DashboardHandler)
            print("=" * 65, flush=True)
            print("  MRIDA ENERGY SOLUTIONS - Smart Prepaid Meter Web Dashboard", flush=True)
            print(f"  Server running at: http://0.0.0.0:{p}", flush=True)
            print("=" * 65, flush=True)
            return httpd
        except OSError as e:
            if p == port + 9:
                print(f"Error binding port: {e}", flush=True)
                sys.exit(1)

if __name__ == "__main__":
    port_to_use = int(os.environ.get("PORT", DEFAULT_PORT))
    if len(sys.argv) > 1:
        try: port_to_use = int(sys.argv[1])
        except ValueError: pass

    server = start_server(port_to_use)
    if server:
        try: server.serve_forever()
        except KeyboardInterrupt:
            server.server_close()
