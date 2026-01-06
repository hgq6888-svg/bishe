# app.py
from flask import Flask, request, jsonify, render_template, session, redirect, url_for, render_template_string
from datetime import datetime, timedelta
import config
import database
import mqtt_service

app = Flask(__name__)
app.secret_key = "bishe_secret_key_123"  # 用于Session加密

# === 1. 登录页面模板 ===
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
        .link { display: block; text-align: center; margin-top: 15px; color: #007bff; text-decoration: none; font-size: 0.9em; }
        .link:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="card">
        <h2>座位预约系统</h2>
        {% if error %}
            <div class="error">{{ error }}</div>
        {% elif msg %}
            <div style="color: green; text-align: center; margin-bottom: 10px;">{{ msg }}</div>
        {% endif %}
        <form method="POST" action="/login">
            <input type="text" name="username" placeholder="用户名" required>
            <input type="password" name="password" placeholder="密码" required>
            <button type="submit">登录</button>
        </form>
        <a href="/register" class="link">没有账号？去注册 -></a>
        <div class="tip">默认管理员: admin / 123456</div>
    </div>
</body>
</html>
"""

# === 2. 注册页面模板 ===
REGISTER_HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>Register - Seat System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background: #f0f2f5; }
        .card { background: white; padding: 2rem; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); width: 300px; }
        h2 { text-align: center; margin-bottom: 1.5rem; color: #333; }
        input { width: 100%; padding: 10px; margin-bottom: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background: #28a745; color: white; border: none; border-radius: 4px; cursor: pointer; }
        button:hover { background: #218838; }
        .error { color: red; font-size: 0.9em; margin-bottom: 10px; text-align: center; }
        .link { display: block; text-align: center; margin-top: 15px; color: #007bff; text-decoration: none; font-size: 0.9em; }
        .link:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="card">
        <h2>注册新用户</h2>
        {% if error %}
            <div class="error">{{ error }}</div>
        {% endif %}
        <form method="POST" action="/register">
            <input type="text" name="username" placeholder="设置用户名" required>
            <input type="password" name="password" placeholder="设置密码" required>
            <input type="password" name="confirm_password" placeholder="确认密码" required>
            <button type="submit">立即注册</button>
        </form>
        <a href="/login" class="link"><- 返回登录</a>
    </div>
</body>
</html>
"""


@app.route("/")
def index():
    if not session.get("user"):
        return redirect(url_for("login"))
    role = session.get("role", "user")
    return render_template("index.html", current_user=session["user"], role=role)


@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form.get("username")
        password = request.form.get("password")

        conn = database.get_conn()
        c = conn.cursor()
        c.execute("SELECT * FROM users WHERE username=?", (username,))
        user = c.fetchone()
        conn.close()

        if not user:
            return render_template_string(LOGIN_HTML, error="用户不存在，请先注册")
        else:
            db_pass = user["password"] if user["password"] else "123456"
            if password == db_pass:
                session["user"] = username
                try:
                    session["role"] = user["role"]
                except:
                    session["role"] = "admin" if username == "admin" else "user"

                return redirect(url_for("index"))
            else:
                return render_template_string(LOGIN_HTML, error="密码错误")

    return render_template_string(LOGIN_HTML)


@app.route("/register", methods=["GET", "POST"])
def register():
    if request.method == "POST":
        username = request.form.get("username")
        password = request.form.get("password")
        confirm_pwd = request.form.get("confirm_password")

        if not username or not password:
            return render_template_string(REGISTER_HTML, error="用户名和密码不能为空")

        if password != confirm_pwd:
            return render_template_string(REGISTER_HTML, error="两次输入的密码不一致")

        conn = database.get_conn()
        c = conn.cursor()

        c.execute("SELECT id FROM users WHERE username=?", (username,))
        if c.fetchone():
            conn.close()
            return render_template_string(REGISTER_HTML, error="该用户名已被占用")

        try:
            role = 'user'
            try:
                c.execute("INSERT INTO users(username, password, role, created_at) VALUES(?,?,?,?)",
                          (username, password, role, database.now_str()))
            except Exception:
                c.execute("INSERT INTO users(username, password, created_at) VALUES(?,?,?)",
                          (username, password, database.now_str()))

            conn.commit()
            conn.close()
            return render_template_string(LOGIN_HTML, msg="注册成功，请登录")

        except Exception as e:
            conn.close()
            return render_template_string(REGISTER_HTML, error=f"注册失败: {str(e)}")

    return render_template_string(REGISTER_HTML)


@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))


