from flask import Flask, request, jsonify, render_template, session, redirect, url_for, render_template_string
from datetime import datetime, timedelta
import config
import database
import mqtt_service
import math

app = Flask(__name__)
app.secret_key = "bishe_secret_key_123"

# --- 登录与注册模板 (保持不变) ---
LOGIN_HTML = """
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>登录 - 座位系统</title>
<style>
body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background:#f3f4f6}
.card{background:white;padding:2rem;border-radius:10px;box-shadow:0 4px 10px rgba(0,0,0,0.1);width:300px}
input{width:100%;padding:10px;margin-bottom:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}
button{width:100%;padding:10px;background:#3b82f6;color:white;border:none;border-radius:5px;cursor:pointer}
button:hover{background:#2563eb}
.error{color:red;font-size:0.9em;margin-bottom:10px;text-align:center}
.link{display:block;text-align:center;margin-top:15px;color:#3b82f6;text-decoration:none;font-size:0.9em}
</style>
</head>
<body>
<div class="card">
    <h2 style="text-align:center;color:#333">用户登录</h2>
    {% if error %}<div class="error">{{ error }}</div>{% endif %}
    {% if msg %}<div style="color:green;text-align:center;margin-bottom:10px">{{ msg }}</div>{% endif %}
    <form method="POST" action="/login">
        <input type="text" name="username" placeholder="用户名" required>
        <input type="password" name="password" placeholder="密码" required>
        <button type="submit">登 录</button>
    </form>
    <a href="/register" class="link">注册新账号 -></a>
</div>
</body>
</html>
"""

REGISTER_HTML = """
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>注册</title>
<style>
body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background:#f3f4f6}
.card{background:white;padding:2rem;border-radius:10px;box-shadow:0 4px 10px rgba(0,0,0,0.1);width:300px}
input{width:100%;padding:10px;margin-bottom:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}
button{width:100%;padding:10px;background:#10b981;color:white;border:none;border-radius:5px;cursor:pointer}
button:hover{background:#059669}
.error{color:red;font-size:0.9em;margin-bottom:10px;text-align:center}
.link{display:block;text-align:center;margin-top:15px;color:#3b82f6;text-decoration:none;font-size:0.9em}
</style>
</head>
<body>
<div class="card">
    <h2 style="text-align:center;color:#333">注册账号</h2>
    {% if error %}<div class="error">{{ error }}</div>{% endif %}
    <form method="POST" action="/register">
        <input type="text" name="username" placeholder="设置用户名" required>
        <input type="password" name="password" placeholder="设置密码" required>
        <input type="password" name="confirm_password" placeholder="确认密码" required>
        <button type="submit">注 册</button>
    </form>
    <a href="/login" class="link"><- 返回登录</a>
</div>
</body>
</html>
"""


# --- 路由逻辑 ---

@app.route("/")
def index():
    if not session.get("user"): return redirect(url_for("login"))
    return render_template("index.html", current_user=session["user"], role=session.get("role", "user"))


@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        u = request.form.get("username")
        p = request.form.get("password")
        conn = database.get_conn()
        user = conn.execute("SELECT * FROM users WHERE username=?", (u,)).fetchone()
        conn.close()

        if not user:
            return render_template_string(LOGIN_HTML, error="用户不存在")

        # 密码比对
        db_pass = user["password"] if user["password"] else "123456"
        if p == db_pass:
            session["user"] = u
            session["role"] = user["role"]
            return redirect(url_for("index"))
        else:
            return render_template_string(LOGIN_HTML, error="密码错误")
    return render_template_string(LOGIN_HTML)


@app.route("/register", methods=["GET", "POST"])
def register():
    if request.method == "POST":
        u = request.form.get("username")
        p = request.form.get("password")
        cp = request.form.get("confirm_password")

        if not u or not p: return render_template_string(REGISTER_HTML, error="不能为空")
        if p != cp: return render_template_string(REGISTER_HTML, error="两次密码不一致")

        conn = database.get_conn()
        if conn.execute("SELECT id FROM users WHERE username=?", (u,)).fetchone():
            conn.close()
            return render_template_string(REGISTER_HTML, error="用户名已存在")

        try:
            conn.execute("INSERT INTO users(username, password, role, created_at) VALUES(?,?,?,?)",
                         (u, p, 'user', database.now_str()))
            conn.commit()
            conn.close()
            return render_template_string(LOGIN_HTML, msg="注册成功，请登录")
        except Exception as e:
            conn.close()
            return render_template_string(REGISTER_HTML, error=str(e))

    return render_template_string(REGISTER_HTML)


@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))


# --- 业务 API ---

