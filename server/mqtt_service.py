# mqtt_service.py
import json
import time
import threading
import paho.mqtt.client as mqtt
from datetime import datetime, timedelta
from config import *
from database import get_conn, db_lock, now_str

client = mqtt.Client()

# ===================== 全局缓存 =====================
seat_empty_timers = {}

# 业务配置
CHECKIN_TIMEOUT_MIN = 15  # 预约后必须在15分钟内签到
AWAY_TIMEOUT_MIN = 20  # 签到后连续离开20分钟自动释放
CLEANUP_INTERVAL_SEC = 30  # 后台清理任务间隔


# ===================== 后台任务线程 =====================
def background_task_loop():
    print("[System] 后台监控线程已启动")
    while True:
        try:
            time.sleep(CLEANUP_INTERVAL_SEC)
            with db_lock:
                _check_reservations_logic()
            _broadcast_time()
        except Exception as e:
            print(f"[System Error] 后台任务异常: {e}")


def _broadcast_time():
    now = datetime.now()
    t_str = now.strftime("%H:%M:%S")
    # print(f"[System] 广播时间同步: {t_str}")
    publish_cmd({"cmd": "time_sync", "time": t_str})


def _check_reservations_logic():
    conn = get_conn()
    c = conn.cursor()
    now = datetime.now()

    # 1. 检查【预约过期】&【超时未签到】
    c.execute("SELECT * FROM reservations WHERE status=?", (RES_ACTIVE,))
    rows = c.fetchall()

    for r in rows:
        reason = None
        created_at = datetime.strptime(r["reserved_at"], "%Y-%m-%d %H:%M:%S")
        expires_at = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")

        if now > expires_at:
            reason = "expired"
        elif (now - created_at).total_seconds() > (CHECKIN_TIMEOUT_MIN * 60):
            reason = "no_show"

        if reason:
            print(f"[System] 释放座位 {r['seat_id']}, 原因: {reason}")
            status_code = RES_EXPIRED if reason == "expired" else "CANCEL_NOSHOW"
            c.execute("UPDATE reservations SET status=? WHERE id=?", (status_code, r["id"]))

            c.execute("SELECT state FROM seats WHERE seat_id=?", (r["seat_id"],))
            seat = c.fetchone()
            if seat and seat["state"] == SEAT_RESERVED:
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), r["seat_id"]))
                publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reason": reason})

    # 2. 检查【久离自动签退】
    to_remove = []
    for sid, start_time in seat_empty_timers.items():
        if (now - start_time).total_seconds() > (AWAY_TIMEOUT_MIN * 60):
            c.execute("SELECT * FROM reservations WHERE seat_id=? AND status=?", (sid, RES_IN_USE))
            res = c.fetchone()
            if res:
                print(f"[System] 座位 {sid} 无人太久，强制签退")
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?",
                          (RES_DONE, now_str(), res["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), sid))
                publish_cmd({"cmd": "checkout_ok", "seat_id": sid, "reason": "auto_away"})
            to_remove.append(sid)

    for sid in to_remove:
        seat_empty_timers.pop(sid, None)

    conn.commit()
    conn.close()


# ===================== MQTT 回调函数 =====================

def _on_message(client, userdata, msg):
    try:
        data = _parse_payload(msg)
        if not data: return

        tp = data.get("type")
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
            if tp == "telemetry" or any(k in data for k in ("temp", "tof_mm", "lux")):
                _handle_telemetry(data)
            elif tp == "state":
                _handle_state_update(data)
            elif tp == "rfid" or "uid" in data:
                _handle_rfid(data)

    except Exception as e:
        print(f"[MQTT Error] 处理消息失败: {e}")


def _parse_payload(msg):
    try:
        txt = msg.payload.decode("utf-8", errors="ignore").strip()
        if txt.startswith("{"):
            return json.loads(txt)
        data = {}
        for p in txt.split("&"):
            if "=" in p:
                k, v = p.split("=", 1)
                data[k.strip()] = v.strip()
        return data
    except:
        return {}


# ===================== 业务处理 =====================

def publish_cmd(payload: dict):
    data = payload.copy()
    if "cmd" in data: data["type"] = data.pop("cmd")
    parts = [f"{k}={v}" for k, v in data.items() if v is not None]
    msg_str = "&".join(parts)
    client.publish(MQTT_CMD_TOPIC, msg_str, qos=1, retain=False)


def _handle_telemetry(d):
    seat_id = str(d.get("seat_id", "")).strip()
    if not seat_id: return

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
        tof = int(d.get("tof_mm", -1))
    except:
        tof = -1

    is_present = False
    if tof > 0 and tof < TOF_OCCUPIED_MM:
        is_present = True

    conn = get_conn()
    c = conn.cursor()
    try:
        c.execute("""
            INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at)
            VALUES(?,?,?,?,?,?,?)
        """, (seat_id, temp, humi, lux, tof, 1 if is_present else 0, now_str()))
        conn.commit()
    except Exception as e:
        print(f"[DB Error] 保存环境数据失败: {e}")

    # 久离逻辑
    c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))
    seat_row = c.fetchone()

    if seat_row:
        state = seat_row["state"]
        if state == SEAT_IN_USE:
            if is_present:
                if seat_id in seat_empty_timers:
                    print(f"[System] 座位 {seat_id} 检测到用户回归")
                    seat_empty_timers.pop(seat_id)
            else:
                if seat_id not in seat_empty_timers:
                    print(f"[System] 座位 {seat_id} 用户离开，开始计时")
                    seat_empty_timers[seat_id] = datetime.now()
        else:
            seat_empty_timers.pop(seat_id, None)

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
    """【修改】严格校验 UID 必须与预约时的一致"""
    seat_id = str(d.get("seat_id", "")).strip()
    uid = str(d.get("uid", "")).strip().upper()
    if not seat_id or not uid: return

    conn = get_conn()
    c = conn.cursor()

    # 查找该座位的有效预约 (ACTIVE 或 IN_USE)
    c.execute("SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
              (seat_id, RES_ACTIVE, RES_IN_USE))
    r = c.fetchone()

    if not r:
        # 没有预约，直接拒绝
        publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "NO_RESERVATION", "uid": uid})
    else:
        # 获取预约单上的 UID (这是用户预约时绑定的卡号)
        reserved_uid = r["uid"].upper() if r["uid"] else ""

        # 【核心安全校验】
        # 1. 预约单必须有卡号记录 (如果用户预约后解绑了卡，或者数据异常，拒绝)
        # 2. 刷的卡必须严格等于预约单上的卡号
        if not reserved_uid:
            print(f"[RFID Deny] 预约单 {r['id']} 无绑定卡号")
            publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "NO_BINDING", "uid": uid})

        elif reserved_uid != uid:
            print(f"[RFID Deny] 卡号不匹配! 预约:{reserved_uid} vs 刷卡:{uid}")
            publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "WRONG_CARD", "uid": uid})

        else:
            # === 验证通过，执行签到或签退 ===
            if r["status"] == RES_ACTIVE:
                # 执行签到
                c.execute("UPDATE reservations SET status=?, checkin_at=? WHERE id=?",
                          (RES_IN_USE, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_IN_USE, now_str(), seat_id))
                publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id, "uid": uid})
                seat_empty_timers.pop(seat_id, None)
                print(f"[RFID Accept] 用户 {r['user']} 签到成功 (卡号:{uid})")

            elif r["status"] == RES_IN_USE:
                # 执行签退
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?",
                          (RES_DONE, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), seat_id))
                publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id, "uid": uid})
                seat_empty_timers.pop(seat_id, None)
                print(f"[RFID Accept] 用户 {r['user']} 签退成功")

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
        t = threading.Thread(target=background_task_loop, daemon=True)
        t.start()
    except Exception as e:
        print(f"[MQTT] Connection Failed: {e}")