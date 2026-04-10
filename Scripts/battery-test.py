"""
battery-test.py — Giromin Battery Life Monitor
================================================
Recebe OSC dos Giromins (UDP 1333), monitora atividade por dispositivo,
detecta reinicializações e fim de bateria, gera CSV de log.

Uso:
    python3 battery-test.py [--osc-port 1333] [--web-port 5001]

Interface web: http://localhost:5001
"""

import argparse
import csv
import os
import threading
import time
from collections import defaultdict
from datetime import datetime

from flask import Flask, jsonify, render_template_string
from pythonosc import dispatcher, osc_server

# ── Configuração ──────────────────────────────────────────────────────────────

TIMEOUT_DEAD   = 10      # s sem mensagem → considera desligado
TIMEOUT_STOP_ALL = 30    # s após todos desligarem → para gravação automática
RESTART_WINDOW = 2.0     # s: gap entre pacotes que indica reinício
TIMELINE_SECS  = 21600   # 6h máximo (escala máxima do frontend) de histórico

LOG_DIR = os.path.join(os.path.dirname(__file__), "battery-logs")
os.makedirs(LOG_DIR, exist_ok=True)

# ── Estado global ─────────────────────────────────────────────────────────────

lock = threading.Lock()

devices  = {}          # device_id → dict de estado
timeline = defaultdict(list)   # device_id → [{active, msgs_this_sec}]

recording       = False
recording_start = None
session_id      = None
all_dead_since  = None
session_notes   = ""
pending_notes   = []   # lista de dicts acumulados durante a sessão
auto_stopped    = False  # sinaliza para o frontend que parou automaticamente

COLORS = ["#e74c3c","#3498db","#2ecc71","#f39c12","#9b59b6","#1abc9c","#e67e22","#e91e8c"]

# Contadores de bundles por segundo (resetados a cada tick)
bundle_this_sec = defaultdict(int)   # device_id → n bundles no segundo atual
# ultimo path visto por device (para agrupar bundles do mesmo instante)
last_bundle_time = {}   # device_id → float (epoch do último bundle contado)
BUNDLE_GAP = 0.005      # 5ms: mensagens dentro desse gap = mesmo bundle

# ── OSC handler ───────────────────────────────────────────────────────────────

def osc_any_handler(address, *args):
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
                "last_seen":    now,
                "first_seen":   now,
                "msg_count":    0,
                "alive":        True,
                "restarts":     0,
                "restart_times": [],
                "dead_since":   None,
                "color":        COLORS[len(devices) % len(COLORS)],
                "msgs_per_sec": 0.0,
            }
        d = devices[device_id]

        # Detectar reinício: só conta se ficou morto por mais de RESTART_WINDOW
        if d["dead_since"] is not None and (now - d["last_seen"]) > RESTART_WINDOW:
            d["restarts"] += 1
            d["restart_times"].append(now)
            d["dead_since"] = None

        d["alive"]     = True
        d["last_seen"] = now

        # Contar bundles (agrupa msgs dentro de BUNDLE_GAP como um bundle)
        prev = last_bundle_time.get(device_id, 0)
        if (now - prev) > BUNDLE_GAP:
            bundle_this_sec[device_id] += 1
            last_bundle_time[device_id] = now

        d["msg_count"] += 1


# ── Tick a cada segundo ───────────────────────────────────────────────────────

def tick_loop():
    global all_dead_since, recording, session_id, auto_stopped

    while True:
        time.sleep(1)
        now = time.time()

        with lock:
            for did, d in devices.items():
                was_alive = d["alive"]
                is_alive  = (now - d["last_seen"]) < TIMEOUT_DEAD
                d["alive"] = is_alive
                if was_alive and not is_alive:
                    d["dead_since"] = now

                bps = bundle_this_sec.pop(did, 0)
                d["msgs_per_sec"] = bps

                # Só acumula timeline durante gravação
                if recording:
                    tl = timeline[did]
                    tl.append({"active": is_alive, "bps": bps})
                    if len(tl) > TIMELINE_SECS:
                        timeline[did] = tl[-TIMELINE_SECS:]

            if recording and devices:
                any_alive = any(d["alive"] for d in devices.values())
                if not any_alive:
                    if all_dead_since is None:
                        all_dead_since = now
                    elif (now - all_dead_since) >= TIMEOUT_STOP_ALL:
                        auto_stopped = True
                        _stop_recording()
                else:
                    all_dead_since = None


