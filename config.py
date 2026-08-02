"""
config.py
=========
Single source of truth for the entire ESP32-CAM + YOLO detection system.

Every other module imports values from here. Change a value in this file
and the behaviour of the whole program changes accordingly -- no other
code needs to be touched.
"""

import os

# ---------------------------------------------------------------------------
# PATHS
# ---------------------------------------------------------------------------
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

MODELS_DIR = os.path.join(BASE_DIR, "models")
SCREENSHOTS_DIR = os.path.join(BASE_DIR, "screenshots")
RECORDINGS_DIR = os.path.join(BASE_DIR, "recordings")
LOGS_DIR = os.path.join(BASE_DIR, "logs")

# ---------------------------------------------------------------------------
# MODEL SELECTION
# ---------------------------------------------------------------------------
# Point this at ANY Ultralytics-compatible weights file:
#   - A stock model such as "yolov8n.pt" or "yolo11n.pt" (auto-downloaded)
#   - A custom-trained model such as "models/best.pt", "models/helmet.pt",
#     "models/fire.pt", "models/attendance.pt"
#
# This is the ONLY line you need to change to switch models.
MODEL_PATH = os.path.join(MODELS_DIR, "best.pt")

# ---------------------------------------------------------------------------
# ESP32-CAM STREAM
# ---------------------------------------------------------------------------
# Replace with the IP address printed on the ESP32-CAM's serial monitor.
ESP32_IP = "192.168.1.50"
STREAM_PORT = 81
STREAM_PATH = "/stream"
STREAM_URL = f"http://{ESP32_IP}:{STREAM_PORT}{STREAM_PATH}"

# How many seconds to wait for a connection before treating the stream as
# unavailable and starting a reconnect cycle.
STREAM_CONNECT_TIMEOUT = 5

# How many seconds to wait between reconnect attempts.
STREAM_RECONNECT_DELAY = 3

# Maximum consecutive corrupted/empty frames tolerated before forcing a
# reconnect.
MAX_CONSECUTIVE_BAD_FRAMES = 15

# ---------------------------------------------------------------------------
# DETECTION SETTINGS
# ---------------------------------------------------------------------------
CONFIDENCE_THRESHOLD = 0.45      # Minimum confidence to accept a detection
IOU_THRESHOLD = 0.45             # Non-max-suppression IoU threshold
IMAGE_SIZE = 640                 # Inference resolution (square)

# ---------------------------------------------------------------------------
# HARDWARE / PERFORMANCE
# ---------------------------------------------------------------------------
GPU_ENABLED = True                # Try to use CUDA if available, else CPU
USE_HALF_PRECISION = True         # FP16 inference (only applies when on GPU)
MAX_FPS = 30                      # Hard cap on processing loop FPS
FRAME_QUEUE_SIZE = 2              # Small queue = low latency (drop-oldest)
DETECTION_EVERY_N_FRAMES = 1      # 1 = detect every frame, 2 = every other, etc.

# ---------------------------------------------------------------------------
# DISPLAY / OVERLAY
# ---------------------------------------------------------------------------
WINDOW_TITLE = "ESP32-CAM YOLO Detection"
SHOW_FPS = True
SHOW_CONFIDENCE = True
SHOW_CLASS_NAME = True
SHOW_OBJECT_ID = False
SHOW_OBJECT_CENTER = False
DRAW_FILLED_LABEL = True
BOX_THICKNESS = 2
FONT_SIZE = 0.55
TEXT_THICKNESS = 1
DISPLAY_WIDTH = 960               # Window is resized to this width for display only
                                    # (does not affect inference resolution)

# ---------------------------------------------------------------------------
# RECORDING / SCREENSHOTS / LOGGING
# ---------------------------------------------------------------------------
ENABLE_RECORDING = False          # Start with recording ON or OFF (toggle with 'R')
RECORDING_FORMAT = "mp4"          # "mp4" or "avi"
RECORDING_FPS = 20.0

ENABLE_SCREENSHOTS = True         # Whether 'S' key is allowed to save screenshots

SAVE_DETECTIONS = True            # Whether to log every detection to CSV
SAVE_UNKNOWN_OBJECTS = False      # Also log detections below the confidence threshold

# ---------------------------------------------------------------------------
# FLASK DASHBOARD (optional browser view + JSON status API)
# ---------------------------------------------------------------------------
ENABLE_FLASK_SERVER = True
FLASK_HOST = "0.0.0.0"
FLASK_PORT = 5000

# ---------------------------------------------------------------------------
# WIFI (for reference / used by esp32_cam.ino, not by the Python server)
# ---------------------------------------------------------------------------
WIFI_SSID = "YOUR_WIFI_SSID"
WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"
