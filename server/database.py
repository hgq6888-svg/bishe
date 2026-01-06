# database.py
import sqlite3
import threading
from datetime import datetime
from config import DB_PATH, DEFAULT_SEATS, SEAT_FREE

db_lock = threading.Lock()

def get_conn():
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn

def now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def init_db():
    conn = get_conn()
    c = conn.cursor()

    # 1. 座位表
    c.execute("""
    CREATE TABLE IF NOT EXISTS seats(
        seat_id TEXT PRIMARY KEY,
        display TEXT NOT NULL,
        state TEXT NOT NULL,
        light_on INTEGER DEFAULT 0,
        light_mode TEXT DEFAULT 'MANUAL',
        updated_at TEXT NOT NULL
    )
    """)
    try: c.execute("ALTER TABLE seats ADD COLUMN light_on INTEGER DEFAULT 0")
    except: pass
    try: c.execute("ALTER TABLE seats ADD COLUMN light_mode TEXT DEFAULT 'MANUAL'")
    except: pass

    # 2. 环境数据表
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

    # 3. 预约表
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

    # 4. 占位事件表
    c.execute("""
    CREATE TABLE IF NOT EXISTS occupy_incidents(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        seat_id TEXT NOT NULL,
        opened_at TEXT NOT NULL,
        closed_at TEXT,
        last_tof_mm INTEGER
    )
    """)

    # 5. 用户表 (增加密码字段)
    c.execute("""
    CREATE TABLE IF NOT EXISTS users(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        password TEXT, 
        uid TEXT,
        created_at TEXT
    )
    """)
    # 【新增】尝试添加 password 列兼容旧库
    try: c.execute("ALTER TABLE users ADD COLUMN password TEXT DEFAULT '123456'")
    except: pass

    # 初始化默认座位
    for sid, disp in DEFAULT_SEATS:
        c.execute("SELECT seat_id FROM seats WHERE seat_id=?", (sid,))
        if not c.fetchone():
            c.execute(
                "INSERT INTO seats(seat_id, display, state, updated_at) VALUES(?,?,?,?)",
                (sid, disp, SEAT_FREE, now_str())
            )

    conn.commit()
    conn.close()