def _stop_recording():
    global recording, session_id, all_dead_since
    if not recording:
        return
    recording      = False
    all_dead_since = None
    _save_csv()
    session_id = None


def _csv_path():
    sid = session_id or "unknown"
    return os.path.join(LOG_DIR, f"battery_{sid}.tsv")

def _save_csv():
    """Salva resultados da sessão em TSV (tab-separated)."""
    now      = datetime.now()
    filepath = _csv_path()
    rows = []
    for did, d in devices.items():
        end        = d["dead_since"] if d["dead_since"] else time.time()
        duration_s = round(end - recording_start) if recording_start else None
        rows.append({
            "session_id":       session_id,
            "date":             now.strftime("%Y-%m-%d"),
            "time":             now.strftime("%H:%M:%S"),
            "device_id":        did,
            "recording_start":  datetime.fromtimestamp(recording_start).strftime("%H:%M:%S") if recording_start else "",
            "duration_seconds": duration_s,
            "duration_hms":     _fmt_hms(duration_s),
            "total_messages":   d["msg_count"],
            "restarts":         d["restarts"],
            "restart_times":    ";".join(datetime.fromtimestamp(t).strftime("%H:%M:%S") for t in d["restart_times"]),
            "notas":            "",
        })
    # Adicionar contexto da sessão como primeira linha especial
    if session_notes:
        rows.insert(0, {
            "session_id":       session_id or "",
            "date":             datetime.now().strftime("%Y-%m-%d"),
            "time":             datetime.now().strftime("%H:%M:%S"),
            "device_id":        "contexto",
            "recording_start":  "",
            "duration_seconds": "",
            "duration_hms":     "",
            "total_messages":   "",
            "restarts":         "",
            "restart_times":    "",
            "notas":            session_notes,
        })
    all_rows = rows + pending_notes
    if not all_rows:
        return filepath
    with open(filepath, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=all_rows[0].keys(), delimiter="\t")
        writer.writeheader()
        writer.writerows(all_rows)
    pending_notes.clear()
    print(f"[battery-test] TSV salvo: {filepath}")
    return filepath

def _append_note(note_text):
    """Acumula nota em memória — será escrita junto com os dados no _save_csv."""
    now     = datetime.now()
    elapsed = round(time.time() - recording_start) if recording_start else 0
    pending_notes.append({
        "session_id":       session_id or "",
        "date":             now.strftime("%Y-%m-%d"),
        "time":             now.strftime("%H:%M:%S"),
        "device_id":        "nota",
        "recording_start":  "",
        "duration_seconds": "",
        "duration_hms":     _fmt_hms(elapsed),
        "total_messages":   "",
        "restarts":         "",
        "restart_times":    "",
        "notas":            note_text,
    })
    print(f"[battery-test] Nota acumulada: {note_text[:60]}")


def _fmt_hms(s):
    if s is None:
        return ""
    h = s // 3600; m = (s % 3600) // 60; sec = s % 60
    return f"{h:02d}:{m:02d}:{sec:02d}"


# ── HTML ──────────────────────────────────────────────────────────────────────

