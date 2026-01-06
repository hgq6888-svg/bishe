# app.py
from flask import Flask, request, jsonify, render_template
from datetime import datetime, timedelta
import config
import database
import mqtt_service

app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")


# app.py 中的 api_state 修改版
@app.route("/api/state")
def api_state():
    try:
        with database.db_lock:
            mqtt_service.cleanup_expired_reservations()
            conn = database.get_conn()
            c = conn.cursor()

            c.execute("SELECT * FROM telemetry ORDER BY id DESC LIMIT 1")
            latest_row = c.fetchone()
            latest = dict(latest_row) if latest_row else None

            c.execute("SELECT * FROM seats ORDER BY seat_id")
            seats = []
            for s in c.fetchall():
                # 转为字典，并使用 .get() 防止缺少 light_on 字段导致报错
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
        print(f"[API Error] /api/state 报错: {e}")
        return jsonify({"ok": False, "error": str(e)}), 500

@app.route("/api/reserve", methods=["POST"])
def api_reserve():
    body = request.get_json(force=True, silent=True) or {}
    seat_id = body.get("seat_id")
    user = body.get("user")
    minutes = int(body.get("minutes", 120))

    with database.db_lock:
        mqtt_service.cleanup_expired_reservations()
        conn = database.get_conn()
        c = conn.cursor()

        c.execute("SELECT * FROM seats WHERE seat_id=?", (seat_id,))
        seat = c.fetchone()
        if not seat or seat["state"] != config.SEAT_FREE:
            conn.close()
            return jsonify({"ok": False, "error": "无法预约"}), 400

        c.execute("SELECT uid FROM users WHERE username=?", (user,))
        u_row = c.fetchone()
        uid = u_row["uid"] if u_row else None

        now_str = database.now_str()
        exp_str = (datetime.now() + timedelta(minutes=minutes)).strftime("%Y-%m-%d %H:%M:%S")

        c.execute("INSERT INTO reservations(seat_id,user,status,uid,reserved_at,expires_at) VALUES(?,?,?,?,?,?)",
                  (seat_id, user, config.RES_ACTIVE, None, now_str, exp_str))
        rid = c.lastrowid
        c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_RESERVED, now_str, seat_id))
        conn.commit()
        conn.close()

        # 发送MQTT指令
        mqtt_service.publish_cmd({
            "cmd": "reserve", "seat_id": seat_id, "reservation_id": rid, 
            "user": user, "uid": uid, "expires_at": exp_str
        })
        return jsonify({"ok": True})

@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    rid = request.get_json(force=True).get("reservation_id")
    with database.db_lock:
        conn = database.get_conn()
        c = conn.cursor()
        c.execute("SELECT * FROM reservations WHERE id=?", (rid,))
        r = c.fetchone()
        if r and r["status"] in (config.RES_ACTIVE, config.RES_IN_USE):
            new_st = config.RES_CANCEL if r["status"] == config.RES_ACTIVE else config.RES_DONE
            c.execute("UPDATE reservations SET status=? WHERE id=?", (new_st, rid))
            c.execute("UPDATE seats SET state=?, updated_at=? WHERE seat_id=?", (config.SEAT_FREE, database.now_str(), r["seat_id"]))
            conn.commit()
            mqtt_service.publish_cmd({"cmd": "release", "seat_id": r["seat_id"], "reservation_id": rid})
        conn.close()
    return jsonify({"ok": True})

# 用户管理 API
@app.route("/api/users", methods=["GET"])
def api_users():
    conn = database.get_conn()
    users = [dict(x) for x in conn.execute("SELECT * FROM users ORDER BY id DESC").fetchall()]
    conn.close()
    return jsonify({"users": users})

@app.route("/api/users/add", methods=["POST"])
def api_user_add():
    b = request.get_json(force=True)
    try:
        conn = database.get_conn()
        conn.execute("INSERT INTO users(username, uid, created_at) VALUES(?,?,?)", (b["username"], b.get("uid"), database.now_str()))
        conn.commit()
        conn.close()
        return jsonify({"ok": True})
    except: return jsonify({"error": "Failed"}), 400

@app.route("/api/users/delete", methods=["POST"])
def api_user_del():
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