@app.route("/api/state")
def api_state():
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
    current_user = session.get("user")
    if not current_user:
        return jsonify({"ok": False, "error": "请先登录"}), 401

    body = request.get_json(force=True, silent=True) or {}
    seat_id = body.get("seat_id")
    minutes = int(body.get("minutes", 120))

    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()

        # 【新增】单一预约限制逻辑
        # 检查该用户是否已有 Active 或 In Use 的预约
        c.execute("SELECT id FROM reservations WHERE user=? AND status IN (?,?)",
                  (current_user, config.RES_ACTIVE, config.RES_IN_USE))
        if c.fetchone():
            conn.close()
            return jsonify({"ok": False, "error": "您当前已有未结束的预约，请勿重复预约"}), 400

        # 检查座位状态
        c.execute("SELECT * FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        if not seat or seat["state"] != config.SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "该座位已被占用或不可用"}), 400

        # 获取UID
        c.execute("SELECT uid FROM users WHERE username=?", (current_user,))
        u_row = c.fetchone()
        uid = u_row["uid"] if u_row else None

        now_str = database.now_str()
        exp_str = (datetime.now() + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S")

        c.execute("INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at) VALUES(?,?,?,?,?,?)",
                  (seat_id, current_user, config.RES_ACTIVE, uid, now_str, exp_str))
        rid = c.lastrowid
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_RESERVED, now_str, seat_id))
        conn.commit()
        conn.close()

        mqtt_service.publish_cmd({
            "cmd": "reserve", "seat_id": seat_id, "reservation_id": rid,
            "user": current_user, "uid": uid, "expires_at": exp_str
        })
        return jsonify({"ok": True})


@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    current_user = session.get("user")
    is_admin = (session.get("role") == "admin")

    if not current_user: return jsonify({"error": "Auth required"}), 401

    rid = request.get_json(force=True).get("reservation_id")
    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()
        c.execute("SELECT * FROM reservations WHERE id=?", (rid,))
        r = c.fetchone()

        if r and r["user"] != current_user and not is_admin:
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


# 用户管理接口 (仅管理员可用)
@app.route("/api/users", methods=["GET"])
def api_users():
    if session.get("role") != "admin":
        return jsonify({"error": "Admin required"}), 403
    conn = database.get_conn()
    users = [dict(x) for x in conn.execute("SELECT * FROM users ORDER BY id DESC").fetchall()]
    conn.close()
    return jsonify({"users": users})


@app.route("/api/users/add", methods=["POST"])
def api_user_add():
    if session.get("role") != "admin": return jsonify({"error": "Admin required"}), 403
    body = request.get_json(force=True)
    username = body.get("username")
    uid = body.get("uid")
    if not username: return jsonify({"error": "Missing name"}), 400
    conn = database.get_conn()
    try:
        try:
            conn.execute("INSERT INTO users(username, uid, role, created_at) VALUES(?, ?, 'user', ?)",
                         (username, uid, database.now_str()))
        except:
            conn.execute("INSERT INTO users(username, uid, created_at) VALUES(?, ?, ?)",
                         (username, uid, database.now_str()))
        conn.commit()
        return jsonify({"ok": True})
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    finally:
        conn.close()


@app.route("/api/users/delete", methods=["POST"])
def api_user_del():
    if session.get("role") != "admin": return jsonify({"error": "Admin required"}), 403
    uid_db = request.get_json(force=True).get("id")
    conn = database.get_conn()
    conn.execute("DELETE FROM users WHERE id=?", (uid_db,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})


if __name__ == "__main__":
    database.init_db()
    mqtt_service.start_mqtt()
    print("Server running on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)