import socket
import threading
import tkinter as tk
from tkinter import messagebox, simpledialog

#Configuración
HOST = "localhost"   #Cambiar por nombre de dominio en producción
PORT = 8080

MAP_W = 100          #Dimensiones lógicas del mapa
MAP_H = 100
CANVAS_W = 500       #Dimensiones visuales del canvas
CANVAS_H = 500
CELL = CANVAS_W // MAP_W  #Píxeles por celda = 5

#Recursos críticos conocidos por defensores (el servidor los revela)
RESOURCES = {}       #{resource_id: (x, y)}

#Estado del cliente
state = {
    "username": "",
    "role": "",
    "room_id": -1,
    "x": 0,
    "y": 0,
    "started": False,
    "players": {},   #{username: (x, y)}
}

sock = None
running = True

#Conversión coordenadas lógicas → canvas
def to_canvas(x, y):
    return x * CELL + CELL // 2, y * CELL + CELL // 2

#Enviar mensaje al servidor
def send_cmd(cmd):
    if sock:
        try:
            sock.sendall((cmd + "\r\n").encode())
        except Exception as e:
            log(f"[ERROR] No se pudo enviar: {e}")

#Log en el panel de eventos
def log(msg):
    log_box.config(state="normal")
    log_box.insert("end", msg + "\n")
    log_box.see("end")
    log_box.config(state="disabled")

#Redibujar el mapa
def redraw_map():
    canvas.delete("all")

    #Grid de fondo
    for i in range(0, CANVAS_W, CELL * 10):
        canvas.create_line(i, 0, i, CANVAS_H, fill="#e0e0e0")
        canvas.create_line(0, i, CANVAS_W, i, fill="#e0e0e0")

    #Recursos críticos
    for rid, (rx, ry) in RESOURCES.items():
        cx, cy = to_canvas(rx, ry)
        canvas.create_rectangle(
            cx - 8, cy - 8, cx + 8, cy + 8,
            fill="#ff4444", outline="#cc0000", width=2
        )
        canvas.create_text(cx, cy, text="SRV", fill="white",
                           font=("Courier", 6, "bold"))

    #Otros jugadores
    for uname, (px, py) in state["players"].items():
        if uname == state["username"]:
            continue
        cx, cy = to_canvas(px, py)
        color = "#ff8800" if state["role"] == "DEFENDER" else "#0088ff"
        canvas.create_oval(cx-6, cy-6, cx+6, cy+6,
                           fill=color, outline="white", width=1)
        canvas.create_text(cx, cy + 12, text=uname,
                           fill=color, font=("Courier", 7))

    #Jugador propio (siempre encima)
    if state["username"]:
        cx, cy = to_canvas(state["x"], state["y"])
        color = "#00cc44" if state["role"] == "ATTACKER" else "#4444ff"
        canvas.create_oval(cx-8, cy-8, cx+8, cy+8,
                           fill=color, outline="white", width=2)
        canvas.create_text(cx, cy + 14, text=state["username"],
                           fill=color, font=("Courier", 8, "bold"))

    #Coordenadas actuales
    coord_label.config(
        text=f"Pos: ({state['x']}, {state['y']})  |  "
             f"Rol: {state['role']}  |  Sala: {state['room_id']}"
    )

