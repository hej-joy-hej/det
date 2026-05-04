import serial
import serial.tools.list_ports
import asyncio
import websockets
import time
import sys

SERIAL_PORT_WINDOWS = 'COM3'
SERIAL_PORT_MAC     = '/dev/cu.usbmodem1101'
BAUD_RATE = 9600

clients = set()

def find_arduino_port():
    if sys.platform == 'win32':
        ports = list(serial.tools.list_ports.comports())
        print(f"Available COM ports: {[f'{p.device} ({p.description})' for p in ports]}")
        for p in ports:
            desc = p.description.lower()
            if any(k in desc for k in ('arduino', 'ch340', 'usb serial', 'usbserial')):
                print(f"Auto-detected: {p.device} ({p.description})")
                return p.device
        if ports:
            print(f"No Arduino keyword match — using first available: {ports[0].device}")
            return ports[0].device
        return None
    else:
        import glob
        ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/cu.usbserial*')
        return ports[0] if ports else None

async def handle_client(websocket):
    clients.add(websocket)
    print(f"Client connected. Total clients: {len(clients)}")
    try:
        await websocket.wait_closed()
    finally:
        clients.remove(websocket)
        print(f"Client disconnected. Total clients: {len(clients)}")

async def broadcast(message):
    if clients:
        await asyncio.gather(
            *[client.send(message) for client in clients],
            return_exceptions=True
        )

async def read_serial():
    port = find_arduino_port()
    if port:
        print(f"Found Arduino on: {port}")
        serial_port = port
    else:
        serial_port = SERIAL_PORT_WINDOWS if sys.platform == 'win32' else SERIAL_PORT_MAC
        print(f"Using configured port: {serial_port}")
    
    try:
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=1)
        time.sleep(2)
        print("Connected to Arduino")
        
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip().replace('\r','')
                print(f"Received: {line}")
                
                if "Conventional" in line:
                    print(f">>> Broadcasting: CONVENTIONAL")
                    await broadcast("CONVENTIONAL")
                elif "Fish" in line:
                    print(f">>> Broadcasting: FISH")
                    await broadcast("FISH")
                elif "back to idle" in line:
                    print(f">>> Broadcasting: IDLE")
                    await broadcast("IDLE")
            
            await asyncio.sleep(0.05)
    
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

async def main():
    print("Starting WebSocket server on ws://localhost:8765")
    print("Open display.html in your browser")
    
    server = await websockets.serve(handle_client, "localhost", 8765)
    serial_task = asyncio.create_task(read_serial())
    
    await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