HTML = r"""
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<title>Giromin Battery Monitor</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: monospace; background: #111; color: #eee; padding: 20px 24px; }
h1 { font-size: 1.15em; margin-bottom: 14px; color: #999; letter-spacing:.05em; }

#controls { margin-bottom: 22px; display:flex; align-items:flex-start; gap:20px; }
#notes-wrap {
    margin-left:auto; display:flex; flex-direction:row; gap:10px;
    min-width:500px; align-items:flex-start;
}
#notes-editor { display:flex; flex-direction:column; gap:4px; flex:1; }
#notes-wrap label { font-size:.75em; color:#666; }
#notes-input {
    background:#1a1a1a; border:1px solid #333; border-radius:3px 3px 0 0;
    color:#ccc; font-family:monospace; font-size:.82em;
    padding:6px 8px; resize:vertical; min-height:58px; width:100%;
}
#notes-input:focus { outline:none; border-color:#555; }
#notes-actions { display:flex; gap:0; }
#btn-save-note {
    background:#333; color:#aaa; border:1px solid #444; border-top:none;
    border-radius:0 0 0 3px; padding:5px 12px; font-size:.8em;
    font-family:monospace; cursor:pointer; flex:1;
}
#btn-save-note:hover { background:#3a3a3a; color:#eee; }
#btn-cancel-edit {
    display:none; background:#2a2020; color:#e74c3c; border:1px solid #444;
    border-top:none; border-left:none; border-radius:0 0 3px 0;
    padding:5px 10px; font-size:.8em; font-family:monospace; cursor:pointer;
}
#btn-cancel-edit:hover { background:#3a2020; }
#note-saved { font-size:.75em; color:#2ecc71; height:1em; margin-top:2px; }

#notes-list {
    width:200px; max-height:120px; overflow-y:auto;
    border:1px solid #2a2a2a; border-radius:3px; background:#151515;
    flex-shrink:0;
}
.note-item {
    padding:5px 8px; font-size:.75em; color:#888; cursor:pointer;
    border-bottom:1px solid #1e1e1e; white-space:nowrap;
    overflow:hidden; text-overflow:ellipsis;
}
.note-item:hover { background:#1e1e1e; color:#ccc; }
.note-item.active { background:#1a2a1a; color:#2ecc71; }
.note-item .note-time { color:#555; font-size:.85em; margin-right:4px; }
#notes-list-empty { padding:8px; font-size:.75em; color:#444; text-align:center; }
button {
    padding: 9px 26px; font-size: .95em; border: none; border-radius: 4px;
    cursor: pointer; font-family: monospace; letter-spacing:.04em;
}
#btn-record { background: #2ecc71; color: #111; }
#btn-record.recording { background: #e74c3c; color: #fff; }

#global-time { font-size: 1.6em; letter-spacing:.1em; color:#eee; min-width:7ch; }
#stat-rec    { font-size:.82em; color:#888; }

/* ── device rows ── */
#devices { margin-bottom: 28px; }
.dev-row { margin-bottom: 20px; }

.dev-header {
    display: flex; align-items: baseline; gap: 14px;
    margin-bottom: 5px; font-size: .83em;
}
.dev-name   { font-weight: bold; font-size:1em; min-width:9ch; }
.dev-timer  { font-size:1.3em; letter-spacing:.08em; min-width:8ch; color:#ccc; }
.dev-stats  { color: #777; font-size:.9em; }
.led        { display:inline-block; width:9px; height:9px;
              border-radius:50%; margin-right:5px; vertical-align:middle; }
.led.on     { background:#2ecc71; box-shadow:0 0 6px #2ecc71; }
.led.off    { background:#444; }

/* ── global ruler ── */
#ruler-global {
    display: block; width: 100%; height: 24px;
    background: #1a1a1a; border-radius: 2px;
    margin-bottom: 18px;
}

/* ── timeline canvas area ── */
.tl-wrap { height: 30px; }
canvas.tl {
    width: 100%; height: 30px; display: block;
    border-radius: 2px; background: #1a1a1a;
}

/* ── logs ── */
#logs { font-size:.76em; color:#555; }
#logs h2 { color:#777; margin-bottom:6px; font-size:.9em; }
.log-entry { padding:2px 0; border-bottom:1px solid #1e1e1e; }
</style>
</head>
<body>
<h1>⚡ Giromin Battery Monitor</h1>

<div id="controls">
    <button id="btn-record" onclick="toggleRecord()">▶ Iniciar gravação</button>
    <div>
        <div id="global-time">00:00:00</div>
        <div id="stat-rec">aguardando</div>
    </div>
    <div id="notes-wrap">
        <div id="notes-editor">
            <label for="notes-input">Notas (Ctrl+Enter para salvar)</label>
            <textarea id="notes-input" placeholder="Ex: Studio Iara · carga 3h · teste de campo oficina"></textarea>
            <div id="notes-actions">
                <button id="btn-save-note" onclick="saveNote()">💾 Salvar nota</button>
                <button id="btn-cancel-edit" onclick="cancelEdit()">✕ cancelar edição</button>
            </div>
            <div id="note-saved"></div>
        </div>
        <div id="notes-list"><div id="notes-list-empty">sem notas</div></div>
    </div>
</div>

<canvas id="ruler-global"></canvas>
<div id="devices"></div>

<div id="logs">
    <h2>Logs salvos</h2>
    <div id="log-list">—</div>
</div>

<script>
// ── estado cliente ─────────────────────────────────────────────────────────
let recording    = false;
let recStartEpoch = null;   // epoch seconds (do servidor)
// Escala dinâmica: começa em 30min, expande quando elapsed > 2/3 da janela
const SCALES = [1800, 3600, 5400, 7200, 10800, 14400, 21600]; // 30m,1h,1.5h,2h,3h,4h,6h
function getScale(elapsed) {
    for (const s of SCALES) if (elapsed < s * 2/3) return s;
    return SCALES[SCALES.length - 1];
}

// ── helpers ────────────────────────────────────────────────────────────────
function fmtHMS(s) {
    if (s == null || s < 0) s = 0;
    const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), ss = Math.floor(s%60);
    return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(ss).padStart(2,'0')}`;
}

// ── controle ───────────────────────────────────────────────────────────────
let editingIndex = null;  // null = nova nota, number = editando existente

function saveNote() {
    const ta   = document.getElementById('notes-input');
    const note = ta.value.trim();
    if (!note) return;

    if (editingIndex !== null) {
        // Editar nota existente em memória local
        notesList[editingIndex].note = note;
        notesList[editingIndex].edited = true;
        fetch('/api/note_update', {method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify({index: editingIndex, note})});
        editingIndex = null;
        document.getElementById('btn-cancel-edit').style.display = 'none';
    } else {
        fetch('/api/note', {method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify({note})}).then(r=>r.json()).then(d => {
            notesList.push({time: d.time, elapsed: d.elapsed, note});
        });
    }
    ta.value = '';
    const el = document.getElementById('note-saved');
    el.textContent = '✓ salvo às ' + new Date().toLocaleTimeString();
    setTimeout(() => el.textContent = '', 3000);
    renderNotesList();
}

function cancelEdit() {
    editingIndex = null;
    document.getElementById('notes-input').value = '';
    document.getElementById('btn-cancel-edit').style.display = 'none';
    renderNotesList();
}

function editNote(i) {
    editingIndex = i;
    document.getElementById('notes-input').value = notesList[i].note;
    document.getElementById('notes-input').focus();
    document.getElementById('btn-cancel-edit').style.display = 'block';
    renderNotesList();
}

let notesList = [];
let canvasWidths = {};  // id → largura anterior (para evitar flickering)

function renderNotesList() {
    const list = document.getElementById('notes-list');
    if (notesList.length === 0) {
        list.innerHTML = '<div id="notes-list-empty">sem notas</div>';
        return;
    }
    list.innerHTML = notesList.map((n, i) => `
        <div class="note-item ${editingIndex === i ? 'active' : ''}" onclick="editNote(${i})" title="${n.note}">
            <span class="note-time">${n.elapsed}</span>${n.note}
        </div>
    `).join('');
}

// Salvar com Enter (sem Shift) ou Ctrl+Enter
document.addEventListener('keydown', e => {
    if (document.activeElement.id !== 'notes-input') return;
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); saveNote(); }
});

function toggleRecord() {
    if (!recording) {
        const ta    = document.getElementById('notes-input');
        const notes = ta.value;
        fetch('/api/start',{method:'POST',headers:{'Content-Type':'application/json'},
            body:JSON.stringify({notes})}).then(r=>r.json()).then(d=>{
            if (d.ok) {
                recording=true; recStartEpoch=d.start;
                // Preservar o conteúdo da textarea (não limpar se havia texto de contexto)
                // só limpa se o usuário não digitou nada novo além do que virou nota de contexto
                notesList=[]; renderNotesList();
                // Resetar linhas de dispositivos para nova sessão
                document.getElementById('devices').innerHTML = '';
                canvasWidths = {};
            }
        });
    } else {
        fetch('/api/stop',{method:'POST'}).then(r=>r.json()).then(()=>{
            recording=false; recStartEpoch=null;
        });
    }
}

// ── desenho da linha do tempo ──────────────────────────────────────────────
// points: array de {active, bps}, esquerda=mais antigo, direita=mais novo
// TIMELINE_SECS slots, cada slot = 1s
// a canvas tem largura física em px; mapeamos slot → px
function drawTimeline(canvas, points, color, nowEpoch, recStart) {
    const ctx  = canvas.getContext('2d');
    const W    = canvas.width;
    const H    = canvas.height;
    ctx.clearRect(0, 0, W, H);

    if (!recording || !recStart) {
        ctx.fillStyle = color + '44';
        ctx.fillRect(0, H/2 - 1, 4, 2);
        return;
    }

    const elapsed  = nowEpoch - recStart;
    const scale    = getScale(elapsed);
    const pxPerSec = W / scale;
    const n        = points.length;

    for (let i = 0; i < n; i++) {
        const p  = points[i];
        const x  = i * pxPerSec;
        const bw = Math.max(1, pxPerSec);
        ctx.fillStyle = p.active ? color : '#2a2a2a';
        ctx.fillRect(x, 0, bw + 0.5, H);
    }
}

function drawRuler(canvas, nowEpoch, recStart) {
    const ctx = canvas.getContext('2d');
    const W   = canvas.width, H = canvas.height;

    // Fundo
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, W, H);

    if (!recording || !recStart) {
        ctx.fillStyle = '#555';
        ctx.font = '10px monospace';
        ctx.textAlign = 'left';
        ctx.fillText('aguardando gravação...', 8, H / 2 + 4);
        return;
    }

    const elapsed  = Math.max(0, nowEpoch - recStart);
    const scale    = getScale(elapsed);
    const pxPerSec = W / scale;

    const major = scale <= 3600 ? 300 : scale <= 7200 ? 600 : 1800;
    const minor = scale <= 3600 ? 60  : scale <= 7200 ? 120 : 300;

    // Linha de base
    ctx.strokeStyle = '#444';
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0, H - 1); ctx.lineTo(W, H - 1); ctx.stroke();

    ctx.font      = 'bold 10px monospace';
    ctx.textAlign = 'center';

    for (let s = 0; s <= scale; s += minor) {
        const x = s * pxPerSec;
        if (x > W) break;
        const isMajor = (s % major === 0);

        ctx.strokeStyle = isMajor ? '#aaa' : '#555';
        ctx.lineWidth   = isMajor ? 1.5 : 1;
        ctx.beginPath();
        ctx.moveTo(x, isMajor ? 0 : H * 0.55);
        ctx.lineTo(x, H - 1);
        ctx.stroke();

        if (isMajor && x > 20 && x < W - 20) {
            ctx.fillStyle = '#ccc';
            ctx.fillText(fmtHMS(s), x, H * 0.45);
        }
    }

    // Cursor do tempo atual
    const curX = elapsed * pxPerSec;
    if (curX <= W) {
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(curX, 0); ctx.lineTo(curX, H); ctx.stroke();
        ctx.fillStyle = '#fff';
        ctx.textAlign = 'left';
        ctx.fillText(fmtHMS(elapsed), Math.min(curX + 4, W - 55), H * 0.45);
    }
}

// ── update loop ────────────────────────────────────────────────────────────
function update() {
    fetch('/api/state').then(r=>r.json()).then(data => {
        const wasRecording = recording;
        recording     = data.recording;
        recStartEpoch = data.recording_start;
        const nowEpoch = Date.now() / 1000;

        // Auto-stop: mostrar aviso se parou automaticamente
        if (wasRecording && !recording && data.auto_stopped) {
            document.getElementById('stat-rec').textContent = '⚠️ parou automaticamente (todos desligados)';
            document.getElementById('stat-rec').style.color = '#f39c12';
        } else if (!recording) {
            document.getElementById('stat-rec').style.color = '';
        }

        // Botão e status global
        const btn = document.getElementById('btn-record');
        if (recording) {
            btn.textContent = '⏹ Parar gravação';
            btn.classList.add('recording');
            document.getElementById('stat-rec').textContent = '🔴 gravando';
            const elapsed = recStartEpoch ? nowEpoch - recStartEpoch : 0;
            document.getElementById('global-time').textContent = fmtHMS(elapsed);
        } else {
            btn.textContent = '▶ Iniciar gravação';
            btn.classList.remove('recording');
            document.getElementById('stat-rec').textContent = 'aguardando';
            document.getElementById('global-time').textContent = '00:00:00';
        }

        const devs = data.devices;
        const ids  = Object.keys(devs).sort((a,b) => +a - +b);
        const container = document.getElementById('devices');

        ids.forEach(id => {
            const d  = devs[id];
            const tl = data.timeline[id] || [];

            // Cria linha se ainda não existe
            if (!document.getElementById('dev-' + id)) {
                const row = document.createElement('div');
                row.className = 'dev-row';
                row.id = 'dev-' + id;
                row.innerHTML = `
                  <div class="dev-header">
                    <span class="led" id="led-${id}"></span>
                    <span class="dev-name" style="color:${d.color}">Giromin ${id}</span>
                    <span class="dev-timer" id="timer-${id}">00:00:00</span>
                    <span class="dev-stats" id="stats-${id}"></span>
                  </div>
                  <div class="tl-wrap" id="wrap-${id}">
                    <canvas class="tl" id="canvas-${id}"></canvas>
                  </div>
                `;
                container.appendChild(row);
            }

            // Redimensionar canvas apenas quando a largura mudou (evita flickering)
            const wrap   = document.getElementById('wrap-' + id);
            const canvas = document.getElementById('canvas-' + id);
            const pxW    = wrap.clientWidth || 800;
            if (canvasWidths[id] !== pxW) {
                canvas.width = pxW; canvas.height = 30;
                canvasWidths[id] = pxW;
            }

            // LED
            const led = document.getElementById('led-' + id);
            led.className = 'led ' + (d.alive ? 'on' : 'off');

            // Timer individual: usa first_seen do dispositivo, para quando morreu
            let devElapsed = 0;
            if (recording && d.first_seen) {
                const endTime = d.dead_since || nowEpoch;
                devElapsed = Math.max(0, endTime - d.first_seen);
            }
            document.getElementById('timer-' + id).textContent = fmtHMS(devElapsed);

            // Stats
            const bps = d.msgs_per_sec != null ? d.msgs_per_sec.toFixed(0) : '—';
            document.getElementById('stats-' + id).textContent =
                `bundles/s: ${bps}  |  total: ${d.msg_count}  |  reinícios: ${d.restarts}`;

            drawTimeline(canvas, tl, d.color, nowEpoch, recStartEpoch);
        });

        // Régua global (redimensiona só quando necessário)
        const gr = document.getElementById('ruler-global');
        const grW = gr.parentElement.clientWidth || 800;
        if (gr.width !== grW) { gr.width = grW; gr.height = 24; }
        drawRuler(gr, nowEpoch, recStartEpoch);

        // Logs (clicáveis — abre fetch do arquivo)
        if (data.logs && data.logs.length > 0) {
            document.getElementById('log-list').innerHTML =
                data.logs.map(l => `<div class="log-entry">
                    <a href="/api/log/${encodeURIComponent(l)}" target="_blank"
                       style="color:#666;text-decoration:none;"
                       onmouseover="this.style.color='#aaa'"
                       onmouseout="this.style.color='#666'">${l}</a>
                </div>`).join('');
        }
    });
}

// Wake Lock (mantém tela acesa no Chrome)
if (navigator.wakeLock) {
    const lock = async () => { try { await navigator.wakeLock.request('screen'); } catch(e){} };
    lock();
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') lock();
    });
}

update();
setInterval(update, 1000);
</script>
</body>
</html>
"""

