import serial
import asyncio
import websockets
import time
import glob

SERIAL_PORT = '/dev/cu.usbmodem1101'
BAUD_RATE = 9600

clients = set()

def find_arduino_port():
    ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/cu.usbserial*')
    if ports:
        return ports[0]
    return None

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
        serial_port = SERIAL_PORT
        print(f"Using configured port: {serial_port}")
    
    try:
        ser = serial.Serial(serial_port, BAUD_RATE, timeout=1)
        time.sleep(2)
        print("Connected to Arduino")
        
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                print(f"Received: {line}")
                
                if line == "MODE:IDLE":
                    await broadcast("IDLE")
                elif line == "MODE:CONVENTIONAL":
                    await broadcast("CONVENTIONAL")
                elif line == "MODE:FISH":
                    await broadcast("FISH")
            
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
