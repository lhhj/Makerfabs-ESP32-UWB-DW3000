#!/usr/bin/env python3
"""
UWB tag visualizer - Web version
Provides a web interface for real-time UWB tag position visualization.

Usage:
  python web_visualizer.py --port COM3 --baud 115200
  python web_visualizer.py --logfile serial.txt
  
Then open http://localhost:5000 in your web browser.
"""
import argparse
import json
import re
import threading
import queue
import time
import os
from pathlib import Path

import numpy as np
from flask import Flask, render_template, jsonify, request

try:
    import serial
except Exception:
    serial = None

LINE_RE = re.compile(r"\[TAG\]\s*A(\d+)\s*=\s*([0-9]*\.?[0-9]+)\s*m")


def parse_line(line):
    m = LINE_RE.search(line)
    if not m:
        return None, None
    aid = 'A' + m.group(1)
    dist = float(m.group(2))
    return aid, dist


class ReaderThread(threading.Thread):
    def __init__(self, port=None, baud=115200, logfile=None, out_q=None):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.logfile = logfile
        self.out_q = out_q or queue.Queue()
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        if self.logfile:
            with open(self.logfile, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    if self._stop.is_set():
                        break
                    aid, d = parse_line(line)
                    if aid:
                        self.out_q.put((time.time(), aid, d))
                    time.sleep(0.01)
            return

        if serial is None:
            print('pyserial not installed. Cannot read from serial port.')
            return

        try:
            ser = serial.Serial(self.port, self.baud, timeout=1)
            print(f'Successfully opened serial port {self.port} at {self.baud} baud')
        except Exception as e:
            print('Failed to open serial port:', e)
            return

        while not self._stop.is_set():
            try:
                line = ser.readline().decode('utf-8', errors='ignore')
            except Exception as e:
                print(f'Error reading serial: {e}')
                continue
            if not line:
                continue
            print(f'Raw: {line.strip()}')
            aid, d = parse_line(line)
            if aid:
                print(f'Parsed: {aid} = {d} m')
                self.out_q.put((time.time(), aid, d))


def trilaterate(anchors, distances):
    """Compute 2D position given anchors dict {A1:[x,y],...} and distances dict {A1: r1,...}
    Requires at least 3 anchors. Returns (x,y)
    Uses linear least squares on difference of equations.
    """
    keys = [k for k in distances.keys() if k in anchors]
    if len(keys) < 3:
        raise ValueError('need at least 3 anchors')

    pts = np.array([anchors[k] for k in keys], dtype=float)  # Nx2
    rs = np.array([distances[k] for k in keys], dtype=float)  # N

    # Use first anchor as reference
    x1, y1 = pts[0]
    r1 = rs[0]
    A = []
    b = []
    for (xi, yi), ri in zip(pts[1:], rs[1:]):
        A.append([2*(xi - x1), 2*(yi - y1)])
        b.append(r1*r1 - ri*ri - x1*x1 + xi*xi - y1*y1 + yi*yi)
    A = np.array(A)
    b = np.array(b)
    # Solve A*[x;y] = b in least squares sense
    sol, *_ = np.linalg.lstsq(A, b, rcond=None)
    return float(sol[0]), float(sol[1])


# Global state
app = Flask(__name__)
app.config['SECRET_KEY'] = 'uwb-visualizer-secret'

data_queue = queue.Queue()
reader_thread = None
anchors = {}
latest_distances = {}
position_history = []
max_history = 500


def load_anchors(anchors_file):
    """Load anchors from JSON file"""
    global anchors
    if os.path.exists(anchors_file):
        with open(anchors_file, 'r') as f:
            anchors_raw = json.load(f)
        # Remove metadata
        if isinstance(anchors_raw, dict) and '_image_transform' in anchors_raw:
            image_transform = anchors_raw.get('_image_transform')
            anchors_raw.pop('_image_transform', None)
            anchors = {k: tuple(v) for k, v in anchors_raw.items()}
            return anchors, image_transform
        anchors = {k: tuple(v) for k, v in anchors_raw.items()}
        return anchors, None
    return {}, None


def data_processor():
    """Background thread to process incoming data and store for HTTP API access.
    
    Reads distance measurements from the queue, computes tag position via trilateration,
    and updates global state that is accessible via HTTP endpoints.
    """
    global latest_distances, position_history
    
    while True:
        try:
            tstamp, aid, d = data_queue.get(timeout=0.1)
            latest_distances[aid] = d
            
            # Try to compute position if we have enough anchors
            if len(latest_distances) >= 3 and len(anchors) >= 3:
                try:
                    pos = trilaterate(anchors, latest_distances)
                    position_history.append({'x': pos[0], 'y': pos[1], 'timestamp': time.time()})
                    if len(position_history) > max_history:
                        position_history = position_history[-max_history:]
                    print(f'Position: ({pos[0]:.2f}, {pos[1]:.2f}) m')
                except Exception as e:
                    print(f'Trilateration failed: {e}')
        except queue.Empty:
            continue
        except Exception as e:
            print(f'Error in data processor: {e}')


@app.route('/api/current_state')
def get_current_state():
    """Get current position and distances"""
    global latest_distances, position_history
    
    current_pos = None
    if len(position_history) > 0:
        current_pos = position_history[-1]
    
    return jsonify({
        'position': current_pos,
        'distances': dict(latest_distances)
    })


@app.route('/')
def index():
    """Serve the main visualization page"""
    return render_template('index.html')


@app.route('/api/anchors')
def get_anchors():
    """Get anchor positions"""
    return jsonify({
        'anchors': {k: list(v) for k, v in anchors.items()}
    })


@app.route('/api/config')
def get_config():
    """Get visualization configuration"""
    anchors_file = Path(__file__).parent / 'anchors.json'
    image_transform = None
    floorplan_available = False
    
    if os.path.exists(anchors_file):
        with open(anchors_file, 'r') as f:
            anchors_raw = json.load(f)
            if '_image_transform' in anchors_raw:
                image_transform = anchors_raw['_image_transform']
                floorplan_file = image_transform.get('image_file', 'floorplan.png')
                floorplan_path = Path(__file__).parent / floorplan_file
                floorplan_available = os.path.exists(floorplan_path)
    
    return jsonify({
        'anchors': {k: list(v) for k, v in anchors.items()},
        'image_transform': image_transform,
        'floorplan_available': floorplan_available,
        'max_history': max_history
    })


@app.route('/api/floorplan')
def get_floorplan():
    """Serve floorplan image"""
    from flask import send_file
    import os.path
    anchors_file = Path(__file__).parent / 'anchors.json'
    
    if os.path.exists(anchors_file):
        with open(anchors_file, 'r') as f:
            anchors_raw = json.load(f)
            if '_image_transform' in anchors_raw:
                image_transform = anchors_raw['_image_transform']
                floorplan_file = image_transform.get('image_file', 'floorplan.png')
                
                # Security: Prevent path traversal by ensuring file is in the same directory
                # and normalize the path
                floorplan_file = os.path.basename(floorplan_file)
                floorplan_path = Path(__file__).parent / floorplan_file
                
                # Ensure the resolved path is still within the application directory
                app_dir = Path(__file__).parent.resolve()
                try:
                    floorplan_resolved = floorplan_path.resolve()
                    if floorplan_resolved.parent == app_dir and floorplan_resolved.exists():
                        return send_file(floorplan_resolved, mimetype='image/png')
                except (OSError, ValueError):
                    pass
    
    # Return 404 if no floorplan
    return jsonify({'error': 'Floorplan not found'}), 404


@app.route('/api/history')
def get_history():
    """Get position history"""
    return jsonify({
        'history': position_history
    })


def main():
    global reader_thread, max_history
    
    p = argparse.ArgumentParser()
    p.add_argument('--port', help='Serial port (e.g. COM3 or /dev/ttyUSB0)')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--logfile', help='Read from a saved log file instead of serial')
    p.add_argument('--anchors', default='anchors.json', help='JSON file with anchor coordinates')
    p.add_argument('--history', type=int, default=500, help='Max points to keep in path')
    p.add_argument('--host', default='0.0.0.0', help='Web server host')
    p.add_argument('--web-port', type=int, default=5000, help='Web server port')
    args = p.parse_args()
    
    max_history = args.history
    
    # Load anchors
    anchors_data, image_transform = load_anchors(args.anchors)
    print(f'Loaded {len(anchors_data)} anchors')
    
    # Start reader thread
    reader_thread = ReaderThread(port=args.port, baud=args.baud, logfile=args.logfile, out_q=data_queue)
    reader_thread.start()
    
    # Start data processor thread
    processor = threading.Thread(target=data_processor, daemon=True)
    processor.start()
    
    print(f'\nUWB Visualizer Web Server starting...')
    print(f'Open http://localhost:{args.web_port} in your web browser')
    
    # Run Flask app
    app.run(host=args.host, port=args.web_port, debug=False, threaded=True)


if __name__ == '__main__':
    main()
