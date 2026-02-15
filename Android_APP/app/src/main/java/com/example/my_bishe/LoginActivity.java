package com.example.my_bishe;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.text.TextUtils;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import com.google.gson.Gson;
import com.google.gson.JsonObject;
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

public class LoginActivity extends AppCompatActivity {

    private EditText etUser, etPass;
    private Button btnLogin, btnReg; // 已移除 btnIp

    // Cookie 管理 (保持 Session)
    private static final HashMap<String, List<Cookie>> cookieStore = new HashMap<>();

    public static final OkHttpClient client = new OkHttpClient.Builder()
            .cookieJar(new CookieJar() {
                @Override
                public void saveFromResponse(HttpUrl url, List<Cookie> cookies) {
                    if (cookies != null && !cookies.isEmpty()) {
                        cookieStore.put(url.host(), cookies);
                    }
                }
                @Override
                public List<Cookie> loadForRequest(HttpUrl url) {
                    List<Cookie> cookies = cookieStore.get(url.host());
                    return cookies != null ? cookies : new ArrayList<>();
                }
            })
            .connectTimeout(10, TimeUnit.SECONDS)
            .writeTimeout(10, TimeUnit.SECONDS)
            .readTimeout(10, TimeUnit.SECONDS)
            .build();

    // === 默认服务器 IP 配置 ===
    // 如果需要改 IP，直接修改这里的字符串即可
    public static String BASE_URL = "http://192.168.0.104:5000";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_login);

        etUser = findViewById(R.id.et_username);
        etPass = findViewById(R.id.et_password);
        btnLogin = findViewById(R.id.btn_login);
        btnReg = findViewById(R.id.btn_register);

        // 读取上次保存的用户名
        SharedPreferences sp = getSharedPreferences("config", Context.MODE_PRIVATE);
        etUser.setText(sp.getString("username", ""));

        // 尝试读取可能存在的旧 IP 配置，优先使用保存的配置，如果没有则用默认的
        String savedIp = sp.getString("ip", "192.168.0.104");
        BASE_URL = "http://" + savedIp + ":5000";

        // 登录逻辑
        btnLogin.setOnClickListener(v -> doLogin());

        // 注册跳转
        if (btnReg != null) {
            btnReg.setOnClickListener(v -> {
                Intent intent = new Intent(LoginActivity.this, RegisterActivity.class);
                startActivity(intent);
            });
        }
    }

    private void doLogin() {
        String u = etUser.getText().toString().trim();
        String p = etPass.getText().toString().trim();
        if (TextUtils.isEmpty(u) || TextUtils.isEmpty(p)) {
            Toast.makeText(this, "请输入用户名和密码", Toast.LENGTH_SHORT).show();
            return;
        }

        JsonObject json = new JsonObject();
        json.addProperty("username", u);
        json.addProperty("password", p);

        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request req = new Request.Builder().url(BASE_URL + "/api/login").post(body).build();

        new Thread(() -> {
            try (Response resp = client.newCall(req).execute()) {
                String s = resp.body().string();
                runOnUiThread(() -> {
                    try {
                        JsonObject res = new Gson().fromJson(s, JsonObject.class);
                        if (res.has("ok") && res.get("ok").getAsBoolean()) {
                            Toast.makeText(this, "登录成功", Toast.LENGTH_SHORT).show();

                            String role = "user";
                            if (res.has("role")) role = res.get("role").getAsString();

                            SharedPreferences sp = getSharedPreferences("config", Context.MODE_PRIVATE);
                            sp.edit().putString("username", u).putString("role", role).apply();

                            startActivity(new Intent(LoginActivity.this, MainActivity.class));
                            finish();
                        } else {
                            String err = res.has("error") ? res.get("error").getAsString() : "登录失败";
                            Toast.makeText(this, err, Toast.LENGTH_SHORT).show();
                        }
                    } catch (Exception e) {
                        Toast.makeText(this, "服务器响应异常", Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(this, "网络错误: " + e.getMessage(), Toast.LENGTH_SHORT).show());
            }
        }).start();
    }
}