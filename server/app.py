# app.py
from flask import Flask, request, jsonify, render_template, session, redirect, url_for, render_template_string
from datetime import datetime, timedelta
import config
import database
import mqtt_service

app = Flask(__name__)
app.secret_key = "bishe_secret_key_123"  # 【新增】用于Session加密

# 简单的内嵌登录页面模板
LOGIN_HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>Login - Seat System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background: #f0f2f5; }
        .card { background: white; padding: 2rem; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); width: 300px; }
        h2 { text-align: center; margin-bottom: 1.5rem; color: #333; }
        input { width: 100%; padding: 10px; margin-bottom: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; }
        button:hover { background: #0056b3; }
        .error { color: red; font-size: 0.9em; margin-bottom: 10px; text-align: center; }
        .tip { font-size: 0.8em; color: #666; margin-top: 10px; text-align: center; }
    </style>
</head>
<body>
    <div class="card">
        <h2>座位预约系统</h2>
        {% if error %}
            <div class="error">{{ error }}</div>
        {% endif %}
        <form method="POST" action="/login">
            <input type="text" name="username" placeholder="用户名" required>
            <input type="password" name="password" placeholder="密码" required>
            <button type="submit">登录</button>
        </form>
        <div class="tip">默认账号: admin / 密码: 123456</div>
    </div>
</body>
</html>
"""


@app.route("/")
def index():
    # 【权限控制】未登录跳转到登录页
    if not session.get("user"):
        return redirect(url_for("login"))
    # 将当前用户名传给前端显示
    return render_template("index.html", current_user=session["user"])


@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form.get("username")
        password = request.form.get("password")

        conn = database.get_conn()
        c = conn.cursor()
        # 查询用户
        c.execute("SELECT * FROM users WHERE username=?", (username,))
        user = c.fetchone()

        # 简单验证: 如果用户不存在，自动注册(方便演示); 如果存在则校验密码
        if not user:
            # 自动注册新用户
            c.execute("INSERT INTO users(username, password, created_at) VALUES(?,?,?)",
                      (username, password, database.now_str()))
            conn.commit()
            session["user"] = username
            conn.close()
            return redirect(url_for("index"))
        else:
            # 校验密码 (这里是明文对比，实际项目建议用hash)
            db_pass = user["password"] if user["password"] else "123456"
            if password == db_pass:
                session["user"] = username
                conn.close()
                return redirect(url_for("index"))
            else:
                conn.close()
                return render_template_string(LOGIN_HTML, error="密码错误")

    return render_template_string(LOGIN_HTML)


@app.route("/logout")
def logout():
    session.pop("user", None)
    return redirect(url_for("login"))


@app.route("/api/state")
def api_state():
    # 允许未登录获取状态，或者你也想拦截？通常状态看板可以公开，但为了统一：
    if not session.get("user"):
        return jsonify({"ok": False, "error": "Unauthorized"}), 401

    try:
        with database.db_lock:
            conn = database.get_conn()
            c = conn.cursor()

            c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1")
            latest_row = c.fetchone()
            latest = dict(latest_row) if latest_row else None

            c.execute("SELECT * FROM seats ORDER BY seat_id")
            seats = []
            for s in c.fetchall():
                s_obj = dict(s)
                s_obj["light_on"] = s_obj.get("light_on", 0)
                s_obj["light_mode"] = s_obj.get("light_mode", "MANUAL")
                c.execute("SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
                          (s["seat_id"], config.RES_ACTIVE, config.RES_IN_USE))
                res = c.fetchone()
                s_obj["active_reservation"] = dict(res) if res else None
                seats.append(s_obj)

            conn.close()
            return jsonify({"ok": True, "latest": latest, "seats": seats})
    except Exception as e:
        print(f"[API Error] {e}")
        return jsonify({"ok": False, "error": str(e)}), 500


@app.route("/api/reserve", methods=["POST"])
def api_reserve():
    # 【权限控制】只有登录用户才能预约
    current_user = session.get("user")
    if not current_user:
        return jsonify({"ok": False, "error": "请先登录"}), 401

    body = request.get_json(force=True, silent=True) or {}
    seat_id = body.get("seat_id")
    # 强制使用当前登录用户，防止伪造他人预约
    user = current_user
    minutes = int(body.get("minutes", 120))

    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()

        c.execute("SELECT * FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        if not seat or seat["state"] != config.SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "无法预约"}), 400

        # 获取 UID 用于后续刷卡校验
        c.execute("SELECT uid FROM users WHERE username=?", (user,))
        u_row = c.fetchone()
        uid = u_row["uid"] if u_row else None

        now_str = database.now_str()
        exp_str = (datetime.now() + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S")

        c.execute("INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at) VALUES(?,?,?,?,?,?)",
                  (seat_id, user, config.RES_ACTIVE, uid, now_str, exp_str))
        rid = c.lastrowid
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_RESERVED, now_str, seat_id))
        conn.commit()
        conn.close()

        mqtt_service.publish_cmd({
            "cmd": "reserve", "seat_id": seat_id, "reservation_id": rid,
            "user": user, "uid": uid, "expires_at": exp_str
        })
        return jsonify({"ok": True})


@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    if not session.get("user"): return jsonify({"error": "Auth required"}), 401

    rid = request.get_json(force=True).get("reservation_id")
    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()
        c.execute("SELECT * FROM reservations WHERE id=?", (rid,))
        r = c.fetchone()

        # 只能取消自己的预约 (或者你是管理员)
        if r and r["user"] != session["user"]:
            conn.close()
            return jsonify({"ok": False, "error": "只能取消自己的预约"}), 403

        if r and r["status"] in (config.RES_ACTIVE, config.RES_IN_USE):
            new_st = config.RES_CANCEL if r["status"] == config.RES_ACTIVE else config.RES_DONE
            c.execute("UPDATE reservations SET status=? WHERE id=?", (new_st, rid))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?",
                      (config.SEAT_FREE, database.now_str(), r["seat_id"]))
            conn.commit()
            mqtt_service.publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reservation_id": rid})
        conn.close()
    return jsonify({"ok": True})


# 简单的用户管理接口 (保留)
@app.route("/api/users", methods=["GET"])
def api_users():
    conn = database.get_conn()
    users = [dict(x) for x in conn.execute("SELECT * FROM users ORDER BY id DESC").fetchall()]
    conn.close()
    return jsonify({"users": users})


if __name__ == "__main__":
    database.init_db()
    mqtt_service.start_mqtt()
    print("Server running on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)