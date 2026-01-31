package com.example.my_bishe;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import okhttp3.Cookie;
import okhttp3.CookieJar;
import okhttp3.HttpUrl;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public class LoginActivity extends AppCompatActivity {

    private EditText etIp, etUser, etPwd;

    // 全局静态 Client，用于在 Activity 之间共享 Cookie (Session)
    public static OkHttpClient client;
    public static String BASE_URL = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_login);

        etIp = findViewById(R.id.et_ip);
        etUser = findViewById(R.id.et_username);
        etPwd = findViewById(R.id.et_password);
        Button btnLogin = findViewById(R.id.btn_login);

        // 读取上次保存的 IP
        SharedPreferences sp = getSharedPreferences("config", MODE_PRIVATE);
        etIp.setText(sp.getString("ip", "192.168.0.104"));

        // 初始化带 Cookie 管理的 Client
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
                }).build();

        btnLogin.setOnClickListener(v -> {
            String ip = etIp.getText().toString().trim();
            String user = etUser.getText().toString().trim();
            String pwd = etPwd.getText().toString().trim();

            if(ip.isEmpty()) return;

            // 保存 IP
            sp.edit().putString("ip", ip).apply();
            BASE_URL = "http://" + ip + ":5000";

            doLogin(user, pwd);
        });
    }

    private void doLogin(String user, String pwd) {
        okhttp3.FormBody body = new okhttp3.FormBody.Builder()
                .add("username", user).add("password", pwd).build();
        Request request = new Request.Builder().url(BASE_URL + "/login").post(body).build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                if (response.isSuccessful()) {
                    runOnUiThread(() -> {
                        Toast.makeText(this, "登录成功", Toast.LENGTH_SHORT).show();
                        startActivity(new Intent(this, MainActivity.class));
                        finish(); // 关闭登录页
                    });
                } else {
                    runOnUiThread(() -> Toast.makeText(this, "登录失败，请检查账号密码", Toast.LENGTH_SHORT).show());
                }
            } catch (IOException e) {
                runOnUiThread(() -> Toast.makeText(this, "连接超时，请检查IP和防火墙", Toast.LENGTH_LONG).show());
            }
        }).start();
    }
}