@app.route("/api/state")
def api_state():
    if not session.get("user"): return jsonify({"error": "Unauthorized"}), 401
    try:
        with database.db_lock:
            conn = database.get_conn()
            c = conn.cursor()

            # 1. 自动检查并清理“超时未签到”的预约
            utc_now = datetime.utcnow()
            beijing_now = utc_now + timedelta(hours=8)
            now_str = beijing_now.strftime("%Y-%m-%d %H:%M:%S")

            expired = c.execute("SELECT id, seat_id FROM reservations WHERE status=? AND expires_at < ?",
                                (config.RES_ACTIVE, now_str)).fetchall()

            for r in expired:
                rid = r["id"]
                sid = r["seat_id"]
                c.execute("UPDATE reservations SET status=? WHERE id=?", (config.RES_CANCEL, rid))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (config.SEAT_FREE, now_str, sid))
                mqtt_service.publish_cmd({"cmd": "release", "seat_id": sid})

            if len(expired) > 0:
                conn.commit()

            # 2. 获取最新环境数据 (用于判断在线状态)
            # 修复：判断在线时，使用 telemetry 的 created_at 与 当前北京时间 比较
            tele = c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1").fetchone()
            latest = dict(tele) if tele else None

            # 判断在线状态逻辑：如果最近5分钟有数据
            # 注意：mqtt_service 更新 telemetry 时也会更新 seats.updated_at，可以用 seats.updated_at 判断

            # 3. 获取座位列表
            seats = []
            for s in c.execute("SELECT * FROM seats ORDER BY seat_id").fetchall():
                s_obj = dict(s)
                # 附带当前活跃预约信息
                res = c.execute(
                    "SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
                    (s["seat_id"], config.RES_ACTIVE, config.RES_IN_USE)).fetchone()
                s_obj["active_reservation"] = dict(res) if res else None

                # 单独判断该座位在线状态 (300秒超时)
                s_obj["is_online"] = False
                if s["updated_at"]:
                    try:
                        last_upd = datetime.strptime(s["updated_at"], "%Y-%m-%d %H:%M:%S")
                        if (beijing_now - last_upd).total_seconds() < 300:
                            s_obj["is_online"] = True
                    except:
                        pass

                seats.append(s_obj)

            conn.close()
            return jsonify({"ok": True, "latest": latest, "seats": seats})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route("/api/stats")
