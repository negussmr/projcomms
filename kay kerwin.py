import cv2
import numpy as np
from flask import Flask, request, jsonify, send_file
from flask_cors import CORS
from ultralytics import YOLO
import io
import base64
import os
from datetime import datetime

app = Flask(__name__)
CORS(app)

# ===== Load YOLO Model =====
print("🔄 Loading YOLO model...")
try:
    model = YOLO('yolov8n.pt')  # You can also use 'yolov8s.pt', 'yolov8m.pt'
    print("✅ YOLOv8 model loaded!")
except Exception as e:
    print(f"❌ Failed to load model: {e}")
    exit(1)

# ===== Settings =====
CONFIDENCE_THRESHOLD = 0.5
SAVE_DETECTIONS = True  # Set to False to not save images
DETECTION_FOLDER = "detections"

if SAVE_DETECTIONS and not os.path.exists(DETECTION_FOLDER):
    os.makedirs(DETECTION_FOLDER)

# ===== Routes =====
@app.route('/detect', methods=['POST'])
def detect():
    """Receive image, run YOLO, return detections"""
    
    # Check if image is in request
    if 'image' not in request.files:
        return jsonify({'error': 'No image file'}), 400
    
    file = request.files['image']
    if file.filename == '':
        return jsonify({'error': 'Empty filename'}), 400
    
    try:
        # Read image
        img_bytes = file.read()
        np_arr = np.frombuffer(img_bytes, np.uint8)
        img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
        
        if img is None:
            return jsonify({'error': 'Invalid image'}), 400
        
        # Run YOLO inference
        results = model(img, conf=CONFIDENCE_THRESHOLD)
        
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
        
        # Save annotated image (optional)
        if SAVE_DETECTIONS and len(detections) > 0:
            annotated = results[0].plot()
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{DETECTION_FOLDER}/detect_{timestamp}.jpg"
            cv2.imwrite(filename, annotated)
            print(f"💾 Saved: {filename} ({len(detections)} objects)")
        
        return jsonify({
            'detections': detections,
            'count': len(detections),
            'timestamp': datetime.now().isoformat()
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        'status': 'online',
        'model': 'YOLOv8',
        'confidence_threshold': CONFIDENCE_THRESHOLD
    })

@app.route('/webcam', methods=['GET'])
def webcam_demo():
    """Web interface for webcam testing"""
    return '''
    <!DOCTYPE html>
    <html>
    <head>
        <title>YOLO Webcam Demo</title>
        <style>
            body { font-family: Arial; background: #0a0a0a; color: white; text-align: center; padding: 20px; }
            video, canvas { border: 2px solid #00ff88; border-radius: 10px; max-width: 90%; margin: 10px; }
            .status { color: #00ff88; font-size: 18px; margin: 10px; }
            .detection-list { background: #1a1a1a; padding: 15px; border-radius: 10px; margin: 10px auto; max-width: 600px; }
            .badge { display: inline-block; background: #00ff88; color: #000; padding: 4px 12px; border-radius: 20px; margin: 3px; }
            button { background: #00ff88; color: #000; border: none; padding: 12px 30px; border-radius: 30px; font-size: 16px; cursor: pointer; margin: 10px; }
            button:hover { background: #00cc66; }
        </style>
    </head>
    <body>
        <h1>🎯 YOLO Webcam Demo</h1>
        <p class="status">📸 Click "Start Camera" to test YOLO with your webcam</p>
        <button onclick="startCamera()">📷 Start Camera</button>
        <button onclick="stopCamera()">⏹ Stop</button>
        <br>
        <video id="video" autoplay style="display:none;"></video>
        <canvas id="canvas"></canvas>
        <div class="detection-list" id="detections">Waiting for detections...</div>
        
        <script>
            const video = document.getElementById('video');
            const canvas = document.getElementById('canvas');
            const ctx = canvas.getContext('2d');
            const detectionsDiv = document.getElementById('detections');
            let stream = null;
            let intervalId = null;

            async function startCamera() {
                try {
                    stream = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480 } });
                    video.srcObject = stream;
                    video.style.display = 'block';
                    canvas.style.display = 'block';
                    
                    // Start detection loop
                    if (intervalId) clearInterval(intervalId);
                    intervalId = setInterval(detectFrame, 200);
                } catch(e) {
                    alert('Camera access denied: ' + e.message);
                }
            }

            function stopCamera() {
                if (stream) {
                    stream.getTracks().forEach(track => track.stop());
                    stream = null;
                }
                if (intervalId) {
                    clearInterval(intervalId);
                    intervalId = null;
                }
                video.style.display = 'none';
                canvas.style.display = 'none';
                detectionsDiv.innerHTML = 'Stopped';
            }

            async function detectFrame() {
                canvas.width = video.videoWidth;
                canvas.height = video.videoHeight;
                ctx.drawImage(video, 0, 0);
                
                // Get image data
                const dataURL = canvas.toDataURL('image/jpeg', 0.8);
                const blob = await fetch(dataURL).then(res => res.blob());
                
                const formData = new FormData();
                formData.append('image', blob, 'frame.jpg');
                
                try {
                    const response = await fetch('/detect', {
                        method: 'POST',
                        body: formData
                    });
                    const data = await response.json();
                    
                    if (data.detections) {
                        // Draw boxes on canvas
                        ctx.drawImage(video, 0, 0);
                        ctx.font = '16px Arial';
                        data.detections.forEach(d => {
                            const [x1, y1, x2, y2] = d.bbox;
                            ctx.strokeStyle = '#00ff88';
                            ctx.lineWidth = 3;
                            ctx.strokeRect(x1, y1, x2-x1, y2-y1);
                            
                            ctx.fillStyle = '#00ff88';
                            ctx.fillRect(x1, y1-25, ctx.measureText(d.class).width + 20, 25);
                            ctx.fillStyle = '#000';
                            ctx.fillText(`${d.class} ${Math.round(d.confidence*100)}%`, x1+5, y1-5);
                        });
                        
                        // Update detection list
                        if (data.detections.length > 0) {
                            let html = '';
                            data.detections.forEach(d => {
                                html += `<span class="badge">${d.class} ${Math.round(d.confidence*100)}%</span>`;
                            });
                            detectionsDiv.innerHTML = html + ` (${data.detections.length} objects)`;
                        } else {
                            detectionsDiv.innerHTML = '👀 No objects detected';
                        }
                    }
                } catch(e) {
                    detectionsDiv.innerHTML = '⚠️ Server error: ' + e.message;
                }
            }
        </script>
    </body>
    </html>
    '''

if __name__ == '__main__':
    print("\n" + "="*50)
    print("🚀 YOLO Server Started!")
    print("="*50)
    print(f"📡 Endpoints:")
    print(f"   POST /detect    - Send image for detection")
    print(f"   GET  /health    - Server health check")
    print(f"   GET  /webcam    - Webcam demo page")
    print(f"\n🌐 Open http://localhost:5000/webcam for demo")
    print("="*50 + "\n")
    
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
