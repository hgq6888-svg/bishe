import os

# 数据库路径
DB_PATH = os.path.join(os.path.dirname(__file__), "seat_system.db")

# MQTT配置 (请修改为您实际的MQTT服务器信息)
MQTT_HOST = "1.14.163.35"  # 与STM32代码一致
MQTT_PORT = 1883
MQTT_USER = "test01"
MQTT_PASS = ""             # 如果有密码请填入

# 订阅主题：监听所有设备的上传数据 (对应STM32的 server/+/+)
MQTT_SUB_TOPIC = "server/#"
# 发布主题：向设备发送指令 (对应STM32订阅的 stm32/cmd)
MQTT_CMD_TOPIC = "stm32/cmd"

# 业务参数
DEFAULT_SEATS = [("A18", "座位 A18")]  # 默认初始化的座位
SEAT_FREE = "FREE"
SEAT_RESERVED = "RESERVED"
SEAT_IN_USE = "IN_USE"

# 预约状态
RES_ACTIVE = "ACTIVE"
RES_IN_USE = "IN_USE"
RES_DONE = "DONE"
RES_CANCEL = "CANCEL"
RES_EXPIRED = "EXPIRED"

# 阈值配置
TOF_OCCUPIED_MM = 380  # 小于此距离视为有人