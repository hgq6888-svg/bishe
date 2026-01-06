# mqtt_service.py
import json
import paho.mqtt.client as mqtt
from datetime import datetime
from config import *
from database import get_conn, db_lock, now_str

client = mqtt.Client()

def publish_cmd(payload: dict):
    """发送指令到 STM32 (转换为 key=value 格式)"""
    data = payload.copy()
    
    # 字段映射
    if "cmd" in data:
        data["type"] = data.pop("cmd")
    if data.get("type") == "cancel":
        data["type"] = "release"

    parts = [f"{k}={v}" for k, v in data.items() if v is not None]
    msg_str = "&".join(parts)
    
    print(f"[MQTT] Sending: {msg_str}")
    client.publish(MQTT_CMD_TOPIC, msg_str, qos=0, retain=False)

def cleanup_expired_reservations():
    """定期清理过期预约"""
    with db_lock:
        conn = get_conn()
        c = conn.cursor()
        now = datetime.now()

        c.execute("SELECT * FROM reservations WHERE status=?", (RES_ACTIVE,))
        rows = c.fetchall()
        for r in rows:
            try:
                exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")
                if now > exp:
                    print(f"[System] Reservation {r['id']} expired.")
                    c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))
                    
                    # 只有当座位还处于“已预约”状态时才释放，避免误释放“使用中”的座位
                    c.execute("SELECT state FROM seats WHERE seat_id=?", (r["seat_id"],))
                    seat = c.fetchone()
                    if seat and seat["state"] == SEAT_RESERVED:
                        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                  (SEAT_FREE, now_str(), r["seat_id"]))
                        # 发送 MQTT 释放指令
                        publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reason": "expired"})
            except Exception as e:
                print(f"Cleanup error: {e}")

        conn.commit()
        conn.close()

# --- 内部逻辑处理 ---

def _object_present_from_tof(tof_mm):
    if tof_mm is None: return None
    try:
        v = int(tof_mm)
        if v <= 0: return None
        return 1 if v < TOF_OCCUPIED_MM else 0
    except:
        return None

def _handle_telemetry(d):
    seat_id = str(d.get("seat_id", "")).strip() or None
    tof = d.get("tof_mm", None)
    obj = d.get("object_present", _object_present_from_tof(tof))

    conn = get_conn()
    c = conn.cursor()
    c.execute("""
        INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at)
        VALUES(?,?,?,?,?,?,?)
    """, (seat_id, d.get("temp"), d.get("humi"), d.get("lux"), tof, obj, now_str()))
    conn.commit()

    # 占位检测
    if seat_id:
        c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        seat_state = seat["state"] if seat else None

        if obj == 1 and seat_state == SEAT_FREE:
            # 空闲时检测到人 -> 记录事件
            c.execute("SELECT * FROM occupy_incidents WHERE seat_id=? AND closed_at IS NULL ORDER BY id DESC LIMIT 1", (seat_id,))
            if c.fetchone():
                c.execute("UPDATE occupy_incidents SET last_tof_mm=? WHERE seat_id=? AND closed_at IS NULL", (tof, seat_id))
            else:
                c.execute("INSERT INTO occupy_incidents(seat_id, opened_at, last_tof_mm) VALUES(?,?,?)", (seat_id, now_str(), tof))
                publish_cmd({"cmd": "occupy_warn", "seat_id": seat_id})
        elif obj == 0:
            # 人离开 -> 关闭事件
            c.execute("UPDATE occupy_incidents SET closed_at=? WHERE seat_id=? AND closed_at IS NULL", (now_str(), seat_id))

    conn.commit()
    conn.close()

def _handle_state_update(d):
    seat_id = str(d.get("seat_id", "")).strip()
    if not seat_id: return
    
    conn = get_conn()
    c = conn.cursor()
    updates = []
    params = []
    
    if d.get("state"): updates.append("state=?"), params.append(d.get("state"))
    if d.get("light") is not None: updates.append("light_on=?"), params.append(int(d.get("light")))
    if d.get("light_mode"): updates.append("light_mode=?"), params.append(d.get("light_mode"))
    
    if updates:
        updates.append("updated_at=?")
        params.append(now_str())
        params.append(seat_id)
        c.execute(f"UPDATE seats SET {','.join(updates)} WHERE seat_id=?", tuple(params))
        conn.commit()
    conn.close()

def _handle_rfid(d):
    seat_id = str(d.get("seat_id", "")).strip()
    uid = str(d.get("uid", "")).strip().upper()
    if not seat_id or not uid: return

    conn = get_conn()
    c = conn.cursor()
    c.execute("SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1", (seat_id, RES_ACTIVE, RES_IN_USE))
    r = c.fetchone()

    if not r:
        publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "NO_RES", "uid": uid})
    else:
        # 验证 UID (如果预约绑定了用户)
        if r["uid"] and r["uid"].upper() != uid:
            publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "UID_ERR", "uid": uid})
        else:
            if r["status"] == RES_ACTIVE:
                # 签到
                c.execute("UPDATE reservations SET status=?, uid=?, checkin_at=? WHERE id=?", (RES_IN_USE, uid, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_IN_USE, now_str(), seat_id))
                publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id, "uid": uid})
            elif r["status"] == RES_IN_USE:
                # 签退 (必须是同一人，上面已验证UID)
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?", (RES_DONE, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_FREE, now_str(), seat_id))
                publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id, "uid": uid})
            conn.commit()
    conn.close()

# --- MQTT 回调 ---

def _parse_payload(msg):
    try: txt = msg.payload.decode("utf-8", errors="ignore").strip()
    except: return {}
    if txt.startswith("{"):
        try: return json.loads(txt)
        except: pass
    # 解析 key=value
    data = {}
    for p in txt.split("&"):
        if "=" in p:
            k, v = p.split("=", 1)
            data[k.strip()] = v.strip()
    return data

def _on_message(client, userdata, msg):
    data = _parse_payload(msg)
    if not data: return
    
    tp = data.get("type")
    if not tp:
        if "rfid" in msg.topic: tp = "rfid"
        elif "telemetry" in msg.topic: tp = "telemetry"
        elif "state" in msg.topic: tp = "state"
        else: tp = "unknown"

    with db_lock:
        # 每次收到消息顺便检查过期，保持状态新鲜
        cleanup_expired_reservations()
        
        if tp == "telemetry": _handle_telemetry(data)
        elif tp == "state": _handle_state_update(data)
        elif tp == "rfid" or "uid" in data: _handle_rfid(data)
        elif any(k in data for k in ("temp", "humi", "lux")): _handle_telemetry(data)

def start_mqtt():
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = lambda c,u,f,rc: c.subscribe(MQTT_SUB_TOPIC, qos=0)
    client.on_message = _on_message
    
    print(f"[MQTT] Connecting to {MQTT_HOST}...")
    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"[MQTT] Failed: {e}")