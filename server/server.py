# -*- coding: utf-8 -*-  # 指定文件编码为 UTF-8，支持中文注释
import os  # 操作系统接口模块，用于读取环境变量等
import json  # JSON 数据编码解码模块
import time  # 时间相关函数（虽然导入但未使用）
import sqlite3  # SQLite 数据库模块
import threading  # 多线程模块，用于线程锁
from datetime import datetime, timedelta  # 日期时间处理模块
from flask import Flask, request, jsonify, render_template_string  # Flask Web框架
import paho.mqtt.client as mqtt  # MQTT 客户端库

MQTT_HOST = os.getenv("MQTT_HOST", "1.14.163.35")  # 从环境变量读取MQTT主机，默认1.14.163.35
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))  # MQTT端口，默认1883
MQTT_USER = os.getenv("MQTT_USER", "")  # MQTT用户名，默认为空
MQTT_PASS = os.getenv("MQTT_PASS", "")  # MQTT密码，默认为空
MQTT_CMD_TOPIC = "stm32/cmd"  # 发送命令到STM32的主题
MQTT_SUB_TOPIC = "server/#"  # 订阅所有以server/开头的主题
DB_PATH = os.getenv("DB_PATH", "seat_system.db")  # 数据库文件路径

# TOF传感器判断有物品的距离阈值（毫米）：小于该值认为有物品
TOF_OCCUPIED_MM = int(os.getenv("TOF_OCCUPIED_MM", "380"))

# 默认座位列表，格式：(seat_id用于通信, display用于显示)
DEFAULT_SEATS = [
    ("A01", "A区-01号"), ("A02", "A区-02号"), ("A03", "A区-03号"), ("A04", "A区-04号"),
    ("A05", "A区-05号"), ("A06", "A区-06号"), ("A07", "A区-07号"), ("A08", "A区-08号"),
    ("A09", "A区-09号"), ("A10", "A区-10号"), ("A11", "A区-11号"), ("A12", "A区-12号"),
    ("A13", "A区-13号"), ("A14", "A区-14号"), ("A15", "A区-15号"), ("A16", "A区-16号"),
    ("A17", "A区-17号"), ("A18", "A区-18号"), ("A19", "A区-19号"), ("A20", "A区-20号"),
]

# 座位状态枚举常量
SEAT_FREE = "FREE"  # 座位空闲
SEAT_RESERVED = "RESERVED"  # 座位已预约但未使用
SEAT_IN_USE = "IN_USE"  # 座位使用中

# 预约状态枚举常量
RES_ACTIVE = "ACTIVE"  # 预约有效（未签到）
RES_IN_USE = "IN_USE"  # 已签到使用中
RES_DONE = "DONE"  # 已签退
RES_CANCEL = "CANCEL"  # 已取消
RES_EXPIRED = "EXPIRED"  # 已过期

# ===================== Flask =====================
app = Flask(__name__)   # 创建Flask应用实例
lock = threading.Lock() # 创建线程锁，防止多线程数据竞争

# ===================== DB 工具 =====================
def db():  # 数据库连接函数
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)  # 连接SQLite数据库，允许多线程访问
    conn.row_factory = sqlite3.Row  # 设置行工厂，使查询结果可用列名访问
    return conn  # 返回数据库连接