def api_stats():
    """
    增强版报表统计接口
    """
    if not session.get("user"): return jsonify({"error": "Unauthorized"}), 401
    if session.get("role") != "admin": return jsonify({"error": "Forbidden"}), 403

    try:
        days_param = int(request.args.get("days", 7))
    except:
        days_param = 7

    conn = database.get_conn()
    c = conn.cursor()

    # 计算时间范围
    start_date = (datetime.now() - timedelta(days=days_param)).strftime("%Y-%m-%d %H:%M:%S")

    # 1. 环境趋势 (带降采样)
    raw_rows = c.execute("SELECT temp, humi, lux, created_at FROM telemetry WHERE created_at > ? ORDER BY id ASC",
                         (start_date,)).fetchall()

    # 降采样
    TARGET_POINTS = 60
    sampled_rows = []
    if len(raw_rows) > TARGET_POINTS:
        step = len(raw_rows) / TARGET_POINTS
        for i in range(TARGET_POINTS):
            start_idx = int(i * step)
            end_idx = int((i + 1) * step)
            chunk = raw_rows[start_idx:end_idx]
            if not chunk: continue
            avg_t = sum(r["temp"] for r in chunk) / len(chunk)
            avg_h = sum(r["humi"] for r in chunk) / len(chunk)
            mid_time = chunk[len(chunk) // 2]["created_at"]
            sampled_rows.append({"temp": round(avg_t, 1), "humi": round(avg_h, 1), "created_at": mid_time})
    else:
        sampled_rows = [dict(r) for r in raw_rows]

    timestamps = [r["created_at"][5:16] for r in sampled_rows]
    temps = [r["temp"] for r in sampled_rows]
    humis = [r["humi"] for r in sampled_rows]

    # 预测
    pred_labels = []
    future_t = []
    future_h = []
    if len(temps) >= 5:
        try:
            last_n = temps[-5:]
            x_bar = 2.0
            y_bar = sum(last_n) / 5
            denominator = 10.0
            k = sum((i - x_bar) * (last_n[i] - y_bar) for i in range(5)) / denominator
            b = y_bar - k * x_bar
            future_t = [round(k * (5 + i) + b, 1) for i in range(3)]

            last_n_h = humis[-5:]
            y_bar_h = sum(last_n_h) / 5
            k_h = sum((i - x_bar) * (last_n_h[i] - y_bar_h) for i in range(5)) / denominator
            b_h = y_bar_h - k_h * x_bar
            future_h = [round(k_h * (5 + i) + b_h, 1) for i in range(3)]
            pred_labels = ["预测+1", "预测+2", "预测+3"]
        except:
            pass

    env_data = {
        "labels": timestamps, "temp": temps, "humi": humis,
        "pred_labels": pred_labels, "pred_temp": future_t, "pred_humi": future_h
    }

    # 2. 预约量热力图
    heat_rows = c.execute(f"""
        SELECT strftime('%H', reserved_at) as hour, COUNT(*) as cnt 
        FROM reservations WHERE reserved_at > ? GROUP BY hour
    """, (start_date,)).fetchall()
    heatmap_data = {str(i).zfill(2): 0 for i in range(24)}
    peak_hour = "-";
    peak_val = 0
    for r in heat_rows:
        h = r["hour"];
        c_val = r["cnt"]
        heatmap_data[h] = c_val
        if c_val > peak_val: peak_val = c_val; peak_hour = h + ":00"

    # 3. 座位热门度
    seat_rows = c.execute(f"""
        SELECT s.seat_id, s.display, COUNT(r.id) as cnt 
        FROM seats s LEFT JOIN reservations r ON s.seat_id = r.seat_id AND r.reserved_at > ?
        GROUP BY s.seat_id ORDER BY cnt DESC
    """, (start_date,)).fetchall()
    seat_map = {r["seat_id"]: r["cnt"] for r in seat_rows}
    top_seat_name = seat_rows[0]["display"] if seat_rows and seat_rows[0]["cnt"] > 0 else "暂无"

    # 4. 关联性
    corr_rows = c.execute(f"""
        SELECT substr(created_at, 1, 10) as day, AVG(temp) as avg_t
        FROM telemetry WHERE created_at > ? GROUP BY day
    """, (start_date,)).fetchall()
    res_per_day = c.execute(f"""
        SELECT substr(reserved_at, 1, 10) as day, COUNT(*) as cnt
        FROM reservations WHERE reserved_at > ? GROUP BY day
    """, (start_date,)).fetchall()
    res_dict = {r["day"]: r["cnt"] for r in res_per_day}
    scatter_data = []
    for r in corr_rows:
        d = r["day"]
        if d in res_dict: scatter_data.append({"x": round(r["avg_t"], 1), "y": res_dict[d]})

    # 5. 统计
    avg_dur_row = c.execute(f"""
        SELECT AVG((julianday(expires_at) - julianday(reserved_at)) * 24 * 60) as avg_min
        FROM reservations WHERE reserved_at > ?
    """, (start_date,)).fetchone()
    avg_duration = round(avg_dur_row["avg_min"] or 0, 0)

    # 设备在线率
    last_tele = c.execute("SELECT created_at FROM telemetry ORDER BY id DESC LIMIT 1").fetchone()
    is_online = False
    if last_tele:
        try:
            utc_now = datetime.utcnow()
            beijing_now = utc_now + timedelta(hours=8)
            last_time = datetime.strptime(last_tele["created_at"], "%Y-%m-%d %H:%M:%S")
            if (beijing_now - last_time).total_seconds() < 300: is_online = True
        except:
            pass

    conn.close()
    return jsonify({
        "ok": True, "env": env_data, "heatmap": heatmap_data, "seat_map": seat_map, "scatter": scatter_data,
        "stats": {"avg_duration": avg_duration, "peak_hour": peak_hour, "top_seat": top_seat_name,
                  "device_online": is_online}
    })


@app.route("/api/admin/alerts")
def api_admin_alerts():
    if session.get("role") != "admin": return jsonify({"error": "Forbidden"}), 403
    conn = database.get_conn()
    c = conn.cursor()
    alerts = []

    tele = c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1").fetchone()

    if tele and "tof_mm" in tele.keys():
        dist = tele["tof_mm"]
        source_seat_id = tele["seat_id"] if "seat_id" in tele.keys() else "A18"

        seats = c.execute("SELECT * FROM seats").fetchall()
        for s in seats:
            if str(s["seat_id"]) != str(source_seat_id): continue

            state = s["state"]
            seat_name = s["display"]

            if state == config.SEAT_FREE and dist < 600:
                alerts.append(
                    {"id": s["seat_id"], "name": seat_name, "type": "illegal", "msg": "非法占座：未预约但检测到有人",
                     "val": f"测距 {dist}mm"})
            elif state == config.SEAT_IN_USE and dist > 1000:
                alerts.append(
                    {"id": s["seat_id"], "name": seat_name, "type": "ghost", "msg": "人走未退：状态为使用中但检测无人",
                     "val": f"测距 {dist}mm"})

    conn.close()
    return jsonify({"ok": True, "alerts": alerts})


@app.route("/api/user/profile")
def api_user_profile():
    u = session.get("user")
    if not u: return jsonify({"error": "Auth required"}), 401
    conn = database.get_conn()
    user = conn.execute("SELECT username, uid, role FROM users WHERE username=?", (u,)).fetchone()
    conn.close()
    if user:
        return jsonify({"ok": True, "username": user["username"], "uid": user["uid"] or ""})
    return jsonify({"ok": False, "error": "User not found"}), 404


@app.route("/api/user/bind", methods=["POST"])
def api_user_bind():
    u = session.get("user")
    if not u: return jsonify({"error": "Auth required"}), 401
    uid = request.get_json(force=True).get("uid", "").strip().upper()
    if not uid: return jsonify({"error": "卡号不能为空"}), 400

    conn = database.get_conn()
    dup = conn.execute("SELECT username FROM users WHERE uid=? AND username!=?", (uid, u)).fetchone()
    if dup:
        conn.close()
        return jsonify({"ok": False, "error": f"该卡号已被 {dup['username']} 绑定"}), 400

    conn.execute("UPDATE users SET uid=? WHERE username=?", (uid, u))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


@app.route("/api/reserve", methods=["POST"])
def api_reserve():
    current_user = session.get("user")
    if not current_user: return jsonify({"ok": False, "error": "请登录"}), 401

    body = request.get_json(force=True, silent=True) or {}
    seat_id = body.get("seat_id")
    minutes = int(body.get("minutes", 120))

    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()

        u_row = c.execute("SELECT uid FROM users WHERE username=?", (current_user,)).fetchone()
        user_uid = u_row["uid"] if u_row else None
        if not user_uid or len(user_uid.strip()) == 0:
            conn.close()
            return jsonify({"ok": False, "error": "请先绑定实体卡号", "need_bind": True}), 400

        if c.execute("SELECT id FROM reservations WHERE user=? AND status IN (?,?)",
                     (current_user, config.RES_ACTIVE, config.RES_IN_USE)).fetchone():
            conn.close()
            return jsonify({"ok": False, "error": "您当前已有预约，不可重复预约"}), 400

        seat = c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,)).fetchone()
        if not seat or seat["state"] != config.SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "座位已被占用"}), 400

        now = database.now_str()
        utc_now = datetime.utcnow()
        beijing_now = utc_now + timedelta(hours=8)

        checkin_deadline = beijing_now + timedelta(minutes=15)
        exp = checkin_deadline.strftime("%Y-%m-%d %H:%M:%S")

        c.execute("INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at) VALUES(?,?,?,?,?,?)",
                  (seat_id, current_user, config.RES_ACTIVE, user_uid, now, exp))
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_RESERVED, now, seat_id))
        conn.commit()
        conn.close()

        mqtt_service.publish_cmd({
            "cmd": "reserve", "seat_id": seat_id, "user": current_user,
            "uid": user_uid, "expires_at": exp
        })
        return jsonify({"ok": True})


