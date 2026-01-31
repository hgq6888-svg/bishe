package com.example.my_bishe;

import android.os.Bundle;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.gson.Gson;
import com.google.gson.JsonObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.concurrent.TimeUnit;

import okhttp3.Cookie;
import okhttp3.CookieJar;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {

    // 你的电脑局域网 IP (请确保和 ipconfig 查到的一致)
    private static final String BASE_URL = "http://192.168.0.104:5000";

    private OkHttpClient client;
    private TextView tvStatus, tvLog;
    private Button btnReserve;
    private Gson gson = new Gson();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 绑定界面控件
        tvStatus = findViewById(R.id.tv_status);
        tvLog = findViewById(R.id.tv_log);
        btnReserve = findViewById(R.id.btn_reserve);

        // 初始化网络请求客户端
        client = new OkHttpClient.Builder()
                .connectTimeout(5, TimeUnit.SECONDS) // 连接超时
                .readTimeout(5, TimeUnit.SECONDS)    // 读取超时
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

        logToUi("正在连接系统: " + BASE_URL);

        // 1. 系统启动后自动登录
        login("admin", "123456");

        // 2. 按钮点击事件
        btnReserve.setOnClickListener(v -> {
            btnReserve.setEnabled(false);
            btnReserve.setText("预约请求中...");
            // [修复点] 这里的座位号必须是字符串 "A18"，不能是数字 18
            reserveSeat("A18");
        });
    }

    // 登录功能
    private void login(String username, String password) {
        okhttp3.FormBody formBody = new okhttp3.FormBody.Builder()
                .add("username", username)
                .add("password", password)
                .build();

        Request request = new Request.Builder()
                .url(BASE_URL + "/login")
                .post(formBody)
                .build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                if (response.isSuccessful()) {
                    showToast("管理员登录成功");
                    logToUi("登录成功，正在获取环境数据...");
                    refreshState(); // 登录后立即刷新数据
                } else {
                    logToUi("登录失败: 服务器返回 " + response.code());
                }
            } catch (IOException e) {
                logToUi("连接服务器失败: " + e.getMessage());
                showToast("连接失败，请检查电脑防火墙");
            }
        }).start();
    }

    // 预约功能 [关键修复: 参数改为 String]
    private void reserveSeat(String seatId) {
        JsonObject json = new JsonObject();
        json.addProperty("seat_id", seatId); // 这里发送的是 "A18"
        json.addProperty("minutes", 120);

        RequestBody body = RequestBody.create(
                json.toString(), MediaType.parse("application/json; charset=utf-8"));

        Request request = new Request.Builder()
                .url(BASE_URL + "/api/reserve")
                .post(body)
                .build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                String result = response.body().string();

                // 解析服务器返回结果
                String errorMsg = "";
                boolean isSuccess = false;

                try {
                    JsonObject respJson = gson.fromJson(result, JsonObject.class);
                    if (respJson.has("ok") && respJson.get("ok").getAsBoolean()) {
                        isSuccess = true;
                    } else if (respJson.has("error")) {
                        errorMsg = respJson.get("error").getAsString();
                    }
                } catch (Exception e) {
                    errorMsg = "解析错误: " + result;
                }

                if (isSuccess) {
                    showToast("✅ 预约成功！设备即将响应");
                    logToUi("预约成功: 座位 " + seatId);
                    refreshState();
                } else {
                    String finalError = errorMsg.isEmpty() ? "未知错误" : errorMsg;
                    logToUi("❌ 预约失败: " + finalError);
                    showToast(finalError); // 如果未绑定卡号，这里会直接提示
                }

            } catch (IOException e) {
                logToUi("请求超时: " + e.getMessage());
                showToast("网络请求失败");
            } finally {
                // 无论成功失败，都恢复按钮状态
                runOnUiThread(() -> {
                    btnReserve.setEnabled(true);
                    btnReserve.setText("立即预约 (2小时)");
                });
            }
        }).start();
    }

    // 刷新环境状态 (温湿度)
    private void refreshState() {
        Request request = new Request.Builder()
                .url(BASE_URL + "/api/state")
                .get()
                .build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                String result = response.body().string();
                JsonObject jsonObject = gson.fromJson(result, JsonObject.class);

                if (jsonObject != null && jsonObject.has("latest")) {
                    JsonObject latest = jsonObject.getAsJsonObject("latest");

                    // 防止数据为空时崩溃
                    String temp = latest.has("temp") && !latest.get("temp").isJsonNull()
                            ? latest.get("temp").getAsString() : "--";
                    String humi = latest.has("humi") && !latest.get("humi").isJsonNull()
                            ? latest.get("humi").getAsString() : "--";

                    String info = String.format("温度: %s°C   湿度: %s%%", temp, humi);

                    runOnUiThread(() -> {
                        tvStatus.setText(info);
                        logToUi("数据已更新: " + info);
                    });
                }
            } catch (Exception e) {
                // 获取状态失败不弹窗干扰用户，只记录日志
                e.printStackTrace();
            }
        }).start();
    }

    // 辅助方法：在 UI 线程显示日志
    private void logToUi(String msg) {
        runOnUiThread(() -> tvLog.append("\n" + msg));
    }

    // 辅助方法：弹窗提示
    private void showToast(String msg) {
        runOnUiThread(() -> Toast.makeText(MainActivity.this, msg, Toast.LENGTH_SHORT).show());
    }
}