def init_db():  # 初始化数据库函数
    conn = db()  # 获取数据库连接
    c = conn.cursor()  # 创建游标

    # 创建座位表
    # c.execute("""  # 执行SQL语句
    #    CREATE TABLE IF NOT EXISTS seats(  # 如果表不存在则创建
    #        seat_id TEXT PRIMARY KEY,  # 座位ID，主键
    #        display TEXT NOT NULL,  # 显示名称
    #        state TEXT NOT NULL,  # 状态
    #        updated_at TEXT NOT NULL  # 更新时间
    #    )
    #    """)
    c.execute("""
    CREATE TABLE IF NOT EXISTS seats(
        seat_id TEXT PRIMARY KEY,
        display TEXT NOT NULL,
        state TEXT NOT NULL,
        updated_at TEXT NOT NULL
    )
    """)

    # 创建传感器数据表
    # c.execute("""
    #     CREATE TABLE IF NOT EXISTS telemetry(
    #         id INTEGER PRIMARY KEY AUTOINCREMENT,  # 自增ID
    #         seat_id TEXT,  # 座位ID
    #         temp REAL,  # 温度
    #         humi REAL,  # 湿度
    #         lux INTEGER,  # 光照强度
    #         tof_mm INTEGER,  # TOF距离
    #         object_present INTEGER,  # 是否有物体（0/1）
    #         created_at TEXT NOT NULL  # 创建时间
    #     )
    #     """)
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

    # 创建预约表
    # c.execute("""
    #    CREATE TABLE IF NOT EXISTS reservations(
    #        id INTEGER PRIMARY KEY AUTOINCREMENT,
    #        seat_id TEXT NOT NULL,  # 座位ID
    #        user TEXT NOT NULL,  # 用户
    #        status TEXT NOT NULL,  # 状态
    #        uid TEXT,  # RFID卡UID
    #        reserved_at TEXT NOT NULL,  # 预约时间
    #        expires_at TEXT NOT NULL,  # 过期时间
    #        checkin_at TEXT,  # 签到时间
    #        checkout_at TEXT  # 签退时间
    #    )
    #    """)
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

    # 创建占位事件表（座位空闲但有物品）
    # c.execute("""
    #    CREATE TABLE IF NOT EXISTS occupy_incidents(
    #        id INTEGER PRIMARY KEY AUTOINCREMENT,
    #        seat_id TEXT NOT NULL,  # 座位ID
    #        opened_at TEXT NOT NULL,  # 事件开始时间
    #        closed_at TEXT,  # 事件结束时间
    #        last_tof_mm INTEGER  # 最后一次TOF距离
    #    )
    #    """)
    c.execute("""
    CREATE TABLE IF NOT EXISTS occupy_incidents(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        seat_id TEXT NOT NULL,
        opened_at TEXT NOT NULL,
        closed_at TEXT,
        last_tof_mm INTEGER
    )
    """)

    # 初始化座位数据
    for sid, disp in DEFAULT_SEATS:  # 遍历默认座位列表
        c.execute("SELECT seat_id FROM seats WHERE seat_id=?", (sid,))  # 检查座位是否已存在
        if not c.fetchone():  # 如果不存在
            c.execute(  # 插入新座位
                "INSERT INTO seats(seat_id, display, state, updated_at) VALUES(?,?,?,?)",
                (sid, disp, SEAT_FREE, now_str())  # 参数：ID, 显示名, 空闲状态, 当前时间
            )

    conn.commit()  # 提交事务
    conn.close()  # 关闭连接


def now_str():  # 获取当前时间字符串
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")  # 格式化为年-月-日 时:分:秒


def cleanup_expired_reservations():  # 清理过期预约
    """把过期未签到的预约自动释放座位"""
    conn = db()  # 连接数据库
    c = conn.cursor()  # 创建游标
    now = datetime.now()  # 获取当前时间

    # 查询所有活跃预约
    c.execute("SELECT * FROM reservations WHERE status=?", (RES_ACTIVE,))
    rows = c.fetchall()  # 获取所有结果
    for r in rows:  # 遍历每个预约
        exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")  # 解析过期时间字符串为datetime对象
        if now > exp:  # 如果当前时间超过过期时间
            # 更新预约状态为过期
            c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))
            # 如果座位还是RESERVED状态，则释放为FREE
            c.execute("SELECT state FROM seats WHERE seat_id=?", (r["seat_id"],))
            seat = c.fetchone()  # 获取座位状态
            if seat and seat["state"] == SEAT_RESERVED:  # 如果座位处于预约状态
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (SEAT_FREE, now_str(), r["seat_id"]))

    conn.commit()  # 提交事务
    conn.close()  # 关闭连接
