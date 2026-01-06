# -*- coding: utf-8 -*-
import os
import json
import time
import sqlite3
import threading
from datetime import datetime, timedelta
from flask import Flask, request, jsonify, render_template_string
import paho.mqtt.client as mqtt

MQTT_HOST = os.getenv("MQTT_HOST", "1.14.163.35")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")
MQTT_CMD_TOPIC = "stm32/cmd"
MQTT_SUB_TOPIC = "server/#"
DB_PATH = os.getenv("DB_PATH", "seat_system.db")

TOF_OCCUPIED_MM = int(os.getenv("TOF_OCCUPIED_MM", "380"))

DEFAULT_SEATS = [
    ("A01", "A区-01号"), ("A02", "A区-02号"), ("A03", "A区-03号"), ("A04", "A区-04号"),
    ("A05", "A区-05号"), ("A06", "A区-06号"), ("A07", "A区-07号"), ("A08", "A区-08号"),
    ("A09", "A区-09号"), ("A10", "A区-10号"), ("A11", "A区-11号"), ("A12", "A区-12号"),
    ("A13", "A区-13号"), ("A14", "A区-14号"), ("A15", "A区-15号"), ("A16", "A区-16号"),
    ("A17", "A区-17号"), ("A18", "A区-18号"), ("A19", "A区-19号"), ("A20", "A区-20号"),
]

SEAT_FREE = "FREE"
SEAT_RESERVED = "RESERVED"
SEAT_IN_USE = "IN_USE"

RES_ACTIVE = "ACTIVE"
RES_IN_USE = "IN_USE"
RES_DONE = "DONE"
RES_CANCEL = "CANCEL"
RES_EXPIRED = "EXPIRED"

app = Flask(__name__)
lock = threading.Lock()


# ===================== DB 工具 =====================
def db():
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = db()
    c = conn.cursor()

    c.execute("""
    CREATE TABLE IF NOT EXISTS seats(
        seat_id TEXT PRIMARY KEY,
        display TEXT NOT NULL,
        state TEXT NOT NULL,
        updated_at TEXT NOT NULL
    )
    """)

    c.execute("""
    CREATE TABLE IF NOT EXISTS telemetry(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        seat_id TEXT,
        temp REAL,
        humi REAL,
        lux INTEGER,
        tof_mm INTEGER,
        object_present INTEGER,
        created_at TEXT NOT NULL
    )
    """)

    c.execute("""
    CREATE TABLE IF NOT EXISTS reservations(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        seat_id TEXT NOT NULL,
        user TEXT NOT NULL,
        status TEXT NOT NULL,
        uid TEXT,
        reserved_at TEXT NOT NULL,
        expires_at TEXT NOT NULL,
        checkin_at TEXT,
        checkout_at TEXT
    )
    """)

    c.execute("""
    CREATE TABLE IF NOT EXISTS occupy_incidents(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        seat_id TEXT NOT NULL,
        opened_at TEXT NOT NULL,
        closed_at TEXT,
        last_tof_mm INTEGER
    )
    """)

    c.execute("""
    CREATE TABLE IF NOT EXISTS users(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        uid TEXT,
        created_at TEXT
    )
    """)

    for sid, disp in DEFAULT_SEATS:
        c.execute("SELECT seat_id FROM seats WHERE seat_id=?", (sid,))
        if not c.fetchone():
            c.execute(
                "INSERT INTO seats(seat_id, display, state, updated_at) VALUES(?,?,?,?)",
                (sid, disp, SEAT_FREE, now_str())
            )

    conn.commit()
    conn.close()


def now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def cleanup_expired_reservations():
    conn = db()
    c = conn.cursor()
    now = datetime.now()

    c.execute("SELECT * FROM reservations WHERE status=?", (RES_ACTIVE,))
    rows = c.fetchall()
    for r in rows:
        exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")
        if now > exp:
            c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))
            c.execute("SELECT state FROM seats WHERE seat_id=?", (r["seat_id"],))
            seat = c.fetchone()
            if seat and seat["state"] == SEAT_RESERVED:
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), r["seat_id"]))
                # 通知 STM32 释放
                mqtt_publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reason": "expired"})

    conn.commit()
    conn.close()


# ===================== MQTT =====================
mqtt_client = mqtt.Client()