# ── Flask ─────────────────────────────────────────────────────────────────────

app = Flask(__name__)

@app.route("/")
def index():
    return render_template_string(HTML)

@app.route("/api/state")
def api_state():
    with lock:
        devs_out = {}
        for did, d in devices.items():
            devs_out[str(did)] = {
                "alive":        d["alive"],
                "msg_count":    d["msg_count"],
                "msgs_per_sec": d["msgs_per_sec"],
                "restarts":     d["restarts"],
                "color":        d["color"],
                "first_seen":   d.get("first_seen"),
                "dead_since":   d["dead_since"],
            }
        tl_out = {str(did): list(tl) for did, tl in timeline.items()}
        logs   = sorted(os.listdir(LOG_DIR), reverse=True)[:10]
        return jsonify({
            "recording":       recording,
            "recording_start": recording_start,
            "devices":         devs_out,
            "timeline":        tl_out,
            "logs":            logs,
            "auto_stopped":    auto_stopped,
        })

@app.route("/api/start", methods=["POST"])
def api_start():
    global recording, recording_start, session_id, all_dead_since, session_notes, auto_stopped
    from flask import request as req
    with lock:
        if recording:
            return jsonify({"ok": False, "reason": "already recording"})
        data = req.get_json(silent=True) or {}
        session_notes = data.get("notes", "")
        now = time.time()
        for d in devices.values():
            d.update({"msg_count":0,"restarts":0,"restart_times":[],"dead_since":None,
                      "msgs_per_sec":0.0,"first_seen":now})
        for did in list(timeline.keys()):
            timeline[did] = []
        bundle_this_sec.clear()
        last_bundle_time.clear()
        pending_notes.clear()
        auto_stopped    = False
        recording       = True
        recording_start = now
        all_dead_since  = None
        session_id      = datetime.now().strftime("%Y%m%d_%H%M%S")
    return jsonify({"ok": True, "start": recording_start})