# ===================== MQTT =====================
mqtt_client = mqtt.Client()  # 创建MQTT客户端实例

def mqtt_publish_cmd(payload: dict):  # 发布MQTT命令函数
    """服务器 -> STM32：发布命令到 stm32/cmd"""
    s = json.dumps(payload, ensure_ascii=False)  # 将字典转为JSON字符串，确保中文正常
    mqtt_client.publish(MQTT_CMD_TOPIC, s, qos=0, retain=False)  # 发布消息，QoS=0，不保留

def parse_payload(raw: bytes):  # 解析MQTT消息负载
    """兼容 JSON 或 key=value&key=value 格式"""
    try:
        txt = raw.decode("utf-8", errors="ignore").strip()  # 尝试解码为UTF-8，忽略错误
    except Exception:
        txt = str(raw)  # 解码失败则转为字符串

    if not txt:  # 如果为空字符串
        return {}  # 返回空字典

    # 优先尝试解析JSON格式
    if txt.startswith("{") and txt.endswith("}"):  # 判断是否以花括号开头结尾
        try:
            return json.loads(txt)  # 解析JSON
        except Exception:
            pass  # 解析失败则继续尝试其他格式

    # 尝试解析 key=value&key=value 格式
    data = {}  # 创建空字典
    parts = txt.split("&")  # 按&分割
    for p in parts:  # 遍历每个部分
        if "=" in p:  # 如果包含等号
            k, v = p.split("=", 1)  # 分割键和值（只分割第一个=）
            data[k.strip()] = v.strip()  # 去除空格后存入字典
    return data  # 返回解析结果

def object_present_from_tof(tof_mm):  # 根据TOF距离判断是否有物体
    if tof_mm is None:  # 如果距离为None
        return None  # 返回None
    try:
        v = int(tof_mm)  # 尝试转换为整数
    except Exception:
        return None  # 转换失败返回None
    # 过滤 0 或异常大值
    if v <= 0:  # 如果距离小于等于0
        return None  # 返回None
    return 1 if v < TOF_OCCUPIED_MM else 0  # 小于阈值返回1（有物体），否则返回0

def on_mqtt_connect(client, userdata, flags, rc):  # MQTT连接回调函数
    client.subscribe(MQTT_SUB_TOPIC, qos=0)  # 连接成功后订阅主题

def on_mqtt_message(client, userdata, msg):  # MQTT消息接收回调函数
    data = parse_payload(msg.payload)  # 解析消息负载
    if not data:  # 如果解析结果为空
        return  # 直接返回

    # 允许从topic推断消息类型
    tp = data.get("type")  # 从数据中获取type字段
    topic = msg.topic or ""  # 获取消息主题
    if not tp:  # 如果没有type字段
        if "rfid" in topic:  # 如果主题包含rfid
            tp = "rfid"  # 设为rfid类型
        elif "telemetry" in topic or "sensor" in topic:  # 如果主题包含telemetry或sensor
            tp = "telemetry"  # 设为telemetry类型
        else:
            tp = data.get("cmd") or "unknown"  # 使用cmd字段或设为unknown

    with lock:  # 使用线程锁，保证线程安全
        # 每次收到消息先清理过期预约
        cleanup_expired_reservations()

        if tp == "telemetry":  # 如果是传感器数据
            handle_telemetry(data)  # 处理传感器数据
        elif tp == "rfid":  # 如果是RFID数据
            handle_rfid(data)  # 处理RFID数据
        else:
            # 兼容其他格式：如果数据包含uid和seat_id，当做rfid处理
            if "uid" in data and "seat_id" in data:
                handle_rfid(data)
            # 如果包含传感器字段，当做telemetry处理
            elif any(k in data for k in ("temp", "humi", "lux", "tof_mm")):
                handle_telemetry(data)

