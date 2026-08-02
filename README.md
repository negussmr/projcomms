# ESP32-CAM + Custom YOLO Detection System

A complete pipeline: an ESP32-CAM streams live MJPEG video over Wi-Fi, and a
Python server on your PC runs real-time Ultralytics YOLO detection on the
stream, displaying bounding boxes, confidence scores, FPS, and more — with
optional recording, screenshots, CSV logging, and a browser dashboard.

```
ESP32_YOLO/
├── esp32_cam/
│   └── esp32_cam.ino      # ESP32-CAM firmware (Arduino sketch)
├── server.py               # Main entry point — run this
├── config.py                # All settings live here
├── detection.py             # YOLO model wrapper
├── recorder.py              # AVI/MP4 recording
├── logger.py                 # App logging + CSV detection logging
├── utils.py                   # Drawing, FPS, frame-queue helpers
├── classes.py                 # Class name → color mapping
├── requirements.txt
├── models/                    # Put your best.pt / helmet.pt / etc. here
├── screenshots/                # 'S' key saves PNGs here
├── recordings/                  # 'R' key saves AVI/MP4 here
└── logs/                         # app.log + detections.csv
```

---

## 1. Installation

### Python version
Requires **Python 3.9–3.12**. Check your version:
```bash
python --version
```

### Create a virtual environment
```bash
cd ESP32_YOLO
python -m venv venv

# Activate it:
venv\Scripts\activate        # Windows
source venv/bin/activate     # macOS / Linux
```

### Install requirements
```bash
pip install -r requirements.txt
```
This installs OpenCV, NumPy, Ultralytics, PyTorch, and Flask. The first
time you run the server with a stock model name (e.g. `yolov8n.pt`),
Ultralytics will auto-download the weights.

---

## 2. Connecting the ESP32-CAM

1. Open `esp32_cam/esp32_cam.ino` in the Arduino IDE.
2. Install the **esp32** board package (Boards Manager → search "esp32").
3. Select **Board → AI Thinker ESP32-CAM**, and set:
   - Upload Speed: `115200`
   - Partition Scheme: `Huge APP (3MB No OTA/1MB SPIFFS)`
4. Edit these two lines near the top of the file with your network:
   ```cpp
   const char *WIFI_SSID = "YOUR_WIFI_SSID";
   const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
5. Connect the ESP32-CAM via a USB-to-serial adapter, hold the **IO0**
   button while uploading (required on most AI-Thinker boards), then
   release it once upload starts.
6. Open the Serial Monitor at **115200 baud**. After it connects to
   Wi-Fi you'll see:
   ```
   [WIFI] Connected. IP address: 192.168.1.50
   [WIFI] Stream URL:
          http://192.168.1.50:81/stream
   ```
7. Copy that IP address into `config.py`:
   ```python
   ESP32_IP = "192.168.1.50"
   ```

You can sanity-check the stream independently of Python by opening
`http://<ESP_IP>:81/stream` directly in a browser.

---

## 3. Running the server
```bash
python server.py
```
A window titled **"ESP32-CAM YOLO Detection"** opens showing the live
annotated stream. If `ENABLE_FLASK_SERVER = True` in `config.py`, you can
also view it in a browser at `http://<your-pc-ip>:5000/stream`, and check
JSON status at `http://<your-pc-ip>:5000/status`.

### Keyboard controls
| Key    | Action                     |
|--------|----------------------------|
| Q / ESC| Quit                       |
| S      | Save screenshot            |
| R      | Toggle recording           |
| F      | Toggle fullscreen          |
| C      | Toggle confidence display  |
| B      | Toggle bounding boxes      |
| P      | Pause                      |
| SPACE  | Resume                     |

---

## 4. Changing models

Switching models is a **one-line change** in `config.py`:

```python
# Use a stock pretrained model (auto-downloads):
MODEL_PATH = "yolov8n.pt"      # or "yolo11n.pt"

# Or use a custom-trained model:
MODEL_PATH = os.path.join(MODELS_DIR, "best.pt")
MODEL_PATH = os.path.join(MODELS_DIR, "helmet.pt")
MODEL_PATH = os.path.join(MODELS_DIR, "fire.pt")
MODEL_PATH = os.path.join(MODELS_DIR, "attendance.pt")
```

