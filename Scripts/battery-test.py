"""
battery-test.py — Giromin Battery Life Monitor
================================================
Recebe OSC dos Giromins (UDP 1333), monitora atividade por dispositivo,
detecta reinicializações e fim de bateria, gera CSV de log.

Uso:
    python3 battery-test.py [--osc-port 1333] [--web-port 5000]

Interface web: http://localhost:5000
"""

import argparse
import csv
import json
import os
import threading
import time
from collections import defaultdict
from datetime import datetime, timezone

from flask import Flask, Response, jsonify, render_template_string
from pythonosc import dispatcher, osc_server

# ── Configuração ──────────────────────────────────────────────────────────────

TIMEOUT_DEAD = 10      # segundos sem mensagem → considera desligado
TIMEOUT_STOP_ALL = 30  # segundos após todos desligarem → para gravação
RESTART_WINDOW = 2.0   # segundos: se dois pacotes chegam com menos disso após silêncio = reinício
LOG_DIR = os.path.join(os.path.dirname(__file__), "battery-logs")
os.makedirs(LOG_DIR, exist_ok=True)

# ── Estado global ─────────────────────────────────────────────────────────────

lock = threading.Lock()

# Por device_id: {"last_seen": float, "msg_count": int, "alive": bool,
#                 "restarts": int, "restart_times": [], "first_seen": float,
#                 "dead_since": float|None, "seq_count": int, "last_seq": int}
devices = {}

# Histórico por segundo para a linha do tempo (últimos 600s = 10min)
TIMELINE_SECONDS = 600
# Por device_id: lista de {"t": epoch, "active": bool, "msgs": int}
timeline = defaultdict(list)

recording = False
recording_start = None
session_id = None
all_dead_since = None  # timestamp em que todos ficaram mortos

COLORS = ["#e74c3c","#3498db","#2ecc71","#f39c12","#9b59b6","#1abc9c","#e67e22","#34495e"]
device_colors = {}

# ── OSC handlers ─────────────────────────────────────────────────────────────

def osc_any_handler(address, *args):
    """Chamado para qualquer mensagem OSC."""
    # Extrair device ID do path: /giromin/{id}/...
    parts = address.strip("/").split("/")
    if len(parts) < 2 or parts[0] != "giromin":
        return
    try:
        device_id = int(parts[1])
    except ValueError:
        return

    now = time.time()
    with lock:
        if device_id not in devices:
            devices[device_id] = {
                "last_seen": now,
                "first_seen": now,
                "msg_count": 0,
                "alive": True,
                "restarts": 0,
                "restart_times": [],
                "dead_since": None,
                "color": COLORS[len(devices) % len(COLORS)],
            }
        d = devices[device_id]

        # Detectar reinício: estava morto e voltou
        if not d["alive"] or (d["dead_since"] and (now - d["last_seen"]) > RESTART_WINDOW):
            if d["dead_since"] is not None:
                d["restarts"] += 1
                d["restart_times"].append(now)
                d["dead_since"] = None

        d["alive"] = True
        d["last_seen"] = now
        d["msg_count"] += 1


# ── Thread de tick a cada segundo ────────────────────────────────────────────

def tick_loop():
    global all_dead_since, recording, session_id

    while True:
        time.sleep(1)
        now = time.time()

        with lock:
            # Atualizar estado alive/dead de cada device
            for did, d in devices.items():
                was_alive = d["alive"]
                is_alive = (now - d["last_seen"]) < TIMEOUT_DEAD
                d["alive"] = is_alive
                if was_alive and not is_alive:
                    d["dead_since"] = now

                # Adicionar ponto na linha do tempo
                tl = timeline[did]
                tl.append({"t": now, "active": is_alive})
                # Manter apenas os últimos TIMELINE_SECONDS pontos
                if len(tl) > TIMELINE_SECONDS:
                    timeline[did] = tl[-TIMELINE_SECONDS:]

            # Verificar se todos morreram
            if recording and devices:
                any_alive = any(d["alive"] for d in devices.values())
                if not any_alive:
                    if all_dead_since is None:
                        all_dead_since = now
                    elif (now - all_dead_since) >= TIMEOUT_STOP_ALL:
                        # Para gravação automaticamente
                        _stop_recording()
                else:
                    all_dead_since = None


def _stop_recording():
    """Chamado com lock adquirido. Para a gravação e salva CSV."""
    global recording, session_id, all_dead_since
    if not recording:
        return

    recording = False
    all_dead_since = None
    _save_csv()
    session_id = None


