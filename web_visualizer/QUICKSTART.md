# Quick Start Guide - Web Visualizer

## For Users Without Technical Background

### What is this?
This is a tool that helps you see where a UWB tag is located based on distance measurements from multiple anchors. Think of it like GPS, but for indoor spaces.

### How to use (3 easy steps):

1. **Open the visualizer**
   - Find the file `index.html` in the `web_visualizer` folder
   - Double-click it to open in your web browser
   - Any modern browser works (Chrome, Firefox, Edge, Safari)

2. **Enter your data**
   - Look at the "Manual Data Input" section on the left
   - Copy and paste your distance measurements
   - The format should look like this:
     ```
     [TAG] A0 = 2.35 m
     [TAG] A1 = 3.42 m
     [TAG] A2 = 1.89 m
     ```

3. **See the results**
   - Click "Parse & Update"
   - The visualization will show:
     - Green/Blue/Orange dots = Your anchors
     - Red dot = The tag's calculated position
     - Dotted circles = Distance from each anchor

### Tips:
- You need at least 3 anchors (A0, A1, A2) to calculate a position
- The coordinates shown are in meters
- You can adjust the zoom using "Grid Scale" setting
- Click "Add Anchor" to add more anchor points

### Live Data (Advanced)
If your ESP32 device supports WebSocket:
1. Enter the device's IP address (e.g., `ws://192.168.4.1:81`)
2. Click "Connect"
3. Data will update automatically!

### Troubleshooting:
- **"No position data"** - Make sure you have entered distances for at least 3 anchors
- **Position looks wrong** - Check that your anchor positions (coordinates) are correct
- **Can't connect via WebSocket** - Make sure your device and computer are on the same network

### Example Data:
Copy this into the input box and click "Parse & Update" to see it work:
```
[TAG] A0 = 2.35 m
[TAG] A1 = 3.42 m
[TAG] A2 = 1.89 m
```

---

## For Developers

### Data Format
The visualizer accepts serial output lines in the format:
```
[TAG] A{anchor_id} = {distance} m
```

Optional timestamp prefix is supported:
```
13:38:49.068 -> [TAG] A0 = 2.35 m
```

### WebSocket Integration
Send newline-delimited messages in the same format:
```javascript
ws.send("[TAG] A0 = 2.35 m\n");
```

### Customizing Anchors
Edit the `state.anchors` array in the JavaScript section:
```javascript
state.anchors = [
    { id: 'A0', x: 0, y: 0, distance: null, color: '#4caf50' },
    { id: 'A1', x: 3, y: 0, distance: null, color: '#2196f3' },
    { id: 'A2', x: 1.5, y: 2.6, distance: null, color: '#ff9800' }
];
```

### API / Integration
The visualizer exposes these functions:
- `handleSerialData(line)` - Process a single line of data
- `updateTagPosition()` - Recalculate position based on current distances
- `trilaterate(anchors, distances)` - Core trilateration algorithm

### Browser Requirements
- HTML5 Canvas support
- ES6 JavaScript
- WebSocket API (for live connection)
- Tested on: Chrome 90+, Firefox 88+, Edge 90+, Safari 14+
