#!/usr/bin/env python3
"""
UWB tag visualizer
Reads lines like: "13:38:49.068 -> [TAG] A2 = 3.28 m" from serial or logfile,
collects latest distances and computes a 2D position by least-squares trilateration.

Usage:
  python visualizer.py --port COM3 --baud 1115200
  python visualizer.py --logfile serial.txt

"""
import argparse
import json
import re
import threading
import queue
import time

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import os

try:
    import tkinter as tk
    from tkinter import simpledialog, messagebox
except Exception:
    tk = None

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
    def __init__(self, port=None, baud=1115200, logfile=None, out_q=None):
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
            # Print raw line to see what we're receiving
            print(f'Raw: {line.strip()}')
            aid, d = parse_line(line)
            if aid:
                print(f'Parsed: {aid} = {d} m')
                self.out_q.put((time.time(), aid, d))
            else:
                print(f'Failed to parse line')


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


def run_anchor_editor(floorplan_path, anchors_file, anchors, image_transform=None):
    """Interactive editor to place anchors on a floorplan and save anchors_file.
    Requires tkinter for prompt dialogs. The editor will ask to calibrate two image points
    to real-world coordinates to compute a pixel->world similarity transform.
    """
    if tk is None:
        print('Tkinter not available; interactive editor requires Tkinter')
        return

    root = tk.Tk()
    root.withdraw()

    img = plt.imread(floorplan_path)
    h, w = img.shape[0], img.shape[1]

    fig, ax = plt.subplots()
    ax.set_title('Anchor editor - click to add anchors. Press Close to finish.')

    transform = None
    artists = {}
    
    # keep reference to initial image extent for recalibration
    initial_extent = None
    initial_origin = 'upper'

    # show image initially in pixel coords
    im = ax.imshow(img, origin='upper')

    cal_points = []
    scale_points = []
    ppm = None
    origin_pixel = None
    origin_world = None

    def compute_transform(pix_pts, world_pts):
        # pix_pts and world_pts are lists of two (u,v) and (x,y)
        (u1, v1), (u2, v2) = pix_pts
        (x1, y1), (x2, y2) = world_pts
        z1 = complex(u1, v1)
        z2 = complex(u2, v2)
        w1 = complex(x1, y1)
        w2 = complex(x2, y2)
        if z2 == z1:
            raise ValueError('Calibration image points must be different')
        r = (w2 - w1) / (z2 - z1)
        t = w1 - r * z1
        return r, t

    def save_anchors():
        out = {k: [float(v[0]), float(v[1])] for k, v in anchors.items()}
        if transform is not None:
            # two possible transform formats: legacy complex r/t tuple or new ppm-based dict
            if isinstance(transform, dict):
                out['_image_transform'] = transform
                out['_image_transform']['image_file'] = os.path.basename(floorplan_path)
            else:
                r, t, imgshape = transform
                out['_image_transform'] = {'r': [r.real, r.imag], 't': [t.real, t.imag], 'image_shape': [int(imgshape[0]), int(imgshape[1])], 'image_file': os.path.basename(floorplan_path)}
        with open(anchors_file, 'w') as f:
            json.dump(out, f, indent=2)
        print('Anchors saved to', anchors_file)

    def redraw_anchors():
        # remove existing artists
        for a in list(artists.values()):
            try:
                a['pt'].remove()
            except Exception:
                # artist may already have been removed when axes were cleared
                pass
            try:
                a['txt'].remove()
            except Exception:
                pass
        artists.clear()
        for k, (x, y) in anchors.items():
            pt = ax.plot(x, y, 'rs', markersize=8)[0]
            txt = ax.text(x, y, k, color='red', fontsize=10, ha='right', va='bottom')
            artists[k] = {'pt': pt, 'txt': txt}
        fig.canvas.draw_idle()

    def on_click(event):
        # only handle clicks on image axes
        # if we're in the middle of a calibration flow, ignore generic clicks
        if calib_state.get('await_clicks'):
            return
        if event.inaxes != ax:
            return
        if event.button == 1:
            # left click: add anchor (requires transform)
            if transform is None:
                messagebox.showinfo('Calibration required', 'Please calibrate the image to real-world coordinates before adding anchors. Press OK to start calibration.')
                start_calibration()
                return
            xw = event.xdata
            yw = event.ydata
            # default label
            default_label = 'A{}'.format(len(anchors) + 1)
            label = simpledialog.askstring('Anchor label', 'Enter anchor label:', initialvalue=default_label, parent=root)
            if not label:
                return
            # use clicked world coords as default, allow user to adjust
            try:
                xs = simpledialog.askfloat('Anchor X', 'Enter X (meters):', initialvalue=float(xw), parent=root)
                ys = simpledialog.askfloat('Anchor Y', 'Enter Y (meters):', initialvalue=float(yw), parent=root)
            except Exception:
                return
            if xs is None or ys is None:
                return
            anchors[label] = (float(xs), float(ys))
            redraw_anchors()
        elif event.button == 3:
            # right click: edit/delete nearest anchor
            if len(anchors) == 0:
                return
            # find nearest
            px = event.xdata
            py = event.ydata
            items = [(k, anchors[k]) for k in anchors]
            dists = [((v[0]-px)**2 + (v[1]-py)**2, k) for k, v in items]
            dists.sort()
            nearest = dists[0][1]
            res = messagebox.askquestion('Edit anchor', f'Do you want to delete anchor {nearest}?', icon='warning')
            if res == 'yes':
                anchors.pop(nearest, None)
                redraw_anchors()

    calib_state = {'await_clicks': False}

    def start_calibration():
        # legacy calibration (two world points) - kept for compatibility
        cal_points.clear()
        calib_state['mode'] = 'two_points'
        calib_state['await_clicks'] = True
        messagebox.showinfo('Calibration', 'Click two points on the image that you know coordinates for (point 1 then point 2).')

    def start_scale_calibration():
        # Option B flow: click two scale-bar endpoints, enter real length, then click origin and enter world coords
        nonlocal initial_extent, initial_origin
        # Reset to pixel view if not already
        if transform is not None:
            # redisplay image in pixel coords
            ax.clear()
            ax.imshow(img, origin='upper')
            initial_extent = None
            initial_origin = 'upper'
            redraw_anchors()
        scale_points.clear()
        calib_state['mode'] = 'scale_bar'
        calib_state['await_clicks'] = True
        # Use non-blocking instruction in the figure title only
        ax.set_title('Scale calibration: click TWO endpoints of the scale bar (endpoint 1 then endpoint 2).')
        fig.canvas.draw_idle()

    def on_key(event):
        if event.key == 'c':
            start_calibration()
        if event.key == 'k':
            start_scale_calibration()

    def on_button(event):
        # handle calibration click collection separately
        if not calib_state.get('await_clicks'):
            return
        if event.inaxes != ax:
            return
        mode = calib_state.get('mode')
        if mode == 'two_points':
            cal_points.append((event.xdata, event.ydata))
            if len(cal_points) == 2:
                calib_state['await_clicks'] = False
                # ask for real-world coords for both points
                try:
                    x1 = simpledialog.askfloat('Point 1 X', 'Enter world X for point 1 (meters):', parent=root)
                    y1 = simpledialog.askfloat('Point 1 Y', 'Enter world Y for point 1 (meters):', parent=root)
                    x2 = simpledialog.askfloat('Point 2 X', 'Enter world X for point 2 (meters):', parent=root)
                    y2 = simpledialog.askfloat('Point 2 Y', 'Enter world Y for point 2 (meters):', parent=root)
                except Exception:
                    messagebox.showerror('Calibration', 'Calibration cancelled or failed')
                    return
                if None in (x1, y1, x2, y2):
                    messagebox.showerror('Calibration', 'Calibration cancelled')
                    return
                try:
                    r, t = compute_transform(cal_points, [(x1, y1), (x2, y2)])
                except Exception as e:
                    messagebox.showerror('Calibration failed', str(e))
                    return
                # save transform and redraw image with world extents
                corners = [complex(0, 0), complex(w, 0), complex(w, h), complex(0, h)]
                world = [r * c + t for c in corners]
                xs = [z.real for z in world]
                ys = [z.imag for z in world]
                extent = [min(xs), max(xs), min(ys), max(ys)]
                ax.clear()
                ax.imshow(img, extent=extent, origin='lower')
                transform_local = (r, t, (h, w))
                nonlocal_transform_set(transform_local)
                redraw_anchors()
        elif mode == 'scale_bar':
            # collect two scale endpoints
            scale_points.append((event.xdata, event.ydata))
            if len(scale_points) == 2:
                # compute pixel distance
                (u1, v1), (u2, v2) = scale_points
                pix_dist = ((u2 - u1)**2 + (v2 - v1)**2)**0.5
                try:
                    real_len = simpledialog.askfloat('Scale length', 'Enter real-world length of the scale bar (meters):', parent=root)
                except Exception:
                    messagebox.showerror('Calibration', 'Scale calibration cancelled')
                    calib_state['await_clicks'] = False
                    return
                if real_len is None or real_len <= 0:
                    messagebox.showerror('Calibration', 'Invalid real-world length')
                    calib_state['await_clicks'] = False
                    return
                # pixels per meter
                ppm_local = pix_dist / float(real_len)
                # now ask for origin pixel: instruct user to click origin (non-blocking)
                ax.set_title('Now click the image point that should be the world origin (e.g. bottom-left corner).')
                fig.canvas.draw_idle()
                # set mode to origin capture
                calib_state['mode'] = 'origin'
                calib_state['await_clicks'] = True
                # store ppm in state
                calib_state['ppm'] = ppm_local
        elif mode == 'origin':
            # capture origin pixel and ask for world coords
            origin_px = (event.xdata, event.ydata)
            try:
                ox = simpledialog.askfloat('Origin X', 'Enter world X for origin point (meters):', parent=root)
                oy = simpledialog.askfloat('Origin Y', 'Enter world Y for origin point (meters):', parent=root)
            except Exception:
                messagebox.showerror('Calibration', 'Origin entry cancelled')
                calib_state['await_clicks'] = False
                return
            if ox is None or oy is None:
                messagebox.showerror('Calibration', 'Origin entry cancelled')
                calib_state['await_clicks'] = False
                return
            calib_state['await_clicks'] = False
            ppm_local = calib_state.get('ppm')
            if ppm_local is None:
                messagebox.showerror('Calibration', 'Internal error: ppm missing')
                return
            # compute world extents from pixel->world linear mapping (no rotation)
            # Keep image with origin='upper' to avoid flipping the image itself
            # Just compute the extent to map pixels to world coordinates
            u0, v0 = origin_px
            x0, y0 = float(ox), float(oy)
            
            # Define corners in pixel space (origin='upper' convention)
            # When origin='upper': extent is [left, right, top, bottom] in world coords
            # top-left corner (pixel 0,0) -> world coords
            wx_topleft = x0 + (0 - u0) / ppm_local
            wy_topleft = y0 - (0 - v0) / ppm_local
            # bottom-right corner (pixel w,h) -> world coords
            wx_botright = x0 + (w - u0) / ppm_local
            wy_botright = y0 - (h - v0) / ppm_local
            
            # For origin='upper', extent is [left, right, top, bottom]
            extent = [wx_topleft, wx_botright, wy_botright, wy_topleft]
            ax.clear()
            ax.imshow(img, extent=extent, origin='upper')
            # store transform as ppm-based mapping
            transform_local = {'type': 'scale_origin', 'ppm': ppm_local, 'origin_pixel': [u0, v0], 'origin_world': [x0, y0], 'image_shape': [h, w]}
            nonlocal_transform_set(transform_local)
            redraw_anchors()

    # helper to set transform in outer scope
    def nonlocal_transform_set(tval):
        nonlocal transform
        transform = tval

    # connect events
    fig.canvas.mpl_connect('button_press_event', on_click)
    fig.canvas.mpl_connect('key_press_event', on_key)
    fig.canvas.mpl_connect('button_press_event', on_button)

    # initial draw for existing anchors (if any)
    redraw_anchors()

    def on_close(event):
        save_anchors()
        try:
            root.quit()
            root.destroy()
        except Exception:
            pass

    fig.canvas.mpl_connect('close_event', on_close)

    plt.show()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', help='Serial port (e.g. COM3 or /dev/ttyUSB0)')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--logfile', help='Read from a saved log file instead of serial')
    p.add_argument('--anchors', default='anchors.json', help='JSON file with anchor coordinates')
    p.add_argument('--floorplan', help='Optional floorplan image to load as background')
    p.add_argument('--edit', action='store_true', help='Open interactive anchor editor for floorplan')
    p.add_argument('--history', type=int, default=500, help='Max points to keep in path')
    args = p.parse_args()

    # load anchors file if available
    anchors = {}
    image_transform = None
    if os.path.exists(args.anchors):
        with open(args.anchors, 'r') as f:
            anchors_raw = json.load(f)
        # allow saved image transform under special key
        if isinstance(anchors_raw, dict) and '_image_transform' in anchors_raw:
            image_transform = anchors_raw.get('_image_transform')
            # remove metadata before anchors
            anchors_raw.pop('_image_transform', None)
        anchors = {k: tuple(v) for k, v in anchors_raw.items()}

    # If edit mode, open interactive editor for anchors on floorplan
    if args.edit:
        if not args.floorplan:
            print('Edit mode requires --floorplan <image> to be provided')
            return
        run_anchor_editor(args.floorplan, args.anchors, anchors, image_transform)
        return

    q = queue.Queue()
    reader = ReaderThread(port=args.port, baud=args.baud, logfile=args.logfile, out_q=q)
    reader.start()

    latest = {}
    path = []

    fig, ax = plt.subplots()
    ax.set_aspect('equal', 'box')

    # If floorplan provided, display it. If image_transform metadata exists, use it
    img = None
    transform = None
    if args.floorplan:
        if not os.path.exists(args.floorplan):
            print('Floorplan image not found:', args.floorplan)
        else:
            img = plt.imread(args.floorplan)
            # If image_transform provided, compute extent in world coords
            if image_transform:
                try:
                    # support legacy complex r/t transform or new ppm-based transform
                    if isinstance(image_transform, dict) and image_transform.get('ppm') is not None:
                        # ppm-based transform
                        ppm_local = float(image_transform['ppm'])
                        u0, v0 = image_transform['origin_pixel']
                        x0, y0 = image_transform['origin_world']
                        h, w = img.shape[0], img.shape[1]
                        
                        # Map pixel corners to world coordinates
                        # Keep origin='upper' to avoid flipping the image
                        wx_topleft = x0 + (0 - u0) / ppm_local
                        wy_topleft = y0 - (0 - v0) / ppm_local
                        wx_botright = x0 + (w - u0) / ppm_local
                        wy_botright = y0 - (h - v0) / ppm_local
                        
                        # For origin='upper', extent is [left, right, top, bottom]
                        extent = [wx_topleft, wx_botright, wy_botright, wy_topleft]
                        ax.imshow(img, extent=extent, origin='upper')
                        transform = image_transform
                    else:
                        r = complex(image_transform['r'][0], image_transform['r'][1])
                        t = complex(image_transform['t'][0], image_transform['t'][1])
                        h, w = img.shape[0], img.shape[1]
                        # map image corners (pixel coords) to world
                        corners = [complex(0, 0), complex(w, 0), complex(w, h), complex(0, h)]
                        world = [r * c + t for c in corners]
                        xs = [z.real for z in world]
                        ys = [z.imag for z in world]
                        extent = [min(xs), max(xs), min(ys), max(ys)]
                        ax.imshow(img, extent=extent, origin='lower')
                        transform = (r, t, (w, h))
                except Exception:
                    ax.imshow(img)
            else:
                ax.imshow(img)

    # Plot anchors (if any)
    path_line, = ax.plot([], [], '-o', color='blue', markersize=4)
    current_point, = ax.plot([], [], 'o', color='green', markersize=8)

    margin = 1.0
    if anchors:
        xs = [c[0] for c in anchors.values()]
        ys = [c[1] for c in anchors.values()]
        ax.scatter(xs, ys, marker='s', c='red')
        for k, (x, y) in anchors.items():
            ax.text(x, y, k, color='red', fontsize=10, ha='right', va='bottom')
        minx = min(xs) - margin
        maxx = max(xs) + margin
        miny = min(ys) - margin
        maxy = max(ys) + margin
        ax.set_xlim(minx, maxx)
        ax.set_ylim(miny, maxy)
    else:
        # no anchors defined: try to use image extents if available, otherwise fallback to defaults
        try:
            xlim = ax.get_xlim()
            ylim = ax.get_ylim()
            ax.set_xlim(xlim)
            ax.set_ylim(ylim)
        except Exception:
            ax.set_xlim(-5, 5)
            ax.set_ylim(-5, 5)
    ax.set_title('UWB Tag position')

    last_update = time.time()

    def update(frame):
        nonlocal last_update
        updated = False
        # drain queue
        while True:
            try:
                tstamp, aid, d = q.get_nowait()
            except queue.Empty:
                break
            latest[aid] = d
            print(f'Received: {aid} = {d:.2f} m')
            updated = True
        if not updated:
            # nothing new
            return path_line, current_point

        # if we have at least 3 anchors with distances, compute position
        try:
            if len(latest) >= 3:
                pos = trilaterate(anchors, latest)
                path.append(pos)
                if len(path) > args.history:
                    path[:] = path[-args.history:]
                xs_p = [p[0] for p in path]
                ys_p = [p[1] for p in path]
                path_line.set_data(xs_p, ys_p)
                current_point.set_data([pos[0]], [pos[1]])
                print(f'Position: ({pos[0]:.2f}, {pos[1]:.2f}) m')
                # expand axis if outside
                ax.relim()
                ax.autoscale_view(True, True, True)
        except Exception as e:
            # simple debug print; ignore and continue
            print('trilat failed:', e)

        return path_line, current_point

    ani = FuncAnimation(fig, update, interval=200, blit=False)

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        reader.stop()


if __name__ == '__main__':
    main()