def mqtt_publish_cmd(payload: dict):
    """
    修改：将字典转换为 STM32 main.c 能识别的 key=value&key=value 字符串格式
    """
    data = payload.copy()

    # 1. 映射命令字段：Server用 "cmd", STM32用 "type"
    if "cmd" in data:
        data["type"] = data.pop("cmd")

    # 2. 特殊命令映射：Server "cancel" -> STM32 "release"
    if data.get("type") == "cancel":
        data["type"] = "release"

    # 3. 拼接字符串
    parts = []
    for k, v in data.items():
        if v is not None:
            parts.append(f"{k}={v}")

    msg_str = "&".join(parts)
    print(f"[MQTT] Sending to {MQTT_CMD_TOPIC}: {msg_str}")
    mqtt_client.publish(MQTT_CMD_TOPIC, msg_str, qos=0, retain=False)


def parse_payload(raw: bytes):
    try:
        txt = raw.decode("utf-8", errors="ignore").strip()
    except Exception:
        txt = str(raw)

    if not txt:
        return {}

    if txt.startswith("{") and txt.endswith("}"):
        try:
            return json.loads(txt)
        except Exception:
            pass

    data = {}
    parts = txt.split("&")
    for p in parts:
        if "=" in p:
            k, v = p.split("=", 1)
            data[k.strip()] = v.strip()
    return data


def object_present_from_tof(tof_mm):
    if tof_mm is None:
        return None
    try:
        v = int(tof_mm)
    except Exception:
        return None
    if v <= 0:
        return None
    return 1 if v < TOF_OCCUPIED_MM else 0


def on_mqtt_connect(client, userdata, flags, rc):
    client.subscribe(MQTT_SUB_TOPIC, qos=0)


def on_mqtt_message(client, userdata, msg):
    data = parse_payload(msg.payload)
    if not data:
        return

    tp = data.get("type")
    topic = msg.topic or ""

    if not tp:
        if "rfid" in topic:
            tp = "rfid"
        elif "telemetry" in topic or "sensor" in topic:
            tp = "telemetry"
        else:
            tp = data.get("cmd") or "unknown"

    with lock:
        cleanup_expired_reservations()

        if tp == "telemetry":
            handle_telemetry(data)
        elif tp == "rfid":
            handle_rfid(data)
        else:
            if "uid" in data and "seat_id" in data:
                handle_rfid(data)
            elif any(k in data for k in ("temp", "humi", "lux", "tof_mm")):
                handle_telemetry(data)


def handle_telemetry(d):
    seat_id = str(d.get("seat_id", "")).strip() or None
    temp = d.get("temp", None)
    humi = d.get("humi", None)
    lux = d.get("lux", None)
    tof = d.get("tof_mm", None)

    obj = d.get("object_present", None)
    if obj is None:
        obj = object_present_from_tof(tof)

    conn = db()
    c = conn.cursor()
    c.execute("""
        INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at)
        VALUES(?,?,?,?,?,?,?)
    """, (seat_id, temp, humi, lux, tof, obj, now_str()))
    conn.commit()

    if seat_id:
        c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        seat_state = seat["state"] if seat else None

        if obj == 1 and seat_state == SEAT_FREE:
            c.execute("""
                SELECT * FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                ORDER BY id DESC LIMIT 1
            """, (seat_id,))
            inc = c.fetchone()
            if inc:
                c.execute("UPDATE occupy_incidents SET last_tof_mm=? WHERE id=?",
                          (tof if tof is not None else inc["last_tof_mm"], inc["id"]))
            else:
                c.execute("""
                    INSERT INTO occupy_incidents(seat_id, opened_at, closed_at, last_tof_mm)
                    VALUES(?,?,NULL,?)
                """, (seat_id, now_str(), tof))
                mqtt_publish_cmd({
                    "cmd": "occupy_warn",
                    "seat_id": seat_id
                })
        else:
            c.execute("""
                SELECT * FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                ORDER BY id DESC LIMIT 1
            """, (seat_id,))
            inc = c.fetchone()
            if inc and obj == 0:
                c.execute("UPDATE occupy_incidents SET closed_at=? WHERE id=?",
                          (now_str(), inc["id"]))

    conn.commit()
    conn.close()


