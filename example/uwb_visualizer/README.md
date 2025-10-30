UWB Tag visualizer

This tool reads distance reports from the tag (serial output) and visualizes the estimated 2D position using trilateration.

## Available Versions

### Web Version (Recommended)
Modern web-based interface accessible via browser with real-time updates.

### Desktop Version
Traditional matplotlib-based desktop application.

## Features
- Read live from a serial port (default baud 115200) or from a log file
- Uses anchor coordinates from `anchors.json` (editable)
- Displays anchors, walking path, and current tag location
- Interactive floorplan support

## Quick start

### Web Version

1. Create a virtualenv and install dependencies:

   ```
   python -m venv .venv
   .venv\Scripts\pip.exe install -r requirements.txt
   ```

2. Edit `anchors.json` with the real coordinates of your anchors.

3. Run the web visualizer:

   ```
   # live from serial
   .venv\Scripts\python.exe web_visualizer.py --port COM3 --baud 115200

   # from a saved log file
   .venv\Scripts\python.exe web_visualizer.py --logfile my_serial_log.txt
   ```

4. Open http://localhost:5000 in your web browser

### Desktop Version

1. Create a virtualenv and install dependencies (same as above)

2. Edit `anchors.json` with the real coordinates of your anchors.

3. Run the desktop visualizer:

   ```
   # live from serial
   .venv\Scripts\python.exe visualizer.py --port COM3 --baud 115200

   # from a saved log file
   .venv\Scripts\python.exe visualizer.py --logfile my_serial_log.txt
   ```

Notes
- The program expects lines like: 13:38:49.068 -> [TAG] A2 = 3.28 m
- If you want better accuracy, tune anchor positions in `anchors.json`.
- This script performs a simple least-squares trilateration. For production use you may want to add filtering (Kalman) and more robust outlier rejection.

## Interactive floorplan editor (Desktop Version)

- To place anchors visually on a floorplan and save coordinates, run in edit mode:

   ```
   .venv\Scripts\python.exe visualizer.py --edit --floorplan path\to\floorplan.png --anchors anchors.json
   ```

- Workflow:
   - The editor will ask you to calibrate the image by clicking two known points on the floorplan and entering their real-world coordinates (meters). This computes the pixel->world transform.
   - After calibration, left-click to add anchors. You'll be prompted for anchor label and X/Y coordinates (defaults are the clicked location in meters).
   - Right-click an anchor to delete it.
   - When you close the window anchors (and the computed image transform) are saved to `anchors.json`.

This calibration step ensures that the floorplan image and anchor coordinates share the same coordinate system (meters) so the live visualization overlays correctly.

## Web Version Features

The web version provides:
- Modern, responsive user interface
- Real-time WebSocket updates for low latency
- Visual display of current position, path history, and anchors
- Live distance measurements from all anchors
- Automatic floorplan overlay if configured
- No need to close the window - runs continuously in background
- Access from any device on the same network
