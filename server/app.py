from flask import Flask, request, jsonify, render_template, session, redirect, url_for, render_template_string
from datetime import datetime, timedelta
import config
import database
import mqtt_service

app = Flask(__name__)
app.secret_key = "bishe_secret_key_123"

# --- 登录与注册模板 ---
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

            # 环境数据
            tele = c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1").fetchone()
            latest = dict(tele) if tele else None

            # 座位列表
            seats = []
            for s in c.execute("SELECT * FROM seats ORDER BY seat_id").fetchall():
                s_obj = dict(s)
                # 附带当前活跃预约信息
                res = c.execute(
                    "SELECT * FROM reservations WHERE seat_id=? AND status IN (?,?) ORDER BY id DESC LIMIT 1",
                    (s["seat_id"], config.RES_ACTIVE, config.RES_IN_USE)).fetchone()
                s_obj["active_reservation"] = dict(res) if res else None
                seats.append(s_obj)

            conn.close()
            return jsonify({"ok": True, "latest": latest, "seats": seats})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


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
    # 检查卡号是否已被占用
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

        # 1. 检查用户是否已绑卡
        u_row = c.execute("SELECT uid FROM users WHERE username=?", (current_user,)).fetchone()
        user_uid = u_row["uid"] if u_row else None

        if not user_uid or len(user_uid.strip()) == 0:
            conn.close()
            # need_bind 标记让前端跳转
            return jsonify({"ok": False, "error": "请先绑定实体卡号", "need_bind": True}), 400

        # 2. 检查是否已有未完成的预约 (单一预约限制)
        if c.execute("SELECT id FROM reservations WHERE user=? AND status IN (?,?)",
                     (current_user, config.RES_ACTIVE, config.RES_IN_USE)).fetchone():
            conn.close()
            return jsonify({"ok": False, "error": "您当前已有预约，不可重复预约"}), 400

        # 3. 检查座位状态
        seat = c.execute("SELECT state FROM seats WHERE seat_id=?", (seat_id,)).fetchone()
        if not seat or seat["state"] != config.SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "座位已被占用"}), 400

        # 4. 创建预约
        now = database.now_str()
        utc_now = datetime.utcnow()  # 获取 UTC 标准时间
        beijing_now = utc_now + timedelta(hours=8)  # 转换为北京时间
        exp = beijing_now.strftime("%Y-%m-%d %H:%M:%S")
       

        c.execute("INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at) VALUES(?,?,?,?,?,?)",
                  (seat_id, current_user, config.RES_ACTIVE, user_uid, now, exp))
        rid = c.lastrowid
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_RESERVED, now, seat_id))
        conn.commit()
        conn.close()

        # 发送MQTT通知
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

        # 允许本人或管理员取消
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