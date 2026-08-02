"""
detection.py
============
Wraps an Ultralytics YOLO model (YOLOv8, YOLO11, or any custom-trained
.pt weights file) behind a small, stable interface used by server.py.

Handles:
  - GPU detection with automatic CPU fallback
  - Optional FP16 half-precision inference on GPU
  - Missing-model-file error handling
  - Converting raw Ultralytics results into simple Python objects
"""

import os
from dataclasses import dataclass

import torch
from ultralytics import YOLO

import config
from classes import build_color_map


class ModelNotFoundError(Exception):
    """Raised when config.MODEL_PATH does not point to a loadable model."""


@dataclass
class Detection:
    class_name: str
    confidence: float
    xyxy: tuple          # (x1, y1, x2, y2)
    center: tuple         # (cx, cy)
    track_id: int = None


class YOLODetector:
    """High-level wrapper around an Ultralytics YOLO model."""

    def __init__(self, logger=None):
        self.logger = logger
        self.device = self._resolve_device()
        self.half = config.USE_HALF_PRECISION and self.device.startswith("cuda")
        self.model = None
        self.model_name = None
        self.color_map = {}
        self.load_model(config.MODEL_PATH)

    # ------------------------------------------------------------------
    def _resolve_device(self) -> str:
        """Pick 'cuda' if GPU is requested and available, else 'cpu'."""
        if config.GPU_ENABLED and torch.cuda.is_available():
            if self.logger:
                self.logger.info(f"GPU detected: {torch.cuda.get_device_name(0)}")
            return "cuda:0"

        if config.GPU_ENABLED and not torch.cuda.is_available():
            if self.logger:
                self.logger.warning("GPU requested but not available. Falling back to CPU.")

        return "cpu"

    # ------------------------------------------------------------------
    def load_model(self, model_path: str) -> None:
        """
        Load (or reload) a YOLO model from `model_path`. Stock model names
        like "yolov8n.pt" are auto-downloaded by Ultralytics; local custom
        weights must already exist on disk.
        """
        is_local_reference = os.path.dirname(model_path) not in ("", None)
        if is_local_reference and not os.path.isfile(model_path):
            message = (
                f"Model file not found: {model_path}\n"
                f"  -> Copy your trained weights into the 'models/' folder "
                f"and confirm MODEL_PATH in config.py matches the filename."
            )
            if self.logger:
                self.logger.error(message)
            raise ModelNotFoundError(message)

        if self.logger:
            self.logger.info(f"Loading model: {model_path} on device={self.device}")

        try:
            model = YOLO(model_path)
            model.to(self.device)
        except Exception as exc:
            message = f"Failed to load YOLO model '{model_path}': {exc}"
            if self.logger:
                self.logger.error(message)
            raise ModelNotFoundError(message) from exc

        self.model = model
        self.model_name = os.path.basename(model_path)
        self.color_map = build_color_map(self.model.names)

        if self.logger:
            self.logger.info(
                f"Model loaded successfully. Classes: {list(self.model.names.values())}"
            )

    # ------------------------------------------------------------------
    def detect(self, frame) -> list:
        """
        Run inference on a single BGR frame (numpy array).
        Returns a list of Detection objects that passed the confidence
        threshold configured in config.py.
        """
        if self.model is None:
            return []

        results = self.model.predict(
            source=frame,
            imgsz=config.IMAGE_SIZE,
            conf=config.CONFIDENCE_THRESHOLD,
            iou=config.IOU_THRESHOLD,
            half=self.half,
            device=self.device,
            verbose=False,
        )

        detections = []
        if not results:
            return detections

        result = results[0]
        if result.boxes is None:
            return detections

        for box in result.boxes:
            cls_id = int(box.cls[0].item())
            confidence = float(box.conf[0].item())
            x1, y1, x2, y2 = [float(v) for v in box.xyxy[0].tolist()]
            cx, cy = (x1 + x2) / 2.0, (y1 + y2) / 2.0
            class_name = self.model.names.get(cls_id, str(cls_id))

            detections.append(
                Detection(
                    class_name=class_name,
                    confidence=confidence,
                    xyxy=(x1, y1, x2, y2),
                    center=(cx, cy),
                )
            )

        return detections

    # ------------------------------------------------------------------
    def get_color(self, class_name: str):
        if class_name not in self.color_map:
            from classes import get_class_color
            self.color_map[class_name] = get_class_color(class_name)
        return self.color_map[class_name]