def handle_telemetry(d):  # 处理传感器数据
    seat_id = str(d.get("seat_id", "")).strip() or None  # 获取座位ID，清理空格
    temp = d.get("temp", None)  # 温度
    humi = d.get("humi", None)  # 湿度
    lux = d.get("lux", None)  # 光照
    tof = d.get("tof_mm", None)  # TOF距离

    obj = d.get("object_present", None)  # 物体存在标志
    if obj is None:  # 如果没有提供该标志
        obj = object_present_from_tof(tof)  # 根据TOF距离计算

    # 存入telemetry表
    conn = db()  # 连接数据库
    c = conn.cursor()  # 创建游标
    c.execute("""  # 插入传感器数据
        INSERT INTO telemetry(seat_id, temp, humi, lux, tof_mm, object_present, created_at)
        VALUES(?,?,?,?,?,?,?)
    """, (seat_id, temp, humi, lux, tof, obj, now_str()))
    conn.commit()  # 提交事务

    # 占位检测：有物品 && 座位空闲(FREE) => 占位事件
    if seat_id:  # 如果有座位ID
        c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,))  # 查询座位状态
        seat = c.fetchone()  # 获取结果
        seat_state = seat["state"] if seat else None  # 提取状态

        if obj == 1 and seat_state == SEAT_FREE:  # 如果有物体且座位空闲
            # 查询该座位是否有未关闭的占位事件
            c.execute("""
                SELECT * FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                ORDER BY id DESC LIMIT 1
            """, (seat_id,))
            inc = c.fetchone()  # 获取未关闭事件
            if inc:  # 如果存在未关闭事件
                # 更新事件的距离值
                c.execute("UPDATE occupy_incidents SET last_tof_mm=? WHERE id=?",
                          (tof if tof is not None else inc["last_tof_mm"], inc["id"]))
            else:  # 如果没有未关闭事件
                # 创建新占位事件
                c.execute("""
                    INSERT INTO occupy_incidents(seat_id, opened_at, closed_at, last_tof_mm)
                    VALUES(?,?,NULL,?)
                """, (seat_id, now_str(), tof))
                # 通知STM32显示警告
                mqtt_publish_cmd({
                    "cmd": "occupy_warn",
                    "seat_id": seat_id
                })
        else:  # 没有物品 或 座位非空闲
            # 查询未关闭的占位事件
            c.execute("""
                SELECT * FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                ORDER BY id DESC LIMIT 1
            """, (seat_id,))
            inc = c.fetchone()  # 获取未关闭事件
            if inc and obj == 0:  # 如果有未关闭事件且现在没有物体
                # 关闭事件（设置关闭时间）
                c.execute("UPDATE occupy_incidents SET closed_at=? WHERE id=?",
                          (now_str(), inc["id"]))

    conn.commit()  # 提交事务
    conn.close()  # 关闭连接

