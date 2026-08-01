import cv2
import numpy as np
from flask import Flask, request, jsonify
from ultralytics import YOLO  # YOLOv8
import base64

app = Flask(__name__)

# Load a pretrained YOLOv8 model (you can also use yolov5)
model = YOLO('yolov8n.pt')  # nano model, change to 'yolov8s.pt' for better accuracy

@app.route('/detect', methods=['POST'])
def detect():
    # Get the image from the POST request
    data = request.get_json()
    if 'image' not in data:
        return jsonify({'error': 'No image provided'}), 400

    # Decode base64 image
    image_data = base64.b64decode(data['image'])
    np_arr = np.frombuffer(image_data, np.uint8)
    img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)

    if img is None:
        return jsonify({'error': 'Invalid image'}), 400

    # Run YOLO inference
    results = model(img)  # returns a list of Results objects

    # Prepare detections: class name, confidence, bounding box
    detections = []
    for r in results:
        boxes = r.boxes
        if boxes is not None:
            for box in boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
                conf = float(box.conf[0])
                cls = int(box.cls[0])
                name = model.names[cls]
                detections.append({
                    'class': name,
                    'confidence': round(conf, 2),
                    'bbox': [x1, y1, x2, y2]
                })

    return jsonify({'detections': detections})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)