Nothing else in the codebase needs to change — class names, colors,
confidence display, and logging all adapt automatically to whatever
classes the loaded model reports.

---

## 5. Training a custom model

1. Label your dataset (e.g. with [Roboflow](https://roboflow.com) or
   [LabelImg](https://github.com/heartexlabs/labelImg)) in YOLO format.
2. Train with Ultralytics:
   ```bash
   yolo detect train data=your_dataset.yaml model=yolov8n.pt epochs=100 imgsz=640
   ```
3. After training, your weights appear at
   `runs/detect/train/weights/best.pt`.
4. Copy that file into `ESP32_YOLO/models/best.pt`.
5. Set `MODEL_PATH = os.path.join(MODELS_DIR, "best.pt")` in `config.py`.
6. Run `python server.py` — done.

---

## 6. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `Model file not found` at startup | `MODEL_PATH` in `config.py` points to a file that isn't in `models/`. Check the filename and path. |
| Window opens but stream never appears, log says "Could not connect to ESP32-CAM stream" | ESP32 not on the same network, wrong `ESP32_IP`, or the ESP32 hasn't finished booting yet. Confirm with a browser at `http://<ESP_IP>:81/stream`. |
| Stream connects then repeatedly disconnects | Weak Wi-Fi signal to the ESP32-CAM, or `CAMERA_FRAME_SIZE` set too high for your network's bandwidth. Try `FRAMESIZE_VGA` or lower. |
| Low, choppy FPS | Lower `IMAGE_SIZE` in `config.py`, lower `CAMERA_FRAME_SIZE` on the ESP32, enable GPU, or increase `DETECTION_EVERY_N_FRAMES` to detect every 2nd/3rd frame instead of every frame. |
| `CUDA out of memory` | Lower `IMAGE_SIZE`, disable `USE_HALF_PRECISION`, or set `GPU_ENABLED = False` to fall back to CPU. |
| Brownout resets on the ESP32 | Use a proper 5V/2A power supply — USB ports on some laptops can't supply enough current for the camera + Wi-Fi radio under load. |
| `ModuleNotFoundError: No module named 'flask'` | `pip install -r requirements.txt` wasn't run inside the active virtual environment, or you're using a different Python interpreter than the one the venv was created with. |

---

## 7. Performance tips

- Lower `IMAGE_SIZE` (e.g. 480 or 416) for a large FPS boost with a modest
  accuracy tradeoff.
- Set `CAMERA_FRAME_SIZE` on the ESP32 to `FRAMESIZE_VGA` (640x480) or
  lower — the camera is almost always the bottleneck before the GPU is.
- `DETECTION_EVERY_N_FRAMES = 2` runs detection on every other frame while
  still displaying every frame, roughly doubling perceived throughput.
- Keep `FRAME_QUEUE_SIZE` small (1–2). A bigger queue does not increase
  throughput — it only adds latency, since old frames get processed first.

### GPU setup
1. Confirm you have an NVIDIA GPU and up-to-date drivers.
2. Install the CUDA-enabled PyTorch build (see the note at the bottom of
   `requirements.txt`), matching your installed CUDA version.
3. Leave `GPU_ENABLED = True` and `USE_HALF_PRECISION = True` in
   `config.py`. The app will log which device it selected on startup.

### CPU setup
1. Install the default (CPU) `requirements.txt` as-is.
2. Set `GPU_ENABLED = False` in `config.py` (or simply leave it — the app
   automatically falls back to CPU if CUDA isn't available).
3. Use a smaller model (e.g. `yolov8n.pt` or `yolo11n.pt`) and a lower
   `IMAGE_SIZE` for usable frame rates on CPU.

---

## 8. Common errors

- **`esp_camera_init failed 0x105`**: bad wiring/power, or wrong board
  selected in Arduino IDE. Confirm "AI Thinker ESP32-CAM" is selected.
- **`Failed to connect to ESP32: Timed out waiting for packet header`**
  during upload: hold the IO0 button on the ESP32-CAM's programmer board
  while upload starts, release once it begins writing.
- **Green/purple tinted video**: normal for the OV2640 sensor at boot in
  some lighting; the auto white-balance in `initCamera()` corrects it
  within a few seconds.
- **`RuntimeError: CUDA error: no kernel image is available`**: your
  installed PyTorch build doesn't match your GPU's compute capability —
  reinstall the correct CUDA build for your PyTorch version.