@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    u = session.get("user")
    if not u: return jsonify({"error": "Auth required"}), 401

    rid = request.get_json(force=True).get("reservation_id")
    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()
        r = c.execute("SELECT * FROM reservations WHERE id=?", (rid,)).fetchone()

        if r and (r["user"] == u or session.get("role") == "admin"):
            if r["status"] in (config.RES_ACTIVE, config.RES_IN_USE):
                new_st = config.RES_CANCEL if r["status"] == config.RES_ACTIVE else config.RES_DONE
                c.execute("UPDATE reservations SET status=? WHERE id=?", (new_st, rid))
                c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                          (config.SEAT_FREE, database.now_str(), r["seat_id"]))
                conn.commit()
                mqtt_service.publish_cmd({"cmd": "release", "seat_id": r["seat_id"]})
        conn.close()
    return jsonify({"ok": True})


# --- [新增] App 专用认证接口 ---

@app.route("/api/login", methods=["POST"])
def api_login_json():
    # 强制解析 JSON 数据
    data = request.get_json(force=True, silent=True) or {}
    u = data.get("username")
    p = data.get("password")

    if not u or not p:
        return jsonify({"ok": False, "error": "用户名或密码为空"}), 400

    conn = database.get_conn()
    user = conn.execute("SELECT * FROM users WHERE username=?", (u,)).fetchone()
    conn.close()

    if not user:
        return jsonify({"ok": False, "error": "用户不存在"}), 400

    # 简单密码比对 (与您原有逻辑保持一致)
    db_pass = user["password"] if user["password"] else "123456"
    if p == db_pass:
        session["user"] = u
        session["role"] = user["role"]
        # 登录成功，Session Cookie 会自动由 Flask 处理
        return jsonify({"ok": True, "username": u, "role": user["role"]})
    else:
        return jsonify({"ok": False, "error": "密码错误"}), 400