def handle_rfid(d):  # 处理RFID刷卡
    seat_id = str(d.get("seat_id", "")).strip()  # 座位ID
    uid = str(d.get("uid", "")).strip().upper()  # RFID UID，转为大写

    if not seat_id or not uid:  # 如果缺少必要参数
        return  # 直接返回

    conn = db()  # 连接数据库
    c = conn.cursor()  # 创建游标

    # 查找该座位最新的有效预约或使用中预约
    c.execute("""
        SELECT * FROM reservations
        WHERE seat_id=? AND status IN (?,?)
        ORDER BY id DESC LIMIT 1
    """, (seat_id, RES_ACTIVE, RES_IN_USE))
    r = c.fetchone()  # 获取预约记录

    if not r:  # 如果没有预约
        # 发送拒绝消息给STM32
        mqtt_publish_cmd({
            "cmd": "deny",
            "seat_id": seat_id,
            "reason": "NO_RESERVATION",
            "uid": uid
        })
        conn.close()  # 关闭连接
        return  # 返回

    # 检查预约是否过期（保险起见）
    exp = datetime.strptime(r["expires_at"], "%Y-%m-%d %H:%M:%S")  # 解析过期时间
    if datetime.now() > exp and r["status"] == RES_ACTIVE:  # 如果已过期且状态为活跃
        c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_EXPIRED, r["id"]))  # 更新状态
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_FREE, now_str(), seat_id))  # 释放座位
        conn.commit()  # 提交事务
        # 发送过期拒绝消息
        mqtt_publish_cmd({
            "cmd": "deny",
            "seat_id": seat_id,
            "reason": "RES_EXPIRED",
            "uid": uid
        })
        conn.close()  # 关闭连接
        return  # 返回

    # 1) 预约未签到：第一次刷卡 => 绑定UID，签到
    if r["status"] == RES_ACTIVE:  # 如果是活跃预约
        if r["uid"] is None or r["uid"] == "":  # 如果UID为空（未绑定）
            # 更新预约：绑定UID，签到
            c.execute("""
                UPDATE reservations
                SET status=?, uid=?, checkin_at=?
                WHERE id=?
            """, (RES_IN_USE, uid, now_str(), r["id"]))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                      (SEAT_IN_USE, now_str(), seat_id))  # 更新座位状态为使用中
            conn.commit()  # 提交事务

            # 发送签到成功消息
            mqtt_publish_cmd({
                "cmd": "checkin_ok",
                "seat_id": seat_id,
                "reservation_id": r["id"],
                "uid": uid
            })
        else:  # 如果UID已绑定
            if r["uid"].upper() != uid:  # 如果刷的卡与绑定的UID不匹配
                # 发送UID不匹配拒绝消息
                mqtt_publish_cmd({
                    "cmd": "deny",
                    "seat_id": seat_id,
                    "reason": "UID_MISMATCH",
                    "uid": uid
                })
            else:  # 同一UID重复刷卡
                # 发送签到确认消息（状态不变）
                mqtt_publish_cmd({
                    "cmd": "checkin_ok",
                    "seat_id": seat_id,
                    "reservation_id": r["id"],
                    "uid": uid
                })

    # 2) 使用中：同一UID再刷一次 => 签退
    elif r["status"] == RES_IN_USE:  # 如果正在使用中
        if (r["uid"] or "").upper() == uid:  # 如果是同一UID
            # 更新预约：签退
            c.execute("""
                UPDATE reservations
                SET status=?, checkout_at=?
                WHERE id=?
            """, (RES_DONE, now_str(), r["id"]))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                      (SEAT_FREE, now_str(), seat_id))  # 释放座位
            conn.commit()  # 提交事务

            # 发送签退成功消息
            mqtt_publish_cmd({
                "cmd": "checkout_ok",
                "seat_id": seat_id,
                "reservation_id": r["id"],
                "uid": uid
            })
        else:  # 不同的UID
            # 发送UID不匹配拒绝消息
            mqtt_publish_cmd({
                "cmd": "deny",
                "seat_id": seat_id,
                "reason": "UID_MISMATCH",
                "uid": uid
            })

    conn.close()  # 关闭连接

