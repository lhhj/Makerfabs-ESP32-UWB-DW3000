# UWB Position Visualizer - Web-based Alternative

A standalone web-based visualizer for the Makerfabs ESP32 UWB DW3000 module. This tool provides a modern, browser-based alternative to the Python desktop visualizers, allowing you to visualize UWB anchor positions and tag locations in real-time.

![UWB Web Visualizer](../md_pic/web_visualizer_screenshot.png)

## Features

- **Real-time Visualization**: Display anchor positions, tag location, and movement path
- **WebSocket Support**: Connect to ESP32 devices via WebSocket for live data streaming
- **Manual Data Input**: Parse and visualize data from serial output logs
- **Trilateration**: Automatic 2D position calculation using distance measurements
- **Interactive Canvas**: 
  - Adjustable grid scale
  - Visual distance circles around anchors
  - Tag movement path tracking
- **No Installation Required**: Single HTML file - just open in any modern web browser
- **Responsive Design**: Works on desktop and tablet devices
- **Zero Dependencies**: No external libraries needed

## Quick Start

### Option 1: Direct Browser Usage (Easiest)

1. Simply open `index.html` in any modern web browser (Chrome, Firefox, Edge, Safari)
2. The visualizer will start with 3 default anchors at positions:
   - A0: (0, 0)
   - A1: (3, 0)
   - A2: (1.5, 2.6)

### Option 2: WebSocket Connection to ESP32

1. Open `index.html` in your browser
2. Configure your ESP32 to send WebSocket data in the format: `[TAG] A0 = 2.35 m`
3. Enter the WebSocket URL in the "WebSocket URL" field (e.g., `ws://192.168.4.1:81`)
4. Click "Connect"
5. The visualizer will automatically update as data arrives

### Option 3: Manual Data Input

1. Open `index.html` in your browser
2. Copy serial output from your UWB device (lines like `[TAG] A0 = 2.35 m`)
3. Paste into the "Manual Data Input" textarea
4. Click "Parse & Update"
5. The visualization will update with the new distance measurements

## Data Format

The visualizer accepts distance measurements in the following formats:

```
[TAG] A0 = 2.35 m
[TAG] A1 = 3.42 m
[TAG] A2 = 1.89 m
```

Or with timestamps:
```
13:38:49.068 -> [TAG] A0 = 2.35 m
13:38:49.069 -> [TAG] A1 = 3.42 m
13:38:49.070 -> [TAG] A2 = 1.89 m
```

## Configuration

### Adding Anchors

1. Click the "Add Anchor" button in the sidebar
2. Enter the anchor ID (e.g., `A3`)
3. Enter the X and Y coordinates in meters
4. The new anchor will appear on the canvas

### Adjusting Display Settings

- **Grid Scale**: Adjust the "Grid Scale (px/meter)" slider to zoom in/out
  - Default: 100 pixels per meter
  - Range: 20-200 pixels per meter

- **Reset View**: Click "Reset View" to recenter the coordinate system

- **Clear Path**: Click "Clear Path" to remove the tag's movement trail

### Anchor Configuration

Edit the default anchors by modifying the JavaScript in `index.html`:

```javascript
state.anchors = [
    { id: 'A0', x: 0, y: 0, distance: null, color: '#4caf50' },
    { id: 'A1', x: 3, y: 0, distance: null, color: '#2196f3' },
    { id: 'A2', x: 1.5, y: 2.6, distance: null, color: '#ff9800' }
];
```

## How Trilateration Works

The visualizer uses least-squares trilateration to calculate the tag's 2D position:

1. **Minimum Requirements**: At least 3 anchors with valid distance measurements
2. **Algorithm**: Solves a system of linear equations using the distances from each anchor
3. **Position Update**: Automatically recalculates when new distance data arrives
4. **Path Tracking**: Stores up to 100 historical positions to show movement trail

## ESP32 WebSocket Server Setup

To stream data from your ESP32 to the web visualizer:

1. Configure your ESP32 to create a WebSocket server
2. Send distance measurements in the format: `[TAG] A0 = 2.35 m\n`
3. Use port 81 (or configure as needed)
4. Example WebSocket URL: `ws://192.168.4.1:81`

