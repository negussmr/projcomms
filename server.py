"""
server.py
=========
Main entry point. Connects to the ESP32-CAM MJPEG stream, runs YOLO
detection on each frame in a display loop, and provides an optional Flask
dashboard (JSON status + browser-viewable MJPEG relay).

Run with:
    python server.py
"""

import sys
import time
import threading

import cv2

import config
from utils import FPSCounter, FrameQueue, draw_detection_box, draw_hud, \
    resize_for_display, ensure_dir, generate_timestamp
from logger import setup_logger, DetectionLogger
from recorder import VideoRecorder
from detection import YOLODetector, ModelNotFoundError

try:
    from flask import Flask, Response, jsonify
    _FLASK_AVAILABLE = True
except ImportError:
    _FLASK_AVAILABLE = False


# ===========================================================================
# ESP32-CAM stream reader (runs in its own thread)
# ===========================================================================
class ESP32StreamReader:
    """
    Continuously pulls frames from the ESP32-CAM MJPEG stream in a
    background thread and pushes them into a drop-oldest FrameQueue.
    Automatically reconnects on Wi-Fi loss, camera unavailability, or
    corrupted frames.
    """

    def __init__(self, stream_url: str, logger, frame_queue: FrameQueue):
        self.stream_url = stream_url
        self.logger = logger
        self.frame_queue = frame_queue

        self._cap = None
        self._running = False
        self._thread = None
        self._connected = False
        self._bad_frame_count = 0

    @property
    def connected(self) -> bool:
        return self._connected

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=2)
        self._release_capture()

    def _release_capture(self):
        if self._cap is not None:
            self._cap.release()
            self._cap = None
        self._connected = False

    def _connect(self) -> bool:
        self._release_capture()
        self.logger.info(f"Connecting to ESP32-CAM stream: {self.stream_url}")
        cap = cv2.VideoCapture(self.stream_url)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        deadline = time.time() + config.STREAM_CONNECT_TIMEOUT
        while time.time() < deadline:
            if cap.isOpened():
                self._cap = cap
                self._connected = True
                self._bad_frame_count = 0
                self.logger.info("Connected to ESP32-CAM stream.")
                return True
            time.sleep(0.1)

        cap.release()
        self.logger.warning("Could not connect to ESP32-CAM stream (timeout).")
        return False

    def _run(self):
        while self._running:
            if not self._connected:
                if not self._connect():
                    time.sleep(config.STREAM_RECONNECT_DELAY)
                    continue

            ok, frame = self._cap.read()

            if not ok or frame is None or frame.size == 0:
                self._bad_frame_count += 1
                self.logger.debug("Received empty/corrupted frame from stream.")
                if self._bad_frame_count >= config.MAX_CONSECUTIVE_BAD_FRAMES:
                    self.logger.warning(
                        "Too many bad frames in a row -- assuming ESP32 "
                        "disconnected or Wi-Fi lost. Reconnecting..."
                    )
                    self._connected = False
                    time.sleep(config.STREAM_RECONNECT_DELAY)
                continue

            self._bad_frame_count = 0
            self.frame_queue.put(frame)