@app.route("/api/register", methods=["POST"])
def api_register_json():
    data = request.get_json(force=True, silent=True) or {}
    u = data.get("username")
    p = data.get("password")

    if not u or not p:
        return jsonify({"ok": False, "error": "信息不完整"}), 400

    conn = database.get_conn()
    # 检查重名
    if conn.execute("SELECT id FROM users WHERE username=?", (u,)).fetchone():
        conn.close()
        return jsonify({"ok": False, "error": "用户名已存在"}), 400

    try:
        conn.execute("INSERT INTO users(username, password, role, created_at) VALUES(?,?,?,?)",
                     (u, p, 'user', database.now_str()))
        conn.commit()
        conn.close()
        return jsonify({"ok": True, "msg": "注册成功"})
    except Exception as e:
        conn.close()
        return jsonify({"ok": False, "error": str(e)}), 500


# -----------------------------------------------------------
# 将以下代码覆盖到 server/app.py 的最末尾
# -----------------------------------------------------------

@app.route("/api/seats", methods=["GET"])
def api_seats():
    conn = database.get_conn()
    cursor = conn.cursor()

    try:
        # [关键修正] 使用 seat_id 查询，而不是 id
        cursor.execute("SELECT seat_id, status FROM seats ORDER BY seat_id")
        rows = cursor.fetchall()

        seat_list = []
        for row in rows:
            # 数据库里的 seat_id (例如 "A01")
            # 映射为 App 需要的 "id" 字段
            # 注意：这里我们用 row['seat_id'] 或 row[0] 取值
            s_id = row[0]
            s_status = row[1]
            seat_list.append({
                "id": s_id,
                "status": s_status
            })

        # 如果数据库是空的，说明是第一次运行，自动创建20个座位
        if not seat_list:
            print("⚠️ 数据库为空，正在初始化 20 个默认座位...")
            _init_default_seats()
            # 递归调用自己，重新获取刚才创建的数据
            return api_seats()

        return jsonify({"seats": seat_list})

    except Exception as e:
        print(f"❌ 获取座位失败: {e}")
        # 返回空列表，防止 App 崩溃
        return jsonify({"seats": [], "error": str(e)})
    finally:
        if conn:
            conn.close()


def _init_default_seats():
    """ 自动往数据库里插入 A01 - A20 """
    conn = database.get_conn()
    cursor = conn.cursor()
    try:
        # 生成 A01 到 A20 的座位数据
        default_seats = [(f"A{i:02d}", 0) for i in range(1, 21)]

        # [关键修正] 插入语句也必须用 seat_id
        cursor.executemany("INSERT OR IGNORE INTO seats (seat_id, status) VALUES (?, ?)", default_seats)
        conn.commit()
        print("✅ 已成功初始化 20 个座位 (A01-A20)")
    except Exception as e:
        print(f"❌ 初始化座位失败: {e}")
    finally:
        conn.close()

# --- 管理员接口 ---
@app.route("/api/admin/users", methods=["GET"])
def api_admin_users():
    if session.get("role") != "admin": return jsonify({"error": "Forbbiden"}), 403
    conn = database.get_conn()
    users = [dict(x) for x in conn.execute("SELECT * FROM users ORDER BY id DESC").fetchall()]
    conn.close()
    return jsonify({"users": users})


@app.route("/api/admin/users/add", methods=["POST"])
def api_admin_add_user():
    if session.get("role") != "admin": return jsonify({"error": "Forbbiden"}), 403
    d = request.get_json(force=True)
    try:
        conn = database.get_conn()
        conn.execute("INSERT INTO users(username, uid, role, created_at) VALUES(?, ?, 'user', ?)",
                     (d["username"], d["uid"], database.now_str()))
        conn.commit()
        conn.close()
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route("/api/admin/users/del", methods=["POST"])
def api_admin_del_user():
    if session.get("role") != "admin": return jsonify({"error": "Forbbiden"}), 403
    uid = request.get_json(force=True).get("id")
    conn = database.get_conn()
    conn.execute("DELETE FROM users WHERE id=?", (uid,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


if __name__ == "__main__":
    database.init_db()
    mqtt_service.start_mqtt()
    print("Server running on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)