@app.route("/api/stop", methods=["POST"])
def api_stop():
    global auto_stopped
    with lock:
        _stop_recording()
        auto_stopped = False  # parada manual, não automática
    return jsonify({"ok": True})

@app.route("/api/note", methods=["POST"])
def api_note():
    from flask import request as req
    data = req.get_json(silent=True) or {}
    note = data.get("note", "").strip()
    now = datetime.now()
    elapsed = round(time.time() - recording_start) if recording_start else 0
    if note:
        with lock:
            _append_note(note)
    return jsonify({"ok": True, "time": now.strftime("%H:%M:%S"), "elapsed": _fmt_hms(elapsed)})

@app.route("/api/log/<path:filename>")
def api_log(filename):
    from flask import send_from_directory, abort
    # Segurança: aceita apenas nomes simples (sem barras ou ..)
    if "/" in filename or ".." in filename:
        abort(400)
    filepath = os.path.join(LOG_DIR, filename)
    if not os.path.exists(filepath):
        abort(404)
    return send_from_directory(LOG_DIR, filename, as_attachment=False,
                               mimetype="text/plain; charset=utf-8")

@app.route("/api/note_update", methods=["POST"])
def api_note_update():
    from flask import request as req
    data  = req.get_json(silent=True) or {}
    index = data.get("index")
    note  = data.get("note", "").strip()
    with lock:
        if index is not None and 0 <= index < len(pending_notes):
            pending_notes[index]["notas"] = note
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
    parser.add_argument("--web-port", type=int, default=5001)
    args = parser.parse_args()

    threading.Thread(target=start_osc, args=(args.osc_port,), daemon=True).start()
    threading.Thread(target=tick_loop, daemon=True).start()

    print(f"[battery-test] Interface web em http://localhost:{args.web_port}")
    app.run(host="0.0.0.0", port=args.web_port, debug=False, use_reloader=False)
