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
    )""")

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
    )""")

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
    )""")

    # 4. 用户表 (包含 role 和 uid)
    c.execute("""
    CREATE TABLE IF NOT EXISTS users(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        password TEXT DEFAULT '123456',
        uid TEXT,
        role TEXT DEFAULT 'user',
        created_at TEXT
    )""")

    # --- 初始化数据 ---

    # 默认管理员 admin/123456
    if not c.execute("SELECT id FROM users WHERE username='admin'").fetchone():
        c.execute("INSERT INTO users(username, password, role, created_at) VALUES(?,?,?,?)",
                  ('admin', '123456', 'admin', now_str()))

    # 初始化座位
    for sid, disp in DEFAULT_SEATS:
        if not c.execute("SELECT seat_id FROM seats WHERE seat_id=?", (sid,)).fetchone():
            c.execute("INSERT INTO seats(seat_id, display, state, updated_at) VALUES(?,?,?,?)",
                      (sid, disp, SEAT_FREE, now_str()))

    conn.commit()
    conn.close()