See the `example/range_tx_tag_esp32_web/` directory for ESP32 code examples.

## Browser Compatibility

Tested and working on:
- Chrome 90+
- Firefox 88+
- Edge 90+
- Safari 14+

**Note**: WebSocket support requires a modern browser. All browsers listed above support WebSockets.

## Advantages over Python Visualizer

| Feature | Web Visualizer | Python Visualizer |
|---------|---------------|-------------------|
| Installation | None - just open HTML | Requires Python + dependencies |
| Platform | Any OS with browser | Windows/Mac/Linux |
| Dependencies | Zero | matplotlib, numpy, pyserial, etc. |
| Deployment | Single HTML file | Multiple files + venv |
| Remote Access | Yes (via web server) | No |
| Mobile Support | Yes (tablets) | No |
| UI | Modern, responsive | Traditional desktop |

## Customization

### Changing Colors

Edit the anchor colors in the JavaScript:

```javascript
state.anchors = [
    { id: 'A0', x: 0, y: 0, distance: null, color: '#4caf50' }, // Green
    { id: 'A1', x: 3, y: 0, distance: null, color: '#2196f3' }, // Blue
    { id: 'A2', x: 1.5, y: 2.6, distance: null, color: '#ff9800' } // Orange
];
```

### Adjusting Canvas Size

Modify the canvas dimensions in the HTML:

```html
<canvas id="visualCanvas" width="800" height="600"></canvas>
```

Or change in the config object:

```javascript
const config = {
    canvasWidth: 800,
    canvasHeight: 600,
    // ...
};
```

## Troubleshooting

### No Position Calculated

- **Issue**: "Insufficient data (need 3+ anchors)" message
- **Solution**: Ensure at least 3 anchors have valid distance measurements

### WebSocket Connection Failed

- **Issue**: Cannot connect to ESP32
- **Solution**: 
  - Verify ESP32 WebSocket server is running
  - Check IP address and port
  - Ensure ESP32 and computer are on the same network
  - Check browser console for error messages

### Inaccurate Position

- **Issue**: Tag position doesn't match real location
- **Solution**:
  - Verify anchor coordinates are correct
  - Check for measurement errors in distance data
  - Ensure anchors are not collinear (not in a straight line)
  - Consider calibrating the UWB system

## Demo Mode

To test the visualizer without hardware:

1. Open `index.html`
2. Uncomment the demo mode lines at the bottom of the script:

```javascript
// Initialize on page load
window.addEventListener('load', () => {
    init();
    startDemo(); // Uncomment for demo mode
});
```

3. Also uncomment these lines in the `startDemo()` function:

```javascript
// anchor.distance = dist + (Math.random() - 0.5) * 0.1;
// updateAnchorList();
// updateTagPosition();
```

This will simulate a moving tag with realistic distance measurements.

## Technical Details

- **Coordinate System**: Cartesian (X, Y) with origin at center
- **Units**: Meters for positions, meters for distances
- **Refresh Rate**: Depends on data input rate
- **Trilateration Method**: Least-squares solution
- **Path Buffer**: 100 positions (configurable via `config.pathMaxLength`)

## File Structure

```
web_visualizer/
├── index.html          # Main application (standalone, no dependencies)
└── README.md          # This file
```

## Integration with Existing Examples

This web visualizer can be used with:

1. **range_tx_tag_esp32_web**: ESP32 WebSocket server example
2. **uwb_visualizer**: Can import data from Python visualizer logs
3. **IndoorPositioning**: Compatible data format

## License

This web visualizer follows the same license as the main repository.

## Credits

- Web visualizer developed as an alternative to the Python-based tools
- Trilateration algorithm based on the existing Python visualizer
- Compatible with Makerfabs ESP32 UWB DW3000 hardware

## Support

For issues or questions:
- Check the main repository README
- Review the ESP32 UWB DW3000 Wiki: https://wiki.makerfabs.com/ESP32_DW3000_UWB.html
- Open an issue in the repository
