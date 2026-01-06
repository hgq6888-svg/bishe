import json
import time
import threading
import paho.mqtt.client as mqtt
from datetime import datetime
from config import *
from database import get_conn, db_lock, now_str

client = mqtt.Client()
seat_empty_timers = {}  # 记录座位无人的开始时间

# 业务时间配置
CHECKIN_TIMEOUT_MIN = 15  # 15分钟未签到自动释放
AWAY_TIMEOUT_MIN = 20  # 20分钟无人自动签退
CLEANUP_INTERVAL_SEC = 30  # 检查周期


def background_task_loop():
    """后台任务：时间同步、过期清理、久离签退"""
    print("[System] 后台监控线程已启动")
    while True:
        try:
            time.sleep(CLEANUP_INTERVAL_SEC)
            with db_lock:
                _check_reservations_logic()
            # 广播服务器时间
            _broadcast_time()
        except Exception as e:
            print(f"[BG Task Error] {e}")


def _broadcast_time():
    t_str = datetime.now().strftime("%H:%M:%S")
    # 发送时间同步指令
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
        res_at = datetime.strptime(r["reserved_at"], "%Y-%m-%d %H:%M:%S")
        exp_at = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")

        if now > exp_at:
            reason = "expired"
        elif (now - res_at).total_seconds() > (CHECKIN_TIMEOUT_MIN * 60):
            reason = "no_show"

        if reason:
            print(f"[Auto] 释放座位 {r['seat_id']}, 原因: {reason}")
            st = RES_EXPIRED if reason == "expired" else "CANCEL_NOSHOW"
            c.execute("UPDATE reservations SET status=? WHERE id=?", (st, r["id"]))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_FREE, now_str(), r["seat_id"]))
            publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reason": reason})

    # 2. 检查【久离自动签退】
    to_remove = []
    for sid, start_time in seat_empty_timers.items():
        if (now - start_time).total_seconds() > (AWAY_TIMEOUT_MIN * 60):
            res = c.execute("SELECT * FROM reservations WHERE seat_id=? AND status=?", (sid, RES_IN_USE)).fetchone()
            if res:
                print(f"[Auto] 座位 {sid} 无人超时，强制签退")
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?",
                          (RES_DONE, now_str(), res["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_FREE, now_str(), sid))
                publish_cmd({"cmd": "checkout_ok", "seat_id": sid, "reason": "auto_away"})
            to_remove.append(sid)

    for sid in to_remove: seat_empty_timers.pop(sid, None)
    conn.commit()
    conn.close()


def _on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8").strip()
        # 兼容 JSON 或 key=value 格式
        data = json.loads(payload) if payload.startswith("{") else {k: v for k, v in
                                                                    [p.split("=", 1) for p in payload.split("&") if
                                                                     "=" in p]}

        tp = data.get("type", "unknown")
        # 兼容Topic判断
        if "telemetry" in msg.topic:
            tp = "telemetry"
        elif "state" in msg.topic:
            tp = "state"

        with db_lock:
            # 优先处理带 checkin/checkout 意图的指令 (来自STM32 UI交互)
            if tp == "checkin" or tp == "checkout":
                _handle_rfid_intent(data, tp)
            elif tp == "telemetry" or "temp" in data:
                _handle_telemetry(data)
            elif tp == "state":
                _handle_state_update(data)
            # 兼容纯刷卡 (无UI交互)
            elif tp == "rfid" or "uid" in data:
                _handle_rfid_intent(data, "auto")

    except Exception as e:
        print(f"[MQTT Recv Error] {e}")


def publish_cmd(data_dict):
    msg_parts = [f"{k}={v}" for k, v in data_dict.items() if v is not None]
    client.publish(MQTT_CMD_TOPIC, "&".join(msg_parts), qos=1)


def _handle_telemetry(d):
    sid = d.get("seat_id")
    if not sid: return

    # 存环境数据
    conn = get_conn()
    conn.execute(
        "INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at) VALUES(?,?,?,?,?,?,?)",
        (sid, d.get("temp"), d.get("humi"), d.get("lux"), d.get("tof_mm"), 0, now_str()))

    # 久离检测逻辑
    tof = int(d.get("tof_mm", -1))
    is_present = (0 < tof < TOF_OCCUPIED_MM)

    seat = conn.execute("SELECT state FROM seats WHERE seat_id=?", (sid,)).fetchone()
    if seat and seat["state"] == SEAT_IN_USE:
        if is_present:
            if sid in seat_empty_timers: seat_empty_timers.pop(sid)  # 人回来了
        else:
            if sid not in seat_empty_timers: seat_empty_timers[sid] = datetime.now()  # 人走了，开始计时
    else:
        seat_empty_timers.pop(sid, None)

    conn.commit()
    conn.close()


def _handle_state_update(d):
    sid = d.get("seat_id")
    if not sid: return
    conn = get_conn()
    updates, params = [], []
    if "state" in d: updates.append("state=?"), params.append(d["state"])
    if "light" in d: updates.append("light_on=?"), params.append(int(d["light"]))
    if "light_mode" in d: updates.append("light_mode=?"), params.append(d["light_mode"])

    if updates:
        updates.append("updated_at=?")
        params.extend([now_str(), sid])
        conn.execute(f"UPDATE seats SET {','.join(updates)} WHERE seat_id=?", tuple(params))
        conn.commit()
    conn.close()


def _handle_rfid_intent(d, intent):
    """
    处理刷卡/签到/签退
    intent: "checkin", "checkout", "auto"
    """
    sid = d.get("seat_id")
    uid = str(d.get("uid", "")).strip().upper()
    if not sid or not uid: return

    conn = get_conn()
    c = conn.cursor()
    # 找当前该座位最相关的预约
    res = c.execute("SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
                    (sid, RES_ACTIVE, RES_IN_USE)).fetchone()

    if not res:
        print(f"[RFID] 拒绝 {uid} @ {sid}: 无预约")
        publish_cmd({"cmd": "deny", "seat_id": sid, "reason": "No Reservation", "uid": uid})
    else:
        # === 严格校验卡号 ===
        bound_uid = str(res["uid"] or "").strip().upper()
        if bound_uid != uid:
            print(f"[RFID] 拒绝 {uid} @ {sid}: 卡号不匹配 (应为 {bound_uid})")
            publish_cmd({"cmd": "deny", "seat_id": sid, "reason": "Wrong Card", "uid": uid})
        else:
            # === 卡号正确，执行逻辑 ===
            current_status = res["status"]

            # 意图判断
            action = "none"
            if intent == "checkin" and current_status == RES_ACTIVE:
                action = "do_checkin"
            elif intent == "checkout" and current_status == RES_IN_USE:
                action = "do_checkout"
            elif intent == "auto":
                # 自动推断：如果是ACTIVE则签到，如果是IN_USE则签退
                if current_status == RES_ACTIVE:
                    action = "do_checkin"
                elif current_status == RES_IN_USE:
                    action = "do_checkout"

            if action == "do_checkin":
                c.execute("UPDATE reservations SET status=?, checkin_at=? WHERE id=?",
                          (RES_IN_USE, now_str(), res["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_IN_USE, now_str(), sid))
                publish_cmd({"cmd": "checkin_ok", "seat_id": sid, "uid": uid})
                print(f"[RFID] {uid} 签到成功")

            elif action == "do_checkout":
                c.execute("UPDATE reservations SET status=?, checkout_at=? WHERE id=?",
                          (RES_DONE, now_str(), res["id"]))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (SEAT_FREE, now_str(), sid))
                publish_cmd({"cmd": "checkout_ok", "seat_id": sid, "uid": uid})
                print(f"[RFID] {uid} 签退成功")

            else:
                # 状态不符合意图 (例如重复签到)
                print(f"[RFID] {uid} 操作忽略: 状态 {current_status} 与意图 {intent} 不符")

    conn.commit()
    conn.close()


def start_mqtt():
    if MQTT_USER: client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = lambda c, u, f, rc: c.subscribe(MQTT_SUB_TOPIC) or print(f"[MQTT] Connected: {rc}")
    client.on_message = _on_message
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        client.loop_start()
        threading.Thread(target=background_task_loop, daemon=True).start()
    except Exception as e:
        print(f"[MQTT] Connect Failed: {e}")