def handle_rfid(d):
    seat_id = str(d.get("seat_id", "")).strip()
    uid = str(d.get("uid", "")).strip().upper()

    if not seat_id or not uid:
        return

    conn = db()
    c = conn.cursor()

    c.execute("""
        SELECT * FROM reservations
        WHERE seat_id=? AND status IN (?,?)
        ORDER BY id DESC LIMIT 1
    """, (seat_id, RES_ACTIVE, RES_IN_USE))
    r = c.fetchone()

    if not r:
        mqtt_publish_cmd({
            "cmd": "deny",
            "seat_id": seat_id,
            "reason": "NO_RESERVATION",
            "uid": uid
        })
        conn.close()
        return

    exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")
    if datetime.now() > exp and r["status"] == RES_ACTIVE:
        c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_FREE, now_str(), seat_id))
        conn.commit()
        mqtt_publish_cmd({
            "cmd": "deny",
            "seat_id": seat_id,
            "reason": "RES_EXPIRED",
            "uid": uid
        })
        conn.close()
        return

    if r["status"] == RES_ACTIVE:
        if r["uid"] is None or r["uid"] == "":
            c.execute("""
                UPDATE reservations
                SET status=?, uid=?, checkin_at=?
                WHERE id=?
            """, (RES_IN_USE, uid, now_str(), r["id"]))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                      (SEAT_IN_USE, now_str(), seat_id))
            conn.commit()

            mqtt_publish_cmd({
                "cmd": "checkin_ok",
                "seat_id": seat_id,
                "reservation_id": r["id"],
                "uid": uid
            })
        else:
            if r["uid"].upper() != uid:
                mqtt_publish_cmd({
                    "cmd": "deny",
                    "seat_id": seat_id,
                    "reason": "UID_MISMATCH",
                    "uid": uid
                })
            else:
                mqtt_publish_cmd({
                    "cmd": "checkin_ok",
                    "seat_id": seat_id,
                    "reservation_id": r["id"],
                    "uid": uid
                })

    elif r["status"] == RES_IN_USE:
        if (r["uid"] or "").upper() == uid:
            c.execute("""
                UPDATE reservations
                SET status=?, checkout_at=?
                WHERE id=?
            """, (RES_DONE, now_str(), r["id"]))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                      (SEAT_FREE, now_str(), seat_id))
            conn.commit()

            mqtt_publish_cmd({
                "cmd": "checkout_ok",
                "seat_id": seat_id,
                "reservation_id": r["id"],
                "uid": uid
            })
        else:
            mqtt_publish_cmd({
                "cmd": "deny",
                "seat_id": seat_id,
                "reason": "UID_MISMATCH",
                "uid": uid
            })

    conn.close()


def start_mqtt():
    if MQTT_USER:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)

    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_message = on_mqtt_message
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    mqtt_client.loop_start()


