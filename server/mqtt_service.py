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
    """定时广播时间"""
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

        # 解析 payload (kv格式)
        data = {}
        for part in payload.split('&'):
            if '=' in part:
                k, v = part.split('=', 1)
                data[k] = v.strip()

        seat_id = data.get("seat_id")
        msg_type = data.get("type")

        if not seat_id: return

        # 1. 处理同步请求 (sync)
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

        # 2. 处理设备上报的状态/遥测 (state/telemetry) -> 解决“离线”问题
        if msg_type == "state" or msg_type == "telemetry":
            with db_lock:
                conn = get_conn()
                # 只有 state 类型才更新 status
                if msg_type == "state" and "state" in data:
                    conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                 (data["state"], now_str(), seat_id))

                # 更新遥测数据 (只要有数据传来，就视为在线，写入 telemetry 表)
                if "temp" in data or "humi" in data or "tof_mm" in data:
                    # 确保数值有效，防止转换错误
                    temp = float(data.get("temp", 0))
                    humi = int(float(data.get("humi", 0)))
                    lux = int(float(data.get("lux", 0)))
                    tof = int(float(data.get("tof_mm", 0)))

                    conn.execute(
                        "INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, created_at) VALUES(?,?,?,?,?,?)",
                        (seat_id, temp, humi, lux, tof, now_str()))

                    # 同时更新 seats 表的 updated_at，这对于判断在线状态至关重要！
                    conn.execute("UPDATE seats SET updated_at=? WHERE seat_id=?", (now_str(), seat_id))

                conn.commit()
                conn.close()
            return

        # 3. 处理业务事件 (event) -> 解决“无法签到”问题
        if msg_type == "event":
            cmd = data.get("cmd")
            uid_hex = data.get("uid")

            if cmd == "checkin":  # 处理签到
                with db_lock:
                    conn = get_conn()
                    # 查找该座位当前的“有效预约”
                    res = conn.execute(
                        "SELECT id, uid FROM reservations WHERE seat_id=? AND status=? ORDER BY id DESC LIMIT 1",
                        (seat_id, RES_ACTIVE)
                    ).fetchone()

                    if res:
                        # 校验 UID (忽略大小写)
                        if res["uid"].upper() == uid_hex.upper():
                            # 签到成功：更新预约状态 -> IN_USE
                            conn.execute("UPDATE reservations SET status=? WHERE id=?", (RES_IN_USE, res["id"]))
                            conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                         (SEAT_IN_USE, now_str(), seat_id))
                            conn.commit()
                            # 回复设备
                            publish_cmd({"cmd": "checkin_ok", "seat_id": seat_id})
                            print(f"[EVENT] Checkin SUCCESS for {seat_id}")
                        else:
                            # 签到失败：UID 不匹配
                            publish_cmd({"cmd": "deny", "seat_id": seat_id})
                            print(f"[EVENT] Checkin DENIED for {seat_id}: UID mismatch")
                    else:
                        # 签到失败：无预约
                        publish_cmd({"cmd": "deny", "seat_id": seat_id})
                    conn.close()

            elif cmd == "checkout":  # 处理签退
                with db_lock:
                    conn = get_conn()
                    res = conn.execute(
                        "SELECT id, uid FROM reservations WHERE seat_id=? AND status=? ORDER BY id DESC LIMIT 1",
                        (seat_id, RES_IN_USE)
                    ).fetchone()

                    if res and res["uid"].upper() == uid_hex.upper():
                        # 签退成功：更新预约状态 -> DONE
                        conn.execute("UPDATE reservations SET status=? WHERE id=?", (RES_DONE, res["id"]))
                        conn.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                                     (SEAT_FREE, now_str(), seat_id))
                        conn.commit()
                        # 回复设备
                        publish_cmd({"cmd": "checkout_ok", "seat_id": seat_id})
                        print(f"[EVENT] Checkout SUCCESS for {seat_id}")
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