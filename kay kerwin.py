import cv2
import base64
import numpy as np
from flask import Flask, request, jsonify, send_file
from flask_cors import CORS
from ultralytics import YOLO
import io
import threading
import time
from collections import deque

app = Flask(__name__)
CORS(app)  # Allow ESP32 to connect

# Load YOLO model
model = YOLO('yolov8n.pt')
print("✅ YOLO model loaded!")

# Store latest detections for web UI
latest_detections = []
latest_frame = None
frame_lock = threading.Lock()

@app.route('/detect', methods=['POST'])
def detect():
    """ESP32 sends image → YOLO detects → returns JSON"""
    global latest_detections, latest_frame
    
    if 'image' not in request.json:
        return jsonify({'error': 'No image'}), 400
    
    # Decode image
    img_data = base64.b64decode(request.json['image'])
    np_arr = np.frombuffer(img_data, np.uint8)
    img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
    
    if img is None:
        return jsonify({'error': 'Invalid image'}), 400
    
    # Run YOLO
    results = model(img, conf=0.5)  # Confidence threshold 50%
    
    # Extract detections
    detections = []
    for r in results:
        boxes = r.boxes
        if boxes is not None:
            for box in boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0].tolist())
                conf = float(box.conf[0])
                cls = int(box.cls[0])
                detections.append({
                    'class': model.names[cls],
                    'confidence': round(conf, 2),
                    'bbox': [x1, y1, x2, y2]
                })
    
    # Update latest
    with frame_lock:
        latest_detections = detections
        # Draw boxes on frame and save for web
        annotated = results[0].plot()
        latest_frame = annotated
    
    return jsonify({
        'detections': detections,
        'count': len(detections)
    })

@app.route('/latest', methods=['GET'])
def get_latest():
    """Web UI fetches latest detections"""
    with frame_lock:
        return jsonify({'detections': latest_detections})

@app.route('/frame', methods=['GET'])
def get_frame():
    """Web UI fetches latest annotated frame"""
    with frame_lock:
        if latest_frame is None:
            return '', 404
        _, buffer = cv2.imencode('.jpg', latest_frame)
        return send_file(
            io.BytesIO(buffer.tobytes()),
            mimetype='image/jpeg'
        )

@app.route('/stream', methods=['GET'])
def stream_page():
    """Simple HTML page to view stream"""
    return '''
    <!DOCTYPE html>
    <html>
    <head>
        <title>YOLO Stream</title>
        <style>
            body { font-family: Arial; background: #0a0a0a; color: white; text-align: center; }
            img { border: 3px solid #00ff88; border-radius: 10px; max-width: 90%; margin: 20px; }
            #info { background: #1a1a1a; padding: 20px; border-radius: 10px; margin: 20px; }
            .detection { color: #00ff88; }
        </style>
    </head>
    <body>
        <h1>🎯 YOLO Detection Stream</h1>
        <div id="info">
            <p>Detections: <span id="count" class="detection">0</span></p>
            <div id="list"></div>
        </div>
        <img id="frame" src="/frame" width="640">
        <script>
            async function update() {
                // Update detections
                const resp = await fetch('/latest');
                const data = await resp.json();
                document.getElementById('count').textContent = data.detections.length;
                
                if (data.detections.length > 0) {
                    const list = data.detections.map(d => 
                        `${d.class} (${Math.round(d.confidence*100)}%)`
                    ).join(' | ');
                    document.getElementById('list').textContent = '🔍 ' + list;
                } else {
                    document.getElementById('list').textContent = '🔍 No objects detected';
                }
                
                // Refresh image
                document.getElementById('frame').src = '/frame?' + Date.now();
            }
            setInterval(update, 200);
        </script>
    </body>
    </html>
    '''

if __name__ == '__main__':
    print("🚀 Starting YOLO Server...")
    print(f"📡 Open http://localhost:5000/stream in your browser")
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