# ===================== Web 页面 =====================
INDEX_HTML = """
<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>座位预约系统</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;margin:16px;background:#f6f7fb;}
.card{background:#fff;border:1px solid #e6e7ee;border-radius:12px;padding:12px;margin-bottom:12px;}
.row{display:flex;gap:12px;flex-wrap:wrap;}
.kpi{flex:1;min-width:220px;}
h2{margin:0 0 8px 0;font-size:16px;}
.small{color:#666;font-size:12px;}
table{width:100%;border-collapse:collapse;}
th,td{padding:8px;border-bottom:1px solid #eee;text-align:left;font-size:14px;}
.badge{padding:2px 8px;border-radius:999px;font-size:12px;display:inline-block;}
.free{background:#e8fff1;color:#0a7a3e;}
.reserved{background:#fff8e6;color:#8a5b00;}
.inuse{background:#e8f2ff;color:#0b4ea2;}
.warn{background:#ffe9e9;color:#9b1c1c;}
button{border:0;background:#2d6cdf;color:#fff;padding:8px 10px;border-radius:10px;cursor:pointer;}
button.del{background:#c0392b;}
button:disabled{opacity:.5;cursor:not-allowed;}
input,select{padding:8px;border-radius:10px;border:1px solid #ddd;width:100%;}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
</style>
</head>
<body>

<div class="card">
  <h2>实时环境</h2>
  <div class="row">
    <div class="card kpi">
      <div class="small">温度</div>
      <div style="font-size:28px" id="temp">--</div>
    </div>
    <div class="card kpi">
      <div class="small">湿度</div>
      <div style="font-size:28px" id="humi">--</div>
    </div>
    <div class="card kpi">
      <div class="small">光照</div>
      <div style="font-size:28px" id="lux">--</div>
    </div>
  </div>
  <div class="small" id="last_update">--</div>
</div>

<div class="card">
  <h2>预约座位</h2>
  <div class="grid2">
    <div>
      <div class="small">选择座位</div>
      <select id="seatSelect"></select>
    </div>
    <div>
      <div class="small">预约人</div>
      <input id="userName" placeholder="请输入姓名/学号" list="userList">
      <datalist id="userList"></datalist>
    </div>
  </div>
  <div class="grid2" style="margin-top:10px">
    <div>
      <div class="small">预约时长(分钟)</div>
      <input id="minutes" value="120">
    </div>
    <div style="display:flex;align-items:flex-end">
      <button onclick="reserve()">提交预约</button>
    </div>
  </div>
  <div class="small" id="reserveResult" style="margin-top:8px;color:#0b4ea2;"></div>
</div>

<div class="card">
  <h2>用户管理</h2>
  <div class="grid2">
    <div>
      <div class="small">姓名 (必填)</div>
      <input id="newUserName" placeholder="输入姓名">
    </div>
    <div>
      <div class="small">卡号 (UID/可选)</div>
      <input id="newUserUid" placeholder="RFID卡号">
    </div>
  </div>
  <div style="margin-top:10px; text-align:right;">
    <button onclick="addUser()">添加用户</button>
  </div>

  <table style="margin-top:12px">
    <thead>
      <tr><th>ID</th><th>姓名</th><th>卡号</th><th>操作</th></tr>
    </thead>
    <tbody id="userTable"></tbody>
  </table>
</div>

<div class="card">
  <h2>座位状态</h2>
  <table>
    <thead>
      <tr><th>座位</th><th>状态</th><th>预约信息</th><th>占位检测</th></tr>
    </thead>
    <tbody id="seatTable"></tbody>
  </table>
</div>

<div class="card">
  <h2>管理员</h2>
  <div class="small">占位事件：座位空闲但检测到物品（TOF）</div>
  <div id="incidents" class="small" style="margin-top:6px"></div>
</div>

<script>
// 获取并渲染状态
async function fetchState(){
  const r = await fetch('/api/state');
  const s = await r.json();

  // KPIs
  if(s.latest){
    document.getElementById('temp').innerText = (s.latest.temp ?? '--') + ' ℃';
    document.getElementById('humi').innerText = (s.latest.humi ?? '--') + ' %';
    document.getElementById('lux').innerText  = (s.latest.lux  ?? '--') + ' lux';
    document.getElementById('last_update').innerText = '更新时间：' + (s.latest.created_at ?? '--') + (s.latest.seat_id ? ('（来源座位 '+s.latest.seat_id+'）') : '');
  }

  // 座位下拉
  const sel = document.getElementById('seatSelect');
  const currentVal = sel.value; 

  sel.innerHTML = '';
  s.seats.forEach(it=>{
    const opt = document.createElement('option');
    opt.value = it.seat_id;
    opt.textContent = it.display + ' (' + it.state + ')';
    opt.disabled = (it.state !== 'FREE');
    sel.appendChild(opt);
  });

  if(currentVal){
      sel.value = currentVal;
  }

  // 座位表格 - 增加取消按钮逻辑
  const tb = document.getElementById('seatTable');
  tb.innerHTML = '';
  s.seats.forEach(it=>{
    const tr = document.createElement('tr');
    const badge = it.state==='FREE'?'free':(it.state==='RESERVED'?'reserved':'inuse');

    let res = '--';
    if(it.active_reservation){
        res = it.active_reservation.user + ' | ' + it.active_reservation.status;
        // 如果状态是 ACTIVE (已预约未签到)，显示取消按钮
        if(it.active_reservation.status === 'ACTIVE'){
            res += ` <button class="del" style="padding:4px 8px;font-size:12px;margin-left:4px" onclick="cancelRes(${it.active_reservation.id})">取消</button>`;
        }
        res += `<br/><span class="small">到期: ${it.active_reservation.expires_at}</span>`;
    }

    const occ = it.occupy_active ? '<span class="badge warn">疑似占位</span>' : '--';

    tr.innerHTML =
      '<td>'+it.display+'</td>'+
      '<td><span class="badge '+badge+'">'+it.state+'</span></td>'+
      '<td>'+res+'</td>'+
      '<td>'+occ+'</td>';
    tb.appendChild(tr);
  });

  // 占位事件
  const inc = document.getElementById('incidents');
  if(s.incidents.length===0){
    inc.innerText = '暂无占位事件';
  }else{
    inc.innerHTML = s.incidents.map(x=>
      '座位 '+x.seat_id+' | 开始 '+x.opened_at+' | TOF '+(x.last_tof_mm ?? '--')+'mm'
    ).join('<br/>');
  }
}

// 预约
async function reserve(){
  const seat_id = document.getElementById('seatSelect').value;
  const user = document.getElementById('userName').value.trim();
  const minutes = parseInt(document.getElementById('minutes').value || '120');
  if(!seat_id){ alert('请选择空闲座位'); return; }
  if(!user){ alert('请输入预约人'); return; }

  const r = await fetch('/api/reserve', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({seat_id, user, minutes})
  });
  const s = await r.json();
  document.getElementById('reserveResult').innerText = s.ok ? ('预约成功：编号 '+s.reservation_id+'，请在座位端刷卡签到') : ('预约失败：'+s.error);
  fetchState();
}

// 取消预约 (新增)
async function cancelRes(rid){
    if(!confirm('确定取消该预约吗?')) return;
    const r = await fetch('/api/cancel', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify({reservation_id: rid})
    });
    const d = await r.json();
    if(d.ok){
        alert('已取消');
        fetchState();
    }else{
        alert('取消失败: '+d.error);
    }
}

// 用户管理
async function fetchUsers(){
    const r = await fetch('/api/users');
    const d = await r.json();

    const tb = document.getElementById('userTable');
    tb.innerHTML = '';

    const dl = document.getElementById('userList');
    dl.innerHTML = '';

    if(d.ok){
        d.users.forEach(u=>{
            const tr = document.createElement('tr');
            tr.innerHTML = `<td>${u.id}</td><td>${u.username}</td><td>${u.uid||'--'}</td>
            <td><button class="del" onclick="delUser(${u.id})">删除</button></td>`;
            tb.appendChild(tr);

            const opt = document.createElement('option');
            opt.value = u.username;
            dl.appendChild(opt);
        });
    }
}

async function addUser(){
    const username = document.getElementById('newUserName').value.trim();
    const uid = document.getElementById('newUserUid').value.trim();
    if(!username) { alert('请输入姓名'); return; }

    const r = await fetch('/api/users/add', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify({username, uid})
    });
    const d = await r.json();
    if(d.ok){
        alert('添加成功');
        document.getElementById('newUserName').value='';
        document.getElementById('newUserUid').value='';
        fetchUsers();
    }else{
        alert('添加失败: '+d.error);
    }
}

async function delUser(id){
    if(!confirm('确定删除该用户吗?')) return;
    const r = await fetch('/api/users/delete', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify({id})
    });
    const d = await r.json();
    if(d.ok){
        fetchUsers();
    }else{
        alert('删除失败');
    }
}

fetchState();
fetchUsers();
setInterval(fetchState, 1500);
</script>
</body>
</html>
"""


