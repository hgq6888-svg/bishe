// 路径: Android_APP/app/src/main/java/com/example/my_bishe/MainActivity.java
package com.example.my_bishe;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;

import com.google.gson.Gson;

import com.google.gson.JsonObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import okhttp3.Cookie;
import okhttp3.CookieJar;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {

    // 修改为你的电脑局域网IP (如果是模拟器用 10.0.2.2)
    private static final String BASE_URL = "http://192.168.0.110:5000";
    private OkHttpClient client;
    private TextView tvStatus, tvLog;
    private Gson gson = new Gson();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvStatus = findViewById(R.id.tv_status);
        tvLog = findViewById(R.id.tv_log);
        Button btnReserve = findViewById(R.id.btn_reserve);

        // 初始化支持 Cookie 的 OkHttp 客户端，以维持 Flask Session
        client = new OkHttpClient.Builder()
                .cookieJar(new CookieJar() {
                    private final HashMap<String, List<Cookie>> cookieStore = new HashMap<>();
                    @Override
                    public void saveFromResponse(HttpUrl url, List<Cookie> cookies) {
                        cookieStore.put(url.host(), cookies);
                    }
                    @Override
                    public List<Cookie> loadForRequest(HttpUrl url) {
                        List<Cookie> cookies = cookieStore.get(url.host());
                        return cookies != null ? cookies : new ArrayList<>();
                    }
                })
                .build();

        // 1. 先进行登录 (对接 Flask /login 路由)
        login("admin", "123456"); // 这里替换为你的账号

        btnReserve.setOnClickListener(v -> {
            // 2. 预约 A18 座位 (对接 Flask /api/reserve 路由)
            reserveSeat(18); // 这里的 seat_id 需要对应数据库
        });
    }

    private void login(String username, String password) {
        okhttp3.FormBody formBody = new okhttp3.FormBody.Builder()
                .add("username", username)
                .add("password", password)
                .build();

        Request request = new Request.Builder()
                .url(BASE_URL + "/login")
                .post(formBody)
                .build();

        sendRequest(request, "登录成功", "登录失败");
    }

    private void reserveSeat(int seatId) {
        JsonObject json = new JsonObject();
        json.addProperty("seat_id", seatId);
        json.addProperty("minutes", 120);

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json; charset=utf-8"));

        Request request = new Request.Builder()
                .url(BASE_URL + "/api/reserve")
                .post(body)
                .build();

        sendRequest(request, "预约成功", "预约请求失败");
    }

    private void sendRequest(Request request, String successMsg, String failMsg) {
        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                String result = response.body().string();
                runOnUiThread(() -> {
                    tvLog.append("\n" + successMsg + ": " + result);
                    // 登录后刷新一次状态
                    if (successMsg.contains("登录")) refreshState();
                });
            } catch (IOException e) {
                runOnUiThread(() -> tvLog.append("\n" + failMsg + ": " + e.getMessage()));
            }
        }).start();
    }

    private void refreshState() {
        Request request = new Request.Builder()
                .url(BASE_URL + "/api/state")
                .get()
                .build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                String result = response.body().string();
                JsonObject jsonObject = gson.fromJson(result, JsonObject.class);
                if (jsonObject.has("latest")) {
                    JsonObject latest = jsonObject.getAsJsonObject("latest");
                    String info = String.format("温度: %s°C, 湿度: %s%%",
                            latest.get("temp").getAsString(),
                            latest.get("humi").getAsString());
                    runOnUiThread(() -> tvStatus.setText(info));
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }).start();
    }
}