def _save_csv():
    """Salva resultados da sessão em CSV."""
    now = datetime.now()
    filename = f"battery_{now.strftime('%Y%m%d_%H%M%S')}.csv"
    filepath = os.path.join(LOG_DIR, filename)

    rows = []
    for did, d in devices.items():
        duration_s = None
        if d["first_seen"] and recording_start:
            end = d["dead_since"] if d["dead_since"] else time.time()
            duration_s = round(end - recording_start)

        rows.append({
            "session_id": session_id,
            "date": now.strftime("%Y-%m-%d"),
            "time": now.strftime("%H:%M:%S"),
            "device_id": did,
            "recording_start": datetime.fromtimestamp(recording_start).strftime("%H:%M:%S") if recording_start else "",
            "duration_seconds": duration_s,
            "duration_hms": _fmt_duration(duration_s) if duration_s else "",
            "total_messages": d["msg_count"],
            "restarts": d["restarts"],
            "restart_times": ";".join(
                datetime.fromtimestamp(t).strftime("%H:%M:%S") for t in d["restart_times"]
            ),
        })

    with open(filepath, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys() if rows else [])
        writer.writeheader()
        writer.writerows(rows)

    print(f"[battery-test] CSV salvo: {filepath}")


def _fmt_duration(seconds):
    if seconds is None:
        return ""
    h = seconds // 3600
    m = (seconds % 3600) // 60
    s = seconds % 60
    return f"{h:02d}:{m:02d}:{s:02d}"


# ── Flask web ─────────────────────────────────────────────────────────────────

app = Flask(__name__)

HTML = r"""
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta http-equiv="refresh" content="0">
<title>Giromin Battery Monitor</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: monospace; background: #111; color: #eee; padding: 20px; }
h1 { font-size: 1.2em; margin-bottom: 16px; color: #aaa; }
#status { margin-bottom: 20px; font-size: 0.9em; color: #888; }
#status span { color: #eee; }
#controls { margin-bottom: 24px; }
button {
    padding: 10px 28px; font-size: 1em; border: none; border-radius: 4px;
    cursor: pointer; font-family: monospace;
}
#btn-record { background: #2ecc71; color: #111; }
#btn-record.recording { background: #e74c3c; color: #fff; }
#btn-record:disabled { background: #444; color: #888; cursor: default; }
#devices { margin-bottom: 24px; }
.device-row { margin-bottom: 18px; }
.device-label {
    font-size: 0.85em; margin-bottom: 4px;
    display: flex; justify-content: space-between;
}
.device-label .name { font-weight: bold; }
.device-label .info { color: #888; font-size: 0.9em; }
canvas.timeline {
    width: 100%; height: 28px; display: block;
    border-radius: 3px; background: #222;
}
#logs { font-size: 0.78em; color: #666; }
#logs h2 { color: #888; margin-bottom: 8px; font-size: 0.9em; }
.log-entry { padding: 3px 0; border-bottom: 1px solid #222; }
</style>
</head>
<body>
<h1>⚡ Giromin Battery Monitor</h1>
<div id="status">
    Status: <span id="stat-rec">aguardando</span> &nbsp;|&nbsp;
    Dispositivos ativos: <span id="stat-devices">0</span> &nbsp;|&nbsp;
    Tempo gravando: <span id="stat-time">—</span>
</div>
<div id="controls">
    <button id="btn-record" onclick="toggleRecord()">▶ Iniciar gravação</button>
</div>
<div id="devices"></div>
<div id="logs">
    <h2>Logs salvos</h2>
    <div id="log-list">—</div>
</div>

<script>
let state = {};
let recording = false;
let recStart = null;
const TIMELINE_W = 600;

function toggleRecord() {
    const btn = document.getElementById('btn-record');
    if (!recording) {
        fetch('/api/start', {method:'POST'}).then(r=>r.json()).then(d=>{
            if (d.ok) { recording = true; recStart = Date.now()/1000; }
        });
    } else {
        fetch('/api/stop', {method:'POST'}).then(r=>r.json()).then(d=>{
            recording = false; recStart = null;
        });
    }
}

function fmtDuration(s) {
    if (!s && s !== 0) return '—';
    const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), ss = Math.floor(s%60);
    return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(ss).padStart(2,'0')}`;
}

function drawTimeline(canvas, points, color) {
    const ctx = canvas.getContext('2d');
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    if (!points || points.length === 0) return;
    const n = points.length;
    const barW = Math.max(1, w / TIMELINE_W);
    points.forEach((p, i) => {
        const x = w - (n - i) * barW;
        ctx.fillStyle = p.active ? color : '#333';
        ctx.fillRect(x, 0, barW + 0.5, h);
    });
}

function update() {
    fetch('/api/state').then(r=>r.json()).then(data => {
        recording = data.recording;
        const btn = document.getElementById('btn-record');
        if (recording) {
            btn.textContent = '⏹ Parar gravação';
            btn.className = 'recording';
            document.getElementById('stat-rec').textContent = '🔴 gravando';
            if (data.recording_start) {
                const elapsed = Date.now()/1000 - data.recording_start;
                document.getElementById('stat-time').textContent = fmtDuration(elapsed);
            }
        } else {
            btn.textContent = '▶ Iniciar gravação';
            btn.className = '';
            document.getElementById('stat-rec').textContent = 'aguardando';
            document.getElementById('stat-time').textContent = '—';
        }

        const devs = data.devices;
        const ids = Object.keys(devs);
        document.getElementById('stat-devices').textContent =
            ids.filter(id => devs[id].alive).length + ' / ' + ids.length;

        const container = document.getElementById('devices');
        ids.forEach(id => {
            const d = devs[id];
            const tl = data.timeline[id] || [];
            let row = document.getElementById('dev-' + id);
            if (!row) {
                row = document.createElement('div');
                row.className = 'device-row';
                row.id = 'dev-' + id;
                row.innerHTML = `
                    <div class="device-label">
                        <span class="name" style="color:${d.color}">Giromin ${id}</span>
                        <span class="info" id="info-${id}"></span>
                    </div>
                    <canvas class="timeline" id="canvas-${id}" width="${TIMELINE_W}" height="28"></canvas>
                `;
                container.appendChild(row);
            }
            const duration = recording && data.recording_start
                ? Math.floor(Date.now()/1000 - data.recording_start) : null;
            const alive = d.alive ? '🟢 ativo' : '🔴 inativo';
            document.getElementById('info-' + id).textContent =
                `${alive} | msgs: ${d.msg_count} | reinícios: ${d.restarts}` +
                (duration !== null ? ` | ${fmtDuration(duration)}` : '');

            const canvas = document.getElementById('canvas-' + id);
            drawTimeline(canvas, tl, d.color);
        });

        // Logs
        const logList = document.getElementById('log-list');
        if (data.logs && data.logs.length > 0) {
            logList.innerHTML = data.logs.map(l =>
                `<div class="log-entry">${l}</div>`
            ).join('');
        }
    });
}

// Prevenir sleep via Web Lock API (Chrome)
if (navigator.wakeLock) {
    async function requestWakeLock() {
        try {
            await navigator.wakeLock.request('screen');
        } catch(e) { console.warn('Wake lock negado:', e); }
    }
    requestWakeLock();
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') requestWakeLock();
    });
}

update();
setInterval(update, 1000);
</script>
</body>
</html>
"""

