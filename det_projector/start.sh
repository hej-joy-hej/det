#!/bin/bash

# Navigate to the project directory
cd ~/Desktop/det/det_projector

# Kill any existing servers on these ports
echo "Cleaning up any existing servers..."
lsof -ti:8000 | xargs kill -9 2>/dev/null
lsof -ti:8765 | xargs kill -9 2>/dev/null

# Start HTTP server in background
echo "Starting HTTP server on port 8000..."
python3 -m http.server 8000 > /dev/null 2>&1 &
HTTP_PID=$!

# Wait a moment for HTTP server to start
sleep 1

# Start WebSocket server in background
echo "Starting WebSocket server on port 8765..."
python3 video_controller.py &
WS_PID=$!

# Wait for servers to initialize
sleep 2

# Open display in Safari
echo "Opening display in Safari..."
open -a Safari http://localhost:8000/display.html

echo ""
echo "===================================="
echo "Installation is running!"
echo "HTTP Server PID: $HTTP_PID"
echo "WebSocket Server PID: $WS_PID"
echo ""
echo "Press Safari fullscreen: Control+Command+F"
echo ""
echo "To stop, press Ctrl+C or run:"
echo "  kill $HTTP_PID $WS_PID"
echo "===================================="

# Wait for Ctrl+C
trap "echo 'Stopping servers...'; kill $HTTP_PID $WS_PID 2>/dev/null; exit" INT
wait
