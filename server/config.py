# config.py
import os

# MQTT 配置
MQTT_HOST = os.getenv("MQTT_HOST", "1.14.163.35")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "test01")
MQTT_PASS = os.getenv("MQTT_PASS", "")
MQTT_CMD_TOPIC = "stm32/cmd"
MQTT_SUB_TOPIC = "server/#"

# 数据库配置
DB_PATH = os.getenv("DB_PATH", "seat_system.db")

# 业务参数
TOF_OCCUPIED_MM = int(os.getenv("TOF_OCCUPIED_MM", "380"))

# 座位列表定义
DEFAULT_SEATS = [
    ("A01", "A区-01号"), ("A02", "A区-02号"), ("A03", "A区-03号"), ("A04", "A区-04号"),
    ("A05", "A区-05号"), ("A06", "A区-06号"), ("A07", "A区-07号"), ("A08", "A区-08号"),
    ("A09", "A区-09号"), ("A10", "A区-10号"), ("A11", "A区-11号"), ("A12", "A区-12号"),
    ("A13", "A区-13号"), ("A14", "A区-14号"), ("A15", "A区-15号"), ("A16", "A区-16号"),
    ("A17", "A区-17号"), ("A18", "A区-18号"), ("A19", "A区-19号"), ("A20", "A区-20号"),
]

# 状态常量
SEAT_FREE = "FREE"
SEAT_RESERVED = "RESERVED"
SEAT_IN_USE = "IN_USE"

RES_ACTIVE = "ACTIVE"
RES_IN_USE = "IN_USE"
RES_DONE = "DONE"
RES_CANCEL = "CANCEL"
RES_EXPIRED = "EXPIRED"