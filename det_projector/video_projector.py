import serial
import serial.tools.list_ports
import subprocess
import time
import os
import signal

# --- Video paths (update these) ---
VIDEOS = {
    "IDLE":         "/Users/joyhe/Desktop/det_projector/idle.mp4",
    "CONVENTIONAL": "/Users/joyhe/Desktop/det_projector/conventional.mp4",
    "FISH":         "/Users/joyhe/Desktop/det_projector/fish.mp4",
}

BAUD_RATE = 9600

def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "usbmodem" in p.device or "usbserial" in p.device:
            return p.device
    return None

class VideoPlayer:
    def __init__(self):
        self.process = None
        self.current_video = None

    def play(self, path):
        if path == self.current_video:
            return
        self.stop()
        if not os.path.exists(path):
            print(f"  !! File not found: {path}")
            return
        self.process = subprocess.Popen(
            ["ffplay", "-fs", "-loop", "0", "-loglevel", "quiet", path],
            stdin=subprocess.DEVNULL,
        )
        self.current_video = path
        print(f"  -> Playing: {os.path.basename(path)}")

    def stop(self):
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None
            self.current_video = None

def main():
    port = find_arduino_port()
    if not port:
        print("No Arduino found. Check USB.")
        return
    print(f"Connecting to {port}...")

    ser = serial.Serial(port, BAUD_RATE, timeout=1)
    time.sleep(2)
    player = VideoPlayer()

    # start with idle video
    player.play(VIDEOS["IDLE"])

    print("Listening for mode changes...")

    try:
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if line.startswith("MODE:"):
                    mode = line.split(":")[1]
                    print(f"Mode: {mode}")
                    if mode in VIDEOS:
                        player.play(VIDEOS[mode])
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        player.stop()
        ser.close()

if __name__ == "__main__":
    main()