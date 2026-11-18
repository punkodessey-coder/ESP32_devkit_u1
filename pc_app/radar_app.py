import socket, threading, collections, statistics, tkinter as tk

class App:
    def __init__(self):
        self.win = tk.Tk(); self.win.title("ESP32 CSI Radar")
        self.status = tk.Label(self.win, text="Ожидание данных..."); self.status.pack(pady=10)
        self.score = tk.Label(self.win, text="Движение: --", font=("Arial", 24)); self.score.pack(pady=10)
        self.log = tk.Text(self.win, height=12, width=60); self.log.pack(padx=10, pady=10)
        self.hist = collections.deque(maxlen=100)
        threading.Thread(target=self.listen, daemon=True).start()

    def listen(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("0.0.0.0", 9999))
        while True:
            data, addr = s.recvfrom(2048)
            try:
                rssi = int(data.split(b'"rssi":')[1].split(b',')[0])
            except Exception:
                continue
            self.hist.append(rssi)
            if len(self.hist) > 20:
                mean = statistics.mean(self.hist); sd = statistics.pstdev(self.hist) or 1
                z = abs(rssi - mean) / sd
                state = "ДВИЖЕНИЕ" if z > 2.5 else "ТИХО"
                self.win.after(0, lambda s=state, z=z: self.update(s, z))

    def update(self, state, z):
        self.status.config(text=f"Источник: UDP :9999, пакетов: {len(self.hist)}")
        self.score.config(text=f"{state} (z={z:.2f})")
        self.log.insert("1.0", f"{state} z={z:.2f}\n"); self.log.delete("200.0", "end")

App().mainloop()