#Procesador de mensajes del servidor
def process_message(msg):
    msg = msg.strip()
    if not msg:
        return

    log(f"← {msg}")
    parts = msg.split()
    cmd = parts[0]

    if cmd == "WELCOME":
        #WELCOME username role
        state["username"] = parts[1]
        state["role"]     = parts[2]
        state["players"][parts[1]] = (0, 0)
        root.title(f"CDSP — {parts[1]} ({parts[2]})")
        btn_create.config(state="normal")
        btn_list.config(state="normal")
        redraw_map()

    elif cmd == "JOINED":
        #JOINED room_id map_w map_h
        state["room_id"] = int(parts[1])
        btn_join.config(state="disabled")
        btn_create.config(state="disabled")
        log(f"[SALA] Unido a sala {parts[1]}. Esperando jugadores...")
        redraw_map()

    elif cmd == "START":
        state["started"] = True
        log("[JUEGO] ¡La partida comenzó!")
        #Habilitar controles según rol
        if state["role"] == "ATTACKER":
            frame_attack.pack(pady=4)
        else:
            frame_defend.pack(pady=4)
            #El defensor ve los recursos desde el inicio
            for rid, (rx, ry) in RESOURCES.items():
                log(f"[INFO] Recurso crítico {rid} en ({rx},{ry})")
        redraw_map()

    elif cmd == "GAMES":
        #GAMES count [room_id players status] ...
        count = int(parts[1])
        if count == 0:
            log("[LOBBY] No hay partidas activas. Crea una con 'Crear sala'.")
        else:
            log(f"[LOBBY] Partidas activas ({count}):")
            i = 2
            while i + 2 < len(parts):
                log(f"  Sala {parts[i]} — {parts[i+1]} jugadores — {parts[i+2]}")
                i += 3

    elif cmd == "MOVED":
        #MOVED username x y
        uname = parts[1]
        x, y  = int(parts[2]), int(parts[3])
        state["players"][uname] = (x, y)
        if uname == state["username"]:
            state["x"], state["y"] = x, y
        redraw_map()

    elif cmd == "FOUND":
        #FOUND resource_id x y  (solo atacante)
        rid = int(parts[1])
        rx, ry = int(parts[2]), int(parts[3])
        RESOURCES[rid] = (rx, ry)
        log(f"[!] ¡Encontraste recurso crítico {rid} en ({rx},{ry})!")
        btn_attack.config(state="normal")
        entry_attack_id.delete(0, "end")
        entry_attack_id.insert(0, str(rid))
        redraw_map()

    elif cmd == "ALERT":
        #ALERT resource_id x y time_limit
        rid = int(parts[1])
        rx, ry = int(parts[2]), int(parts[3])
        tl = parts[4]
        RESOURCES[rid] = (rx, ry)
        log(f"[ALERTA] ¡Recurso {rid} bajo ataque en ({rx},{ry})! Tienes {tl}s")
        if state["role"] == "DEFENDER":
            btn_defend.config(state="normal")
            entry_defend_id.delete(0, "end")
            entry_defend_id.insert(0, str(rid))
        redraw_map()

    elif cmd == "MITIGATED":
        rid   = parts[1]
        uname = parts[2]
        log(f"[✓] Recurso {rid} mitigado por {uname}")
        btn_defend.config(state="disabled")

    elif cmd == "BREACH":
        rid = parts[1]
        log(f"[✗] ¡Recurso {rid} comprometido! Tiempo agotado.")

    elif cmd == "GAMEOVER":
        winner   = parts[1]
        sc_att   = parts[2]
        sc_def   = parts[3]
        log(f"[FIN] Ganó: {winner} | Atacante: {sc_att} | Defensor: {sc_def}")
        messagebox.showinfo("Fin de partida",
                            f"Ganador: {winner}\n"
                            f"Atacante: {sc_att} pts\n"
                            f"Defensor: {sc_def} pts")

    elif cmd == "ERR":
        log(f"[ERR {parts[1]}] {' '.join(parts[2:])}")

    elif cmd == "BYE":
        log("[INFO] Desconectado del servidor.")

#Hilo receptor de mensajes
def receiver():
    buffer = ""
    while running:
        try:
            data = sock.recv(1024).decode(errors="replace")
            if not data:
                log("[INFO] Conexión cerrada por el servidor.")
                break
            buffer += data
            # Los mensajes terminan en \r\n
            while "\r\n" in buffer:
                line, buffer = buffer.split("\r\n", 1)
                root.after(0, process_message, line)
        except Exception:
            break

#Acciones de la GUI
def do_login():
    username = entry_user.get().strip()
    password = entry_pass.get().strip()
    if not username or not password:
        messagebox.showwarning("Login", "Ingresa usuario y contraseña.")
        return
    send_cmd(f"HELLO {username} {password}")
    btn_login.config(state="disabled")
    entry_user.config(state="disabled")
    entry_pass.config(state="disabled")

def do_list():
    send_cmd("LIST")

def do_create():
    send_cmd("CREATE")

def do_join():
    room = simpledialog.askstring("Unirse", "ID de sala:")
    if room:
        send_cmd(f"JOIN {room}")

def do_move(dx, dy):
    nx = max(0, min(MAP_W - 1, state["x"] + dx))
    ny = max(0, min(MAP_H - 1, state["y"] + dy))
    send_cmd(f"MOVE {nx} {ny}")

def do_attack():
    rid = entry_attack_id.get().strip()
    if rid:
        send_cmd(f"ATTACK {rid}")
        btn_attack.config(state="disabled")

def do_defend():
    rid = entry_defend_id.get().strip()
    if rid:
        send_cmd(f"DEFEND {rid}")

#Teclado para mover
def on_key(event):
    if not state["started"]:
        return
    mapping = {
        "Up":    (0, -1), "w": (0, -1),
        "Down":  (0,  1), "s": (0,  1),
        "Left":  (-1, 0), "a": (-1, 0),
        "Right": (1,  0), "d": (1,  0),
    }
    if event.keysym in mapping:
        dx, dy = mapping[event.keysym]
        do_move(dx, dy)

#Construir la GUI
root = tk.Tk()
root.title("CDSP Client")
root.resizable(False, False)

#Frame izquierdo: mapa + coords
frame_left = tk.Frame(root, bg="#1a1a2e")
frame_left.pack(side="left", padx=8, pady=8)

canvas = tk.Canvas(frame_left, width=CANVAS_W, height=CANVAS_H,
                   bg="#1a1a2e", highlightthickness=1,
                   highlightbackground="#444")
canvas.pack()

coord_label = tk.Label(frame_left, text="Pos: (0, 0)  |  Rol: —  |  Sala: —",
                       bg="#1a1a2e", fg="#aaaaaa", font=("Courier", 9))
coord_label.pack(pady=4)