# ===========================================================================
# Optional Flask dashboard
# ===========================================================================
class DashboardServer:
    """
    Lightweight Flask app exposing:
      GET /status  -> JSON with fps, detection count, model, recording state
      GET /stream  -> MJPEG relay of the latest ANNOTATED frame (browser-viewable)
    Runs in its own daemon thread so it never blocks the main OpenCV loop.
    """

    def __init__(self, app_state: dict, logger):
        self.app_state = app_state
        self.logger = logger
        self._thread = None

        if not _FLASK_AVAILABLE:
            self.logger.warning("Flask is not installed; dashboard disabled.")
            self.enabled = False
            return

        self.enabled = True
        self.app = Flask(__name__)
        self._register_routes()

    def _register_routes(self):
        app_state = self.app_state

        @self.app.route("/status")
        def status():
            return jsonify(
                {
                    "connected": app_state.get("connected", False),
                    "fps": round(app_state.get("fps", 0.0), 2),
                    "detections": app_state.get("detection_count", 0),
                    "model": app_state.get("model_name", ""),
                    "recording": app_state.get("recording", False),
                    "paused": app_state.get("paused", False),
                    "confidence_threshold": config.CONFIDENCE_THRESHOLD,
                }
            )

        @self.app.route("/stream")
        def stream():
            def generate():
                while True:
                    frame = app_state.get("latest_annotated_frame")
                    if frame is not None:
                        ok, buffer = cv2.imencode(".jpg", frame)
                        if ok:
                            yield (
                                b"--frame\r\n"
                                b"Content-Type: image/jpeg\r\n\r\n"
                                + buffer.tobytes()
                                + b"\r\n"
                            )
                    time.sleep(1.0 / max(config.MAX_FPS, 1))

            return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")

    def start(self):
        if not self.enabled:
            return self
        self._thread = threading.Thread(
            target=lambda: self.app.run(
                host=config.FLASK_HOST,
                port=config.FLASK_PORT,
                debug=False,
                use_reloader=False,
                threaded=True,
            ),
            daemon=True,
        )
        self._thread.start()
        self.logger.info(
            f"Dashboard running at http://{config.FLASK_HOST}:{config.FLASK_PORT}/status "
            f"and /stream"
        )
        return self


