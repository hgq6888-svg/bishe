package com.example.my_bishe;

import android.os.Bundle;
import android.text.TextUtils;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import com.google.gson.JsonObject;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class RegisterActivity extends AppCompatActivity {

    private EditText etUser, etPass, etConfirm;
    private Button btnRegister, btnBack;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_register);

        etUser = findViewById(R.id.et_reg_username);
        etPass = findViewById(R.id.et_reg_password);
        etConfirm = findViewById(R.id.et_reg_confirm_password);
        btnRegister = findViewById(R.id.btn_do_register);
        btnBack = findViewById(R.id.btn_back_login);

        btnRegister.setOnClickListener(v -> doRegister());
        btnBack.setOnClickListener(v -> finish()); // 返回登录页
    }

    private void doRegister() {
        String u = etUser.getText().toString().trim();
        String p = etPass.getText().toString().trim();
        String c = etConfirm.getText().toString().trim();

        if (TextUtils.isEmpty(u) || TextUtils.isEmpty(p)) {
            Toast.makeText(this, "用户名和密码不能为空", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!p.equals(c)) {
            Toast.makeText(this, "两次输入的密码不一致", Toast.LENGTH_SHORT).show();
            return;
        }

        // 使用 LoginActivity 中的配置（IP 和 Client）
        String url = LoginActivity.BASE_URL + "/api/register";

        JsonObject json = new JsonObject();
        json.addProperty("username", u);
        json.addProperty("password", p);

        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request req = new Request.Builder().url(url).post(body).build();

        new Thread(() -> {
            try (Response resp = LoginActivity.client.newCall(req).execute()) {
                String s = resp.body().string();
                runOnUiThread(() -> {
                    if (s.contains("true") || s.contains("ok")) {
                        Toast.makeText(this, "注册成功！请登录", Toast.LENGTH_LONG).show();
                        finish(); // 关闭注册页，回到登录页
                    } else {
                        Toast.makeText(this, "注册失败: " + s, Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(this, "网络错误，请检查服务器", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }
}