@app.route("/")
def index():
    return render_template_string(HTML)


@app.route("/api/state")
def api_state():
    with lock:
        now = time.time()
        devs_out = {}
        for did, d in devices.items():
            devs_out[str(did)] = {
                "alive": d["alive"],
                "msg_count": d["msg_count"],
                "restarts": d["restarts"],
                "color": d["color"],
                "dead_since": d["dead_since"],
            }
        tl_out = {str(did): list(tl) for did, tl in timeline.items()}

        logs = sorted(os.listdir(LOG_DIR), reverse=True)[:10]

        return jsonify({
            "recording": recording,
            "recording_start": recording_start,
            "devices": devs_out,
            "timeline": tl_out,
            "logs": logs,
        })


@app.route("/api/start", methods=["POST"])
def api_start():
    global recording, recording_start, session_id, all_dead_since
    with lock:
        if recording:
            return jsonify({"ok": False, "reason": "already recording"})
        # Resetar estado dos devices para nova sessão
        for d in devices.values():
            d["msg_count"] = 0
            d["restarts"] = 0
            d["restart_times"] = []
            d["first_seen"] = time.time()
            d["dead_since"] = None
        for did in list(timeline.keys()):
            timeline[did] = []
        recording = True
        recording_start = time.time()
        all_dead_since = None
        session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    return jsonify({"ok": True})


@app.route("/api/stop", methods=["POST"])
def api_stop():
    with lock:
        _stop_recording()
    return jsonify({"ok": True})


# ── Main ──────────────────────────────────────────────────────────────────────

def start_osc(port):
    d = dispatcher.Dispatcher()
    d.set_default_handler(osc_any_handler)
    server = osc_server.ThreadingOSCUDPServer(("0.0.0.0", port), d)
    print(f"[battery-test] OSC escutando na porta {port}")
    server.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--osc-port", type=int, default=1333)
    parser.add_argument("--web-port", type=int, default=5000)
    args = parser.parse_args()

    # Thread OSC
    osc_thread = threading.Thread(target=start_osc, args=(args.osc_port,), daemon=True)
    osc_thread.start()

    # Thread de tick
    tick_thread = threading.Thread(target=tick_loop, daemon=True)
    tick_thread.start()

    print(f"[battery-test] Interface web em http://localhost:{args.web_port}")
    app.run(host="0.0.0.0", port=args.web_port, debug=False, use_reloader=False)
