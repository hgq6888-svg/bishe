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
# 用于记录座位连续无人的开始时间，格式: { "A18": datetime_obj }
seat_empty_timers = {}

# 业务配置
CHECKIN_TIMEOUT_MIN = 15  # 预约后必须在15分钟内签到
AWAY_TIMEOUT_MIN = 20  # 签到后连续离开20分钟自动释放
CLEANUP_INTERVAL_SEC = 30  # 后台清理任务间隔


# ===================== 后台任务线程 =====================
def background_task_loop():
    """后台独立线程：处理过期、超时未签到等逻辑 + 广播时间"""
    print("[System] 后台监控线程已启动")
    while True:
        try:
            time.sleep(CLEANUP_INTERVAL_SEC)

            # 1. 执行业务清理逻辑
            with db_lock:
                _check_reservations_logic()

            # 2. 【新增】广播服务器时间给STM32
            _broadcast_time()

        except Exception as e:
            print(f"[System Error] 后台任务异常: {e}")


def _broadcast_time():
    """主动下发时间同步命令"""
    now = datetime.now()
    # 格式: HH:MM:SS
    t_str = now.strftime("%H:%M:%S")
    # 发送命令: type=time_sync&time=12:30:45
    print(f"[System] 广播时间同步: {t_str}")
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

        # 逻辑A: 超过了总预约结束时间
        if now > expires_at:
            reason = "expired"

        # 逻辑B: 预约后超过 N 分钟未签到 (No-show)
        elif (now - created_at).total_seconds() > (CHECKIN_TIMEOUT_MIN * 60):
            reason = "no_show"

        if reason:
            print(f"[System] 释放座位 {r['seat_id']}, 原因: {reason}")
            # 更新预约状态
            status_code = RES_EXPIRED if reason == "expired" else "CANCEL_NOSHOW"
            c.execute("UPDATE reservations SET status=? WHERE id=?", (status_code, r["id"]))

            # 释放座位
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
            # 再次确认数据库状态
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
            # 兼容 STM32 发来的混合数据
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
    # print(f"[MQTT >>] {msg_str}") # 减少日志刷屏
    client.publish(MQTT_CMD_TOPIC, msg_str, qos=1, retain=False)


def _handle_telemetry(d):
    seat_id = str(d.get("seat_id", "")).strip()
    if not seat_id: return

    # 【修复重点】完整解析所有环境参数，不仅仅是 ToF
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

    # 判断是否有人 (用于逻辑)
    is_present = False
    if tof > 0 and tof < TOF_OCCUPIED_MM:  # 使用 config.py 中的阈值
        is_present = True

    # 存入数据库 (Telemetry表) - 包含 temp, humi, lux
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

    # === 久离判断逻辑 ===
    c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))
    seat_row = c.fetchone()

    if seat_row:
        state = seat_row["state"]
        if state == SEAT_IN_USE:
            if is_present:
                # 有人，清除离座计时器
                if seat_id in seat_empty_timers:
                    print(f"[System] 座位 {seat_id} 检测到用户回归")
                    seat_empty_timers.pop(seat_id)
            else:
                # 无人，开始或继续计时
                if seat_id not in seat_empty_timers:
                    print(f"[System] 座位 {seat_id} 用户离开，开始计时")
                    seat_empty_timers[seat_id] = datetime.now()
        else:
            # 非使用状态，不需要计时
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
            publish_cmd({"cmd": "deny", "seat_id": seat_id, "reason": "WRONG_USER", "uid": uid})
        else:
            if r["status"] == RES_ACTIVE:
                c.execute("UPDATE reservations SET status=?, uid=?, checkin_at=? WHERE id=?",
                          (RES_IN_USE, uid, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_IN_USE, now_str(), seat_id))
                publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id, "uid": uid})
                seat_empty_timers.pop(seat_id, None)
            elif r["status"] == RES_IN_USE:
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?",
                          (RES_DONE, now_str(), r["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), seat_id))
                publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id, "uid": uid})
                seat_empty_timers.pop(seat_id, None)
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
        # 启动后台监控线程
        t = threading.Thread(target=background_task_loop, daemon=True)
        t.start()
    except Exception as e:
        print(f"[MQTT] Connection Failed: {e}")