# MQTT 初始化
def start_mqtt():  # 启动MQTT客户端
    if MQTT_USER:  # 如果有用户名
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)  # 设置认证信息

    mqtt_client.on_connect = on_mqtt_connect  # 设置连接回调
    mqtt_client.on_message = on_mqtt_message  # 设置消息回调
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)  # 连接MQTT服务器
    mqtt_client.loop_start()  # 启动消息循环（后台线程）

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
      <input id="userName" placeholder="请输入姓名/学号">
    </div>
  </div>
  <div class="grid2" style="margin-top:10px">
    <div>
      <div class="small">预约时长(分钟)</div>
      <input id="minutes" value="1">
    </div>
    <div style="display:flex;align-items:flex-end">
      <button onclick="reserve()">提交预约</button>
    </div>
  </div>
  <div class="small" id="reserveResult" style="margin-top:8px;color:#0b4ea2;"></div>
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
async function fetchState(){
  const r = await fetch('/api/state');
  const s = await r.json();

  // KPIs（取最新一条 telemetry）
  if(s.latest){
    document.getElementById('temp').innerText = (s.latest.temp ?? '--') + ' ℃';
    document.getElementById('humi').innerText = (s.latest.humi ?? '--') + ' %';
    document.getElementById('lux').innerText  = (s.latest.lux  ?? '--') + ' lux';
    document.getElementById('last_update').innerText = '更新时间：' + (s.latest.created_at ?? '--') + (s.latest.seat_id ? ('（来源座位 '+s.latest.seat_id+'）') : '');
  }

  // 座位下拉
  const sel = document.getElementById('seatSelect');
  sel.innerHTML = '';
  s.seats.forEach(it=>{
    const opt = document.createElement('option');
    opt.value = it.seat_id;
    opt.textContent = it.display + ' (' + it.state + ')';
    opt.disabled = (it.state !== 'FREE');
    sel.appendChild(opt);
  });

  // 座位表格
  const tb = document.getElementById('seatTable');
  tb.innerHTML = '';
  s.seats.forEach(it=>{
    const tr = document.createElement('tr');
    const badge = it.state==='FREE'?'free':(it.state==='RESERVED'?'reserved':'inuse');

    const res = it.active_reservation ? (it.active_reservation.user + ' | ' + it.active_reservation.status +
      ' | 到期 ' + it.active_reservation.expires_at) : '--';

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

fetchState();
setInterval(fetchState, 1500);
</script>
</body>
</html>
"""

# ===================== API =====================
@app.route("/")  # 根路径路由
def index():  # 首页处理函数
    return render_template_string(INDEX_HTML)  # 渲染HTML模板

@app.route("/api/state")  # 状态API
def api_state():  # 获取系统状态
    with lock:  # 线程锁
        cleanup_expired_reservations()  # 清理过期预约
        conn = db()  # 连接数据库
        c = conn.cursor()  # 创建游标

        # 获取最新传感器数据
        c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1")
        latest = c.fetchone()  # 获取一条记录
        latest_obj = dict(latest) if latest else None  # 转换为字典或None

        # 获取未关闭的占位事件
        c.execute("SELECT * FROM occupy_incidents WHERE closed_at IS NULL ORDER BY id DESC LIMIT 20")
        inc = [dict(x) for x in c.fetchall()]  # 转换为字典列表

        # 获取所有座位
        c.execute("SELECT * FROM seats ORDER BY seat_id")
        seat_rows = c.fetchall()  # 获取所有座位

        seats = []  # 座位列表
        for s in seat_rows:  # 遍历每个座位
            sid = s["seat_id"]  # 座位ID

            # 查询活跃预约
            c.execute("""
                SELECT * FROM reservations
                WHERE seat_id=? AND status IN (?,?)
                ORDER BY id DESC LIMIT 1
            """, (sid, RES_ACTIVE, RES_IN_USE))
            ar = c.fetchone()  # 获取预约
            ar_obj = dict(ar) if ar else None  # 转换为字典或None

            # 检查是否有占位事件
            c.execute("""
                SELECT 1 FROM occupy_incidents
                WHERE seat_id=? AND closed_at IS NULL
                LIMIT 1
            """, (sid,))
            occupy_active = True if c.fetchone() else False  # 布尔值表示是否占位

            # 构建座位信息字典
            seats.append({
                "seat_id": sid,
                "display": s["display"],
                "state": s["state"],
                "updated_at": s["updated_at"],
                "active_reservation": ar_obj,
                "occupy_active": occupy_active
            })

        conn.close()  # 关闭连接
        # 返回JSON响应
        return jsonify({
            "ok": True,
            "latest": latest_obj,
            "seats": seats,
            "incidents": inc
        })

@app.route("/api/reserve", methods=["POST"])  # 预约API
def api_reserve():  # 处理预约请求
    body = request.get_json(force=True, silent=True) or {}  # 获取JSON请求体
    seat_id = str(body.get("seat_id","")).strip()  # 座位ID
    user = str(body.get("user","")).strip()  # 用户名
    minutes = int(body.get("minutes", 120))  # 预约时长（分钟）

    # 参数验证
    if not seat_id or not user:  # 如果缺少参数
        return jsonify({"ok": False, "error": "参数缺失"}), 400  # 返回400错误
    if minutes < 10:  # 最小10分钟
        minutes = 10
    if minutes > 8*60:  # 最大8小时
        minutes = 8*60

    with lock:  # 线程锁
        cleanup_expired_reservations()  # 清理过期预约
        conn = db()  # 连接数据库
        c = conn.cursor()  # 创建游标

        # 检查座位是否存在
        c.execute("SELECT * FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()  # 获取座位
        if not seat:  # 座位不存在
            conn.close()
            return jsonify({"ok": False, "error": "座位不存在"}), 404
        if seat["state"] != SEAT_FREE:  # 座位非空闲
            conn.close()
            return jsonify({"ok": False, "error": "座位非空闲"}), 409

        # 计算预约时间
        reserved_at = now_str()  # 当前时间
        expires_at = (datetime.now() + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S")  # 过期时间

        # 插入预约记录
        c.execute("""
            INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at,checkin_at,checkout_at)
            VALUES(?,?,?,?,?,?,NULL,NULL)
        """, (seat_id, user, RES_ACTIVE, None, reserved_at, expires_at))
        rid = c.lastrowid  # 获取新插入记录的ID

        # 更新座位状态
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_RESERVED, now_str(), seat_id))

        conn.commit()  # 提交事务
        conn.close()  # 关闭连接

        # 通知STM32
        mqtt_publish_cmd({
            "cmd": "reserve",
            "seat_id": seat_id,
            "reservation_id": rid,
            "user": user,
            "expires_at": expires_at
        })

        return jsonify({"ok": True, "reservation_id": rid})  # 返回成功响应

@app.route("/api/cancel", methods=["POST"])  # 取消预约API
def api_cancel():  # 处理取消预约
    body = request.get_json(force=True, silent=True) or {}  # 获取请求体
    rid = int(body.get("reservation_id", 0))  # 预约ID
    if rid <= 0:  # 无效ID
        return jsonify({"ok": False, "error": "参数缺失"}), 400

    with lock:  # 线程锁
        conn = db()  # 连接数据库
        c = conn.cursor()  # 创建游标
        c.execute("SELECT * FROM reservations WHERE id=?", (rid,))  # 查询预约
        r = c.fetchone()  # 获取预约记录
        if not r:  # 预约不存在
            conn.close()
            return jsonify({"ok": False, "error": "预约不存在"}), 404

        if r["status"] not in (RES_ACTIVE,):  # 只有活跃预约可取消
            conn.close()
            return jsonify({"ok": False, "error": "当前状态不可取消"}), 409

        # 更新预约状态
        c.execute("UPDATE reservations SET status=? WHERE id=?", (RES_CANCEL, rid))
        # 释放座位
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                  (SEAT_FREE, now_str(), r["seat_id"]))
        conn.commit()  # 提交事务
        conn.close()  # 关闭连接

        # 通知STM32
        mqtt_publish_cmd({
            "cmd": "cancel",
            "seat_id": r["seat_id"],
            "reservation_id": rid
        })
        return jsonify({"ok": True})  # 返回成功

# ===================== 启动 =====================
if __name__ == "__main__":  # 主程序入口
    init_db()  # 初始化数据库
    start_mqtt()  # 启动MQTT客户端
    # 启动Flask应用
    app.run(host="0.0.0.0", port=int(os.getenv("PORT", "5000")), debug=False)