@app.route("/")
def index():
    return render_template_string(INDEX_HTML)


@app.route("/api/state")
def api_state():
    with lock:
        cleanup_expired_reservations()
        conn = db()
        c = conn.cursor()

        c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1")
        latest = c.fetchone()
        latest_obj = dict(latest) if latest else None

        c.execute("SELECT * FROM occupy_incidents WHERE closed_at IS NULL ORDER BY id DESC LIMIT 20")
        inc = [dict(x) for x in c.fetchall()]

        c.execute("SELECT * FROM seats ORDER BY seat_id")
        seat_rows = c.fetchall()

        seats = []
        for s in seat_rows:
            sid = s["seat_id"]
            c.execute("""
                SELECT * FROM reservations
                WHERE seat_id=? AND status IN (?,?)
                ORDER BY id DESC LIMIT 1
            """, (sid, RES_ACTIVE, RES_IN_USE))
            ar = c.fetchone()
            ar_obj = dict(ar) if ar else None

            c.execute("""
                SELECT 1 FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                LIMIT 1
            """, (sid,))
            occupy_active = True if c.fetchone() else False

            seats.append({
                "seat_id": sid,
                "display": s["display"],
                "state": s["state"],
                "updated_at": s["updated_at"],
                "active_reservation": ar_obj,
                "occupy_active": occupy_active
            })

        conn.close()
        return jsonify({
            "ok": True,
            "latest": latest_obj,
            "seats": seats,
            "incidents": inc
        })


