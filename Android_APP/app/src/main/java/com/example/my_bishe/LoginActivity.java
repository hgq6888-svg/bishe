package com.example.my_bishe;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AlertDialog;
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

public class LoginActivity extends AppCompatActivity {

    private EditText etIp, etUser, etPwd;
    public static OkHttpClient client;
    public static String BASE_URL = "";
    private SharedPreferences sp;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_login);

        etIp = findViewById(R.id.et_ip);
        etUser = findViewById(R.id.et_username);
        etPwd = findViewById(R.id.et_password);
        Button btnLogin = findViewById(R.id.btn_login);

        // 动态添加一个注册按钮（因为原布局里没有）
        Button btnRegister = new Button(this);
        btnRegister.setText("没有账号？点此注册");
        btnRegister.setBackground(null);
        btnRegister.setTextColor(getResources().getColor(R.color.primary, null));
        // 将注册按钮添加到布局底部 (假设父布局是 LinearLayout)
        ((android.widget.LinearLayout)findViewById(R.id.btn_login).getParent()).addView(btnRegister);

        sp = getSharedPreferences("config", MODE_PRIVATE);
        etIp.setText(sp.getString("ip", "192.168.0.104"));
        etUser.setText(sp.getString("username", ""));

        // CookieJar 保持 Session
        client = new OkHttpClient.Builder().cookieJar(new CookieJar() {
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

        btnLogin.setOnClickListener(v -> handleAuth("/api/login"));
        btnRegister.setOnClickListener(v -> showRegisterDialog());
    }

    private void handleAuth(String apiPath) {
        String ip = etIp.getText().toString().trim();
        String user = etUser.getText().toString().trim();
        String pwd = etPwd.getText().toString().trim();

        if(ip.isEmpty() || user.isEmpty() || pwd.isEmpty()) {
            Toast.makeText(this, "请填写完整信息", Toast.LENGTH_SHORT).show();
            return;
        }

        sp.edit().putString("ip", ip).apply();
        BASE_URL = "http://" + ip + ":5000";

        JsonObject json = new JsonObject();
        json.addProperty("username", user);
        json.addProperty("password", pwd);
        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request request = new Request.Builder().url(BASE_URL + apiPath).post(body).build();

        new Thread(() -> {
            try (Response response = client.newCall(request).execute()) {
                String resStr = response.body().string();
                runOnUiThread(() -> {
                    if (resStr.contains("\"ok\": true") || resStr.contains("\"ok\":true")) {
                        if (apiPath.contains("login")) {
                            // 登录成功
                            sp.edit().putString("username", user).apply(); // 保存用户名供主页使用
                            Toast.makeText(this, "登录成功", Toast.LENGTH_SHORT).show();
                            startActivity(new Intent(this, MainActivity.class));
                            finish();
                        }
                    } else {
                        Toast.makeText(this, "操作失败: " + resStr, Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (IOException e) {
                runOnUiThread(() -> Toast.makeText(this, "连接超时，请检查IP", Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    private void showRegisterDialog() {
        // 创建一个简单的注册弹窗
        android.widget.LinearLayout layout = new android.widget.LinearLayout(this);
        layout.setOrientation(android.widget.LinearLayout.VERTICAL);
        layout.setPadding(50, 20, 50, 20);

        final EditText regUser = new EditText(this); regUser.setHint("设置用户名");
        final EditText regPwd = new EditText(this); regPwd.setHint("设置密码");
        layout.addView(regUser); layout.addView(regPwd);

        new AlertDialog.Builder(this)
                .setTitle("注册新账号")
                .setView(layout)
                .setPositiveButton("注册", (d, w) -> {
                    // 借用 handleAuth 逻辑，这里临时设置一下 ET 的值来复用逻辑，或者单独写
                    // 为了简单，直接调用注册接口
                    String ip = etIp.getText().toString().trim();
                    if(ip.isEmpty()) return;
                    BASE_URL = "http://" + ip + ":5000";

                    JsonObject json = new JsonObject();
                    json.addProperty("username", regUser.getText().toString().trim());
                    json.addProperty("password", regPwd.getText().toString().trim());

                    new Thread(() -> {
                        try {
                            RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
                            Request req = new Request.Builder().url(BASE_URL + "/api/register").post(body).build();
                            Response resp = client.newCall(req).execute();
                            String s = resp.body().string();
                            runOnUiThread(() -> {
                                if(s.contains("true")) Toast.makeText(this, "注册成功，请登录", Toast.LENGTH_SHORT).show();
                                else Toast.makeText(this, "注册失败: " + s, Toast.LENGTH_SHORT).show();
                            });
                        } catch(Exception e) { e.printStackTrace(); }
                    }).start();
                })
                .setNegativeButton("取消", null)
                .show();
    }
}