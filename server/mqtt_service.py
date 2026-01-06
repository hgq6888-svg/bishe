# mqtt_service.py
import json
import paho.mqtt.client as mqtt
from datetime import datetime
from config import *
from database import get_conn, db_lock, now_str

client = mqtt.Client()


# ===================== 回调函数 =====================

def _on_message(client, userdata, msg):
    # 【新增】打印收到的原始消息，用于调试
    try:
        payload_str = msg.payload.decode('utf-8')
    except:
        payload_str = str(msg.payload)
    print(f"[DEBUG] 收到消息 | Topic: {msg.topic} | Payload: {payload_str}")

    data = _parse_payload(msg)
    if not data: return

    tp = data.get("type")
    # 自动推断消息类型
    if not tp:
        if "rfid" in msg.topic:
            tp = "rfid"
        elif "telemetry" in msg.topic:
            tp = "telemetry"
        elif "state" in msg.topic:
            tp = "state"
        else:
            tp = "unknown"

    with db_lock:
        cleanup_expired_reservations()

        if tp == "telemetry":
            _handle_telemetry(data)
        elif tp == "state":
            _handle_state_update(data)
        elif tp == "rfid" or "uid" in data:
            _handle_rfid(data)
        # 兼容 STM32 发来的数据 (只要包含温湿度就认为是 telemetry)
        elif any(k in data for k in ("temp", "humi", "lux", "tof_mm")):
            _handle_telemetry(data)


def _parse_payload(msg):
    try:
        txt = msg.payload.decode("utf-8", errors="ignore").strip()
    except:
        return {}
    # 1. 尝试 JSON
    if txt.startswith("{"):
        try:
            return json.loads(txt)
        except:
            pass
    # 2. 尝试 key=value 格式
    data = {}
    for p in txt.split("&"):
        if "=" in p:
            k, v = p.split("=", 1)
            data[k.strip()] = v.strip()
    return data


# ===================== 业务处理 =====================

def publish_cmd(payload: dict):
    data = payload.copy()
    if "cmd" in data: data["type"] = data.pop("cmd")
    if data.get("type") == "cancel": data["type"] = "release"
    parts = [f"{k}={v}" for k, v in data.items() if v is not None]
    msg_str = "&".join(parts)
    print(f"[MQTT] 发送指令: {msg_str}")
    client.publish(MQTT_CMD_TOPIC, msg_str, qos=0, retain=False)


def cleanup_expired_reservations():
    conn = get_conn()
    c = conn.cursor()
    now = datetime.now()
    try:
        c.execute("SELECT * FROM reservations WHERE status=?", (RES_ACTIVE,))
        rows = c.fetchall()
        for r in rows:
            exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")
            if now > exp:
                print(f"[System] 预约过期: {r['id']}")
                c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))
                c.execute("SELECT state FROM seats WHERE seat_id=?", (r["seat_id"],))
                seat = c.fetchone()
                if seat and seat["state"] == SEAT_RESERVED:
                    c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                              (SEAT_FREE, now_str(), r["seat_id"]))
                    publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reason": "expired"})
        conn.commit()
    except Exception as e:
        print(f"Cleanup Error: {e}")
    finally:
        conn.close()


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

    # 安全的数据类型转换
    try:
        temp = float(d.get("temp"))
    except:
        temp = None
    try:
        humi = float(d.get("humi"))
    except:
        humi = None
    try:
        lux = int(d.get("lux"))
    except:
        lux = None
    try:
        tof = int(d.get("tof_mm"))
    except:
        tof = None

    obj = d.get("object_present", _object_present_from_tof(tof))

    conn = get_conn()
    c = conn.cursor()
    try:
        c.execute("""
            INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at)
            VALUES(?,?,?,?,?,?,?)
        """, (seat_id, temp, humi, lux, tof, obj, now_str()))
        conn.commit()
        # print(f"[DB] 已保存环境数据: {seat_id} T:{temp} H:{humi}")
    except Exception as e:
        print(f"[DB Error] 保存环境数据失败: {e}")

    # 占位检测逻辑
    if seat_id:
        c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        seat_state = seat["state"] if seat else None

        if obj == 1 and seat_state == SEAT_FREE:
            c.execute("SELECT * FROM occupy_incidents WHERE seat_id=? AND closed_at IS NULL ORDER BY id DESC LIMIT 1",
                      (seat_id,))
            if c.fetchone():
                c.execute("UPDATE occupy_incidents SET last_tof_mm=? WHERE seat_id=? AND closed_at IS NULL",
                          (tof, seat_id))
            else:
                c.execute("INSERT INTO occupy_incidents(seat_id, opened_at, last_tof_mm) VALUES(?,?,?)",
                          (seat_id, now_str(), tof))
                publish_cmd({"cmd": "occupy_warn", "seat_id": seat_id})
        elif obj == 0:
            c.execute("UPDATE occupy_incidents SET closed_at=? WHERE seat_id=? AND closed_at IS NULL",
                      (now_str(), seat_id))

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
    c.execute("SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
              (seat_id, RES_ACTIVE, RES_IN_USE))
    r = c.fetchone()

    if not r:
        publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "NO_RES", "uid": uid})
    else:
        if r["uid"] and r["uid"].upper() != uid:
            publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "UID_ERR", "uid": uid})
        else:
            if r["status"] == RES_ACTIVE:
                c.execute("UPDATE reservations SET status=?, uid=?, checkin_at=? WHERE id=?",
                          (RES_IN_USE, uid, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_IN_USE, now_str(), seat_id))
                publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id, "uid": uid})
            elif r["status"] == RES_IN_USE:
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?", (RES_DONE, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_FREE, now_str(), seat_id))
                publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id, "uid": uid})
            conn.commit()
    conn.close()


def start_mqtt():
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = lambda c, u, f, rc: c.subscribe(MQTT_SUB_TOPIC, qos=0) or print(f"[MQTT] Connected code: {rc}")
    client.on_message = _on_message

    print(f"[MQTT] Connecting to {MQTT_HOST}...")
    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_start()
    except Exception as e:
        print(f"[MQTT] Connection Failed: {e}")