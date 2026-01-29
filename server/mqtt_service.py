import json
import time
import threading
import paho.mqtt.client as mqtt
from datetime import datetime, timedelta
from config import *
from database import get_conn, db_lock, now_str

# MQTT 配置
BROKER = "1.14.163.35"
PORT = 1883
TOPIC_UP = "server/+/+"

client = mqtt.Client()


def get_beijing_time_str():
    """获取当前北京时间 HH:MM:SS"""
    utc_now = datetime.utcnow()
    beijing_now = utc_now + timedelta(hours=8)
    return beijing_now.strftime("%H:%M:%S")


def publish_cmd(cmd_dict):
    """发送指令给设备 (key=value格式)"""
    try:
        parts = [f"{k}={v}" for k, v in cmd_dict.items()]
        payload = "&".join(parts)
        client.publish("stm32/cmd", payload, qos=0)
        print(f"[MQTT] Sent: {payload}")
    except Exception as e:
        print(f"[MQTT] Publish error: {e}")


def time_broadcast_task():
    while True:
        time.sleep(60)
        try:
            t_str = get_beijing_time_str()
            publish_cmd({"cmd": "time_sync", "time": t_str})
        except Exception as e:
            print(f"[TIME] Broadcast error: {e}")


def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected with result code {rc}")
    client.subscribe(TOPIC_UP)


def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = msg.payload.decode("utf-8")
        print(f"[MQTT] Recv {topic}: {payload}")

        # 1. 基础解析 Payload
        data = {}
        for part in payload.split('&'):
            if '=' in part:
                k, v = part.split('=', 1)
                data[k] = v.strip()

        # --- 核心修复开始: 如果 Payload 缺字段，从 Topic 补全 ---
        # Topic 格式: server/{type}/{seat_id}
        topic_parts = topic.split('/')
        if len(topic_parts) >= 3:
            # 如果 data 中没有 type，从 topic[1] 获取 (例如 event)
            if "type" not in data:
                data["type"] = topic_parts[1]
            # 如果 data 中没有 seat_id，从 topic[2] 获取 (例如 A18)
            if "seat_id" not in data:
                data["seat_id"] = topic_parts[2]
        # --- 核心修复结束 ---

        seat_id = data.get("seat_id")
        msg_type = data.get("type")

        if not seat_id:
            print("[MQTT] Error: No seat_id found")
            return

        # 业务逻辑 1: 同步请求
        if msg_type == "sync":
            print(f"[SYNC] Device {seat_id} requesting sync...")
            publish_cmd({"cmd": "time_sync", "time": get_beijing_time_str()})

            with db_lock:
                conn = get_conn()
                res = conn.execute(
                    "SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
                    (seat_id, RES_ACTIVE, RES_IN_USE)
                ).fetchone()

                if res:
                    if res["status"] == RES_ACTIVE:
                        publish_cmd({
                            "cmd": "reserve", "seat_id": seat_id,
                            "user": res["user"], "uid": res["uid"], "expires_at": res["expires_at"]
                        })
                    elif res["status"] == RES_IN_USE:
                        publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id})
                else:
                    publish_cmd({"cmd": "release", "seat_id": seat_id})
                conn.close()
            return

        # 业务逻辑 2: 状态上报 & 遥测
        if msg_type == "state" or msg_type == "telemetry":
            with db_lock:
                conn = get_conn()
                if msg_type == "state" and "state" in data:
                    conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                 (data["state"], now_str(), seat_id))

                if "temp" in data or "humi" in data or "tof_mm" in data:
                    temp = float(data.get("temp", 0))
                    humi = int(float(data.get("humi", 0)))
                    lux = int(float(data.get("lux", 0)))
                    tof = int(float(data.get("tof_mm", 0)))
                    conn.execute(
                        "INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, created_at) VALUES(?,?,?,?,?,?)",
                        (seat_id, temp, humi, lux, tof, now_str()))
                    conn.execute("UPDATE seats SET updated_at=? WHERE seat_id=?", (now_str(), seat_id))

                conn.commit()
                conn.close()
            return

        # 业务逻辑 3: 刷卡事件 (checkin / checkout)
        if msg_type == "event":
            cmd = data.get("cmd")
            uid_hex = data.get("uid")
            print(f"[EVENT] Processing {cmd} from {seat_id} with UID {uid_hex}")

            if cmd == "checkin":
                with db_lock:
                    conn = get_conn()
                    res = conn.execute(
                        "SELECT id, uid FROM reservations WHERE seat_id=? AND status=? ORDER BY id DESC LIMIT 1",
                        (seat_id, RES_ACTIVE)
                    ).fetchone()

                    if res:
                        print(f"[CHECKIN] Found reservation, expected: {res['uid']}, got: {uid_hex}")
                        if res["uid"].upper() == uid_hex.upper():
                            conn.execute("UPDATE reservations SET status=? WHERE id=?", (RES_IN_USE, res["id"]))
                            conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                         (SEAT_IN_USE, now_str(), seat_id))
                            conn.commit()
                            publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id})
                            print(f"[CHECKIN] Success -> IN_USE")
                        else:
                            publish_cmd({"cmd": "deny", "seat_id": seat_id})
                            print(f"[CHECKIN] Denied: UID Mismatch")
                    else:
                        publish_cmd({"cmd": "deny", "seat_id": seat_id})
                        print(f"[CHECKIN] Denied: No active reservation")
                    conn.close()

            elif cmd == "checkout":
                with db_lock:
                    conn = get_conn()
                    res = conn.execute(
                        "SELECT id, uid FROM reservations WHERE seat_id=? AND status=? ORDER BY id DESC LIMIT 1",
                        (seat_id, RES_IN_USE)
                    ).fetchone()

                    if res and res["uid"].upper() == uid_hex.upper():
                        conn.execute("UPDATE reservations SET status=? WHERE id=?", (RES_DONE, res["id"]))
                        conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                     (SEAT_FREE, now_str(), seat_id))
                        conn.commit()
                        publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id})
                        print(f"[CHECKOUT] Success -> FREE")
                    else:
                        publish_cmd({"cmd": "deny", "seat_id": seat_id})
                    conn.close()

    except Exception as e:
        print(f"[MQTT] Message process error: {e}")


client.on_connect = on_connect
client.on_message = on_message


def start_mqtt():
    def run():
        try:
            client.connect(BROKER, PORT, 60)
            client.loop_forever()
        except Exception as e:
            print(f"MQTT Service Start Failed: {e}")

    t = threading.Thread(target=run)
    t.daemon = True
    t.start()

    t_time = threading.Thread(target=time_broadcast_task)
    t_time.daemon = True
    t_time.start()