# ===========================================================================
# Main application
# ===========================================================================
class Application:
    def __init__(self):
        ensure_dir(config.SCREENSHOTS_DIR)
        ensure_dir(config.RECORDINGS_DIR)
        ensure_dir(config.LOGS_DIR)

        self.logger = setup_logger()
        self.detection_logger = DetectionLogger()

        try:
            self.detector = YOLODetector(logger=self.logger)
        except ModelNotFoundError as exc:
            self.logger.error(str(exc))
            print(f"\nFATAL: {exc}\n")
            sys.exit(1)

        self.frame_queue = FrameQueue(maxsize=config.FRAME_QUEUE_SIZE)
        self.stream_reader = ESP32StreamReader(
            config.STREAM_URL, self.logger, self.frame_queue
        )
        self.recorder = VideoRecorder(logger=self.logger)
        self.fps_counter = FPSCounter()

        # Shared state dict exposed to the Flask dashboard thread.
        self.app_state = {
            "connected": False,
            "fps": 0.0,
            "detection_count": 0,
            "model_name": self.detector.model_name,
            "recording": False,
            "paused": False,
            "latest_annotated_frame": None,
        }

        self.dashboard = DashboardServer(self.app_state, self.logger)

        # Toggle-able runtime state (keyboard controlled).
        self.show_confidence = config.SHOW_CONFIDENCE
        self.show_boxes = True
        self.paused = False
        self.fullscreen = False
        self.frame_number = 0

    # ------------------------------------------------------------------
    def _handle_key(self, key: int, current_frame) -> bool:
        """Process a single keyboard event. Returns False if the app should quit."""
        if key == -1:
            return True

        char = chr(key & 0xFF).lower()

        if char == "q" or key == 27:  # 'q' or ESC
            self.logger.info("Quit requested by user.")
            return False

        elif char == "s":
            if config.ENABLE_SCREENSHOTS and current_frame is not None:
                self._save_screenshot(current_frame)

        elif char == "r":
            h, w = current_frame.shape[:2] if current_frame is not None else (480, 640)
            is_recording = self.recorder.toggle(w, h)
            self.app_state["recording"] = is_recording

        elif char == "f":
            self.fullscreen = not self.fullscreen
            prop = cv2.WINDOW_FULLSCREEN if self.fullscreen else cv2.WINDOW_NORMAL
            cv2.setWindowProperty(config.WINDOW_TITLE, cv2.WND_PROP_FULLSCREEN, prop)

        elif char == "c":
            self.show_confidence = not self.show_confidence

        elif char == "b":
            self.show_boxes = not self.show_boxes

        elif char == "p":
            self.paused = True
            self.app_state["paused"] = True

        elif key == 32:  # SPACE
            self.paused = False
            self.app_state["paused"] = False

        return True

    def _save_screenshot(self, frame) -> None:
        filename = f"screenshot_{generate_timestamp()}.png"
        path = f"{config.SCREENSHOTS_DIR}/{filename}"
        cv2.imwrite(path, frame)
        self.logger.info(f"Screenshot saved: {path}")

    # ------------------------------------------------------------------
    def run(self):
        self.logger.info("Starting ESP32-CAM YOLO detection system...")
        self.stream_reader.start()
        self.dashboard.start()

        cv2.namedWindow(config.WINDOW_TITLE, cv2.WINDOW_NORMAL)

        min_frame_interval = 1.0 / config.MAX_FPS if config.MAX_FPS > 0 else 0
        last_frame_time = 0.0
        last_annotated_frame = None

        try:
            while True:
                self.app_state["connected"] = self.stream_reader.connected

                now = time.time()
                if now - last_frame_time < min_frame_interval:
                    key = cv2.waitKey(1)
                    if not self._handle_key(key, last_annotated_frame):
                        break
                    continue

                try:
                    frame = self.frame_queue.get(timeout=1.0)
                except Exception:
                    key = cv2.waitKey(1)
                    if not self._handle_key(key, last_annotated_frame):
                        break
                    continue

                last_frame_time = now
                self.frame_number += 1

                if not self.paused:
                    annotated = self._process_frame(frame)
                    last_annotated_frame = annotated
                    self.app_state["latest_annotated_frame"] = annotated

                    if self.recorder.is_recording:
                        self.recorder.write(annotated)

                display_frame = resize_for_display(last_annotated_frame, config.DISPLAY_WIDTH) \
                    if last_annotated_frame is not None else frame
                cv2.imshow(config.WINDOW_TITLE, display_frame)

                key = cv2.waitKey(1)
                if not self._handle_key(key, last_annotated_frame):
                    break

        except KeyboardInterrupt:
            self.logger.info("Interrupted by user (Ctrl+C).")
        finally:
            self._shutdown()

    # ------------------------------------------------------------------
    def _process_frame(self, frame):
        """Run detection on `frame`, draw overlays, and return the annotated copy."""
        annotated = frame.copy()

        detections = []
        if self.frame_number % config.DETECTION_EVERY_N_FRAMES == 0:
            try:
                detections = self.detector.detect(frame)
            except Exception as exc:
                self.logger.error(f"Detection error: {exc}")
                detections = []

        if self.show_boxes:
            for det in detections:
                color = self.detector.get_color(det.class_name)
                draw_detection_box(
                    annotated,
                    det.xyxy,
                    det.class_name,
                    det.confidence if self.show_confidence else 0.0,
                    color,
                )

        for det in detections:
            self.detection_logger.log_detection(
                det.class_name, det.confidence, self.frame_number
            )

        fps = self.fps_counter.tick()
        h, w = frame.shape[:2]
        draw_hud(
            annotated,
            fps=fps,
            detection_count=len(detections),
            model_name=self.detector.model_name,
            resolution=(w, h),
            conf_threshold=config.CONFIDENCE_THRESHOLD,
            recording=self.recorder.is_recording,
            paused=self.paused,
        )

        self.app_state["fps"] = fps
        self.app_state["detection_count"] = len(detections)

        return annotated

    # ------------------------------------------------------------------
    def _shutdown(self):
        self.logger.info("Shutting down...")
        self.stream_reader.stop()
        self.recorder.stop()
        cv2.destroyAllWindows()
        self.logger.info("Shutdown complete.")


def main():
    app = Application()
    app.run()


if __name__ == "__main__":
    main()
