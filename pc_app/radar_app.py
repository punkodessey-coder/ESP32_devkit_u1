import socket, threading, collections, statistics, tkinter as tk

RADAR_IP = "192.168.1.23"
RADAR_PORT = 9999

class App:
    def __init__(self):
        self.win = tk.Tk(); self.win.title("ESP32 CSI Radar")
        self.status = tk.Label(self.win, text=f"Подключение к {RADAR_IP}:{RADAR_PORT}...")
        self.status.pack(pady=10)
        self.score = tk.Label(self.win, text="Движение: --", font=("Arial", 24)); self.score.pack(pady=10)
        self.log = tk.Text(self.win, height=12, width=60); self.log.pack(padx=10, pady=10)
        self.hist = collections.deque(maxlen=100)
        threading.Thread(target=self.listen, daemon=True).start()

    def listen(self):
        while True:
            try:
                s = socket.create_connection((RADAR_IP, RADAR_PORT), timeout=5)
                self.win.after(0, lambda: self.status.config(text=f"Подключено: {RADAR_IP}:{RADAR_PORT}"))
                buf = b""
                while True:
                    part = s.recv(4096)
                    if not part:
                        break
                    buf += part
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        try:
                            rssi = int(line.split(b'"rssi":')[1].split(b',')[0])
                        except Exception:
                            continue
                        self.hist.append(rssi)
                        if len(self.hist) > 20:
                            mean = statistics.mean(self.hist)
                            sd = statistics.pstdev(self.hist) or 1
                            z = abs(rssi - mean) / sd
                            state = "ДВИЖЕНИЕ" if z > 2.5 else "ТИХО"
                            self.win.after(0, lambda s=state, z=z: self.update(s, z))
            except Exception:
                self.win.after(0, lambda: self.status.config(text=f"Нет связи с {RADAR_IP}:{RADAR_PORT}; повтор через 3 с"))
            import time; time.sleep(3)

    def update(self, state, z):
        self.status.config(text=f"Подключено: {RADAR_IP}:{RADAR_PORT}, пакетов: {len(self.hist)}")
        self.score.config(text=f"{state} (z={z:.2f})")
        self.log.insert("1.0", f"{state} z={z:.2f}\n"); self.log.delete("200.0", "end")

App().mainloop()