@app.route("/api/reserve", methods=["POST"])
def api_reserve():
    body = request.get_json(force=True, silent=True) or {}
    seat_id = str(body.get("seat_id", "")).strip()
    user = str(body.get("user", "")).strip()
    minutes = int(body.get("minutes", 120))

    if not seat_id or not user:
        return jsonify({"ok": False, "error": "参数缺失"}), 400
    if minutes < 10: minutes = 10
    if minutes > 8 * 60: minutes = 8 * 60

    with lock:
        cleanup_expired_reservations()
        conn = db()
        c = conn.cursor()

        c.execute("SELECT * FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        if not seat:
            conn.close()
            return jsonify({"ok": False, "error": "座位不存在"}), 404
        if seat["state"] != SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "座位非空闲"}), 409

        # 查找用户是否有绑定UID
        c.execute("SELECT uid FROM users WHERE username=?", (user,))
        u_row = c.fetchone()
        bound_uid = u_row["uid"] if u_row else None

        reserved_at = now_str()
        expires_at = (datetime.now() + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S")

        c.execute("""
            INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at,checkin_at,checkout_at)
            VALUES(?,?,?,?,?,?,NULL,NULL)
        """, (seat_id, user, RES_ACTIVE, None, reserved_at, expires_at))
        rid = c.lastrowid

        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_RESERVED, now_str(), seat_id))

        conn.commit()
        conn.close()

        # 发送预约命令
        mqtt_publish_cmd({
            "cmd": "reserve",
            "seat_id": seat_id,
            "reservation_id": rid,
            "user": user,
            "uid": bound_uid,  # 如果用户表里有UID，发给设备，这样只能该卡签到
            "expires_at": expires_at
        })

        return jsonify({"ok": True, "reservation_id": rid})


@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    body = request.get_json(force=True, silent=True) or {}
    rid = int(body.get("reservation_id", 0))
    if rid <= 0:
        return jsonify({"ok": False, "error": "参数缺失"}), 400

    with lock:
        conn = db()
        c = conn.cursor()
        c.execute("SELECT * FROM reservations WHERE id=?", (rid,))
        r = c.fetchone()
        if not r:
            conn.close()
            return jsonify({"ok": False, "error": "预约不存在"}), 404

        if r["status"] not in (RES_ACTIVE,):
            conn.close()
            return jsonify({"ok": False, "error": "当前状态不可取消"}), 409

        c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_CANCEL, rid))
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_FREE, now_str(), r["seat_id"]))
        conn.commit()
        conn.close()

        # 发送释放命令 (注意：server用cancel，但mqtt_publish_cmd会自动转成release发给STM32)
        mqtt_publish_cmd({
            "cmd": "cancel",
            "seat_id": r["seat_id"],
            "reservation_id": rid
        })
        return jsonify({"ok": True})


# ===================== 用户管理 API =====================
@app.route("/api/users", methods=["GET"])
def api_users_list():
    conn = db()
    c = conn.cursor()
    c.execute("SELECT * FROM users ORDER BY id DESC")
    rows = [dict(x) for x in c.fetchall()]
    conn.close()
    return jsonify({"ok": True, "users": rows})


@app.route("/api/users/add", methods=["POST"])
def api_users_add():
    body = request.get_json(force=True, silent=True) or {}
    name = str(body.get("username", "")).strip()
    uid = str(body.get("uid", "")).strip().upper()
    if not name:
        return jsonify({"ok": False, "error": "用户名不能为空"}), 400

    conn = db()
    c = conn.cursor()
    try:
        c.execute("INSERT INTO users(username, uid, created_at) VALUES(?,?,?)",
                  (name, uid, now_str()))
        conn.commit()
    except sqlite3.IntegrityError:
        conn.close()
        return jsonify({"ok": False, "error": "用户名已存在"}), 409
    except Exception as e:
        conn.close()
        return jsonify({"ok": False, "error": str(e)}), 500

    conn.close()
    return jsonify({"ok": True})


@app.route("/api/users/delete", methods=["POST"])
def api_users_delete():
    body = request.get_json(force=True, silent=True) or {}
    uid = body.get("id")
    if not uid:
        return jsonify({"ok": False, "error": "ID缺失"}), 400

    conn = db()
    c = conn.cursor()
    c.execute("DELETE FROM users WHERE id=?", (uid,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


if __name__ == "__main__":
    init_db()
    start_mqtt()
    app.run(host="0.0.0.0", port=int(os.getenv("PORT", "5000")), debug=False)