#Instrucciones de movimiento
tk.Label(frame_left, text="Mover: WASD o flechas",
         bg="#1a1a2e", fg="#666666", font=("Courier", 8)).pack()

#Frame derecho: controles
frame_right = tk.Frame(root, bg="#16213e", padx=10, pady=10)
frame_right.pack(side="right", fill="y", padx=8, pady=8)

#Login
tk.Label(frame_right, text="Login", bg="#16213e", fg="white",
         font=("Courier", 11, "bold")).pack(pady=(0,4))

tk.Label(frame_right, text="Usuario:", bg="#16213e",
         fg="#aaaaaa", font=("Courier", 9)).pack(anchor="w")
entry_user = tk.Entry(frame_right, font=("Courier", 10), width=18)
entry_user.pack()
entry_user.insert(0, "user1")

tk.Label(frame_right, text="Contraseña:", bg="#16213e",
         fg="#aaaaaa", font=("Courier", 9)).pack(anchor="w", pady=(4,0))
entry_pass = tk.Entry(frame_right, font=("Courier", 10),
                      width=18, show="*")
entry_pass.pack()
entry_pass.insert(0, "password123")

btn_login = tk.Button(frame_right, text="Conectar",
                      font=("Courier", 10, "bold"),
                      bg="#0f3460", fg="white", relief="flat",
                      command=do_login)
btn_login.pack(pady=6, fill="x")

tk.Frame(frame_right, bg="#333", height=1).pack(fill="x", pady=6)

#Lobby
tk.Label(frame_right, text="Lobby", bg="#16213e", fg="white",
         font=("Courier", 11, "bold")).pack(pady=(0,4))

btn_list = tk.Button(frame_right, text="Listar partidas",
                     font=("Courier", 9), bg="#0f3460", fg="white",
                     relief="flat", state="disabled", command=do_list)
btn_list.pack(fill="x", pady=2)

btn_create = tk.Button(frame_right, text="Crear sala",
                       font=("Courier", 9), bg="#0f3460", fg="white",
                       relief="flat", state="disabled", command=do_create)
btn_create.pack(fill="x", pady=2)

btn_join = tk.Button(frame_right, text="Unirse a sala",
                     font=("Courier", 9), bg="#0f3460", fg="white",
                     relief="flat", state="disabled", command=do_join)
btn_join.pack(fill="x", pady=2)
btn_list.config(command=lambda: [do_list(), btn_join.config(state="normal")])

tk.Frame(frame_right, bg="#333", height=1).pack(fill="x", pady=6)

#Controles atacante (ocultos hasta START)
frame_attack = tk.Frame(frame_right, bg="#16213e")
tk.Label(frame_attack, text="Atacar recurso ID:",
         bg="#16213e", fg="#ff6666", font=("Courier", 9)).pack(anchor="w")
entry_attack_id = tk.Entry(frame_attack, font=("Courier", 10), width=6)
entry_attack_id.pack(side="left", padx=(0,4))
btn_attack = tk.Button(frame_attack, text="ATTACK",
                       font=("Courier", 9, "bold"),
                       bg="#cc0000", fg="white", relief="flat",
                       state="disabled", command=do_attack)
btn_attack.pack(side="left")

#Controles defensor (ocultos hasta START)
frame_defend = tk.Frame(frame_right, bg="#16213e")
tk.Label(frame_defend, text="Defender recurso ID:",
         bg="#16213e", fg="#6666ff", font=("Courier", 9)).pack(anchor="w")
entry_defend_id = tk.Entry(frame_defend, font=("Courier", 10), width=6)
entry_defend_id.pack(side="left", padx=(0,4))
btn_defend = tk.Button(frame_defend, text="DEFEND",
                       font=("Courier", 9, "bold"),
                       bg="#0000cc", fg="white", relief="flat",
                       state="disabled", command=do_defend)
btn_defend.pack(side="left")

tk.Frame(frame_right, bg="#333", height=1).pack(fill="x", pady=6)

#Log de eventos
tk.Label(frame_right, text="Eventos", bg="#16213e", fg="white",
         font=("Courier", 11, "bold")).pack(pady=(0,2))
log_box = tk.Text(frame_right, width=28, height=16,
                  font=("Courier", 8), bg="#0d0d1a", fg="#00ff88",
                  state="disabled", relief="flat")
log_box.pack()

#Conectar al servidor al arrancar
try:
    sock = socket.create_connection((HOST, PORT))
    log(f"[INFO] Conectado a {HOST}:{PORT}")
except Exception as e:
    messagebox.showerror("Error", f"No se pudo conectar:\n{e}")
    sock = None

#Arrancar hilo receptor
if sock:
    t = threading.Thread(target=receiver, daemon=True)
    t.start()

#Capturar teclado para movimiento
root.bind("<KeyPress>", on_key)

#Cerrar limpiamente
def on_close():
    global running
    running = False
    if sock:
        send_cmd("QUIT")
        sock.close()
    root.destroy()

root.protocol("WM_DELETE_WINDOW", on_close)
root.mainloop()