package com.example.my_bishe.ui;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.RadioGroup;
import android.widget.Spinner;
import android.widget.Toast;
import androidx.fragment.app.Fragment;
import com.example.my_bishe.LoginActivity;
import com.example.my_bishe.R;
import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class ReserveFragment extends Fragment {
    private Spinner spinner;
    private RadioGroup rgDuration;
    private Button btnSubmit;
    private final List<String> freeSeats = new ArrayList<>();
    private ArrayAdapter<String> adapter;
    private final Gson gson = new Gson();

    // 状态标记
    private boolean hasActiveReservation = false;
    private String currentUsername = "";

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle s) {
        View v = inflater.inflate(R.layout.fragment_reserve, container, false);
        spinner = v.findViewById(R.id.spinner_seats);
        rgDuration = v.findViewById(R.id.rg_duration);
        btnSubmit = v.findViewById(R.id.btn_submit);

        // 获取当前用户名
        if (getContext() != null) {
            SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
            currentUsername = sp.getString("username", "");
        }

        adapter = new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_dropdown_item, freeSeats);
        spinner.setAdapter(adapter);

        // 每次进入页面时加载最新数据
        loadFreeSeats();
        btnSubmit.setOnClickListener(view -> doReserve());
        return v;
    }

    private void loadFreeSeats() {
        String url = LoginActivity.BASE_URL + "/api/state";
        Request request = new Request.Builder().url(url).get().build();

        new Thread(() -> {
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                if (response.body() == null) return;
                String respStr = response.body().string();
                JsonObject json = gson.fromJson(respStr, JsonObject.class);

                freeSeats.clear();
                hasActiveReservation = false;

                if (json.has("seats")) {
                    for (JsonElement e : json.getAsJsonArray("seats")) {
                        JsonObject s = e.getAsJsonObject();

                        // 1. 检查是否已被"我"预约
                        if (s.has("active_reservation") && !s.get("active_reservation").isJsonNull()) {
                            JsonObject res = s.getAsJsonObject("active_reservation");
                            String resUser = "";
                            if (res.has("user")) resUser = res.get("user").getAsString();

                            if (resUser.equals(currentUsername)) {
                                hasActiveReservation = true;
                            }
                        }

                        // 2. 筛选空闲座位
                        String state = "";
                        if (s.has("state")) state = s.get("state").getAsString().toUpperCase();
                        if (state.equals("FREE") || state.equals("0")) {
                            freeSeats.add(s.get("seat_id").getAsString());
                        }
                    }
                }

                if(getActivity()!=null) getActivity().runOnUiThread(() -> {
                    adapter.notifyDataSetChanged();

                    // 状态提示逻辑
                    if (hasActiveReservation) {
                        btnSubmit.setText("您当前已有预约");
                        // 雖然已有預約，但為了能讓用戶點擊看到提示，我們可以不禁用按鈕，或者禁用並在點擊時提示
                        // 這裡選擇保持啟用但點擊時攔截，體驗更好
                    } else {
                        btnSubmit.setText("立即预约");
                    }
                    btnSubmit.setEnabled(true);

                    if(freeSeats.isEmpty() && !hasActiveReservation) {
                        // 仅提示，不禁用，以免无数据时界面死板
                    } else {
                        // 自动选中逻辑
                        SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
                        String targetId = sp.getString("intent_seat_id", "");
                        if (!targetId.isEmpty()) {
                            int index = freeSeats.indexOf(targetId);
                            if (index >= 0) {
                                spinner.setSelection(index);
                            }
                            sp.edit().remove("intent_seat_id").apply();
                        }
                    }
                });
            } catch (Exception e) { e.printStackTrace(); }
        }).start();
    }

    private void doReserve() {
        // 1. 前置检查：是否已有预约
        if (hasActiveReservation) {
            new AlertDialog.Builder(getContext())
                    .setTitle("操作受限")
                    .setMessage("您当前已经有一个正在进行的预约。\n请先在首页取消预约或签退后，再申请新座位。")
                    .setPositiveButton("知道了", null)
                    .show();
            return;
        }

        if (spinner.getSelectedItem() == null) {
            Toast.makeText(getContext(), "请先选择座位", Toast.LENGTH_SHORT).show();
            return;
        }

        String seatId = spinner.getSelectedItem().toString();
        int minutes = 120;
        int checkedId = rgDuration.getCheckedRadioButtonId();
        if (checkedId == R.id.rb_30) minutes = 30;
        else if (checkedId == R.id.rb_60) minutes = 60;
        else if (checkedId == R.id.rb_240) minutes = 240;

        JsonObject json = new JsonObject();
        json.addProperty("seat_id", seatId);
        json.addProperty("minutes", minutes);

        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request req = new Request.Builder().url(LoginActivity.BASE_URL + "/api/reserve").post(body).build();

        new Thread(() -> {
            try (Response resp = LoginActivity.client.newCall(req).execute()) {
                String s = resp.body().string();
                if(getActivity()!=null) getActivity().runOnUiThread(() -> {
                    try {
                        // === [核心修复] 严格解析 JSON 结果 ===
                        JsonObject res = gson.fromJson(s, JsonObject.class);
                        boolean isOk = res.has("ok") && res.get("ok").getAsBoolean();
                        String errorMsg = res.has("error") ? res.get("error").getAsString() : "";

                        if (isOk) {
                            // 成功
                            Toast.makeText(getContext(), "预约成功！", Toast.LENGTH_SHORT).show();
                            View bottomNav = getActivity().findViewById(R.id.bottom_navigation);
                            if (bottomNav != null) {
                                ((BottomNavigationView) bottomNav).setSelectedItemId(R.id.nav_home);
                            }
                        } else {
                            // 失败
                            if (errorMsg.contains("绑定") || errorMsg.contains("bind")) {
                                showBindCardDialog();
                            } else if (errorMsg.contains("已有") || errorMsg.contains("exists")) {
                                Toast.makeText(getContext(), "预约失败：您已有其他预约", Toast.LENGTH_LONG).show();
                                loadFreeSeats(); // 刷新状态
                            } else {
                                Toast.makeText(getContext(), "预约失败: " + errorMsg, Toast.LENGTH_SHORT).show();
                            }
                        }
                    } catch (Exception e) {
                        Toast.makeText(getContext(), "服务器响应异常: " + s, Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (Exception e) {
                if(getActivity()!=null) getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "网络错误", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    private void showBindCardDialog() {
        new AlertDialog.Builder(getContext())
                .setTitle("需要绑定实体卡")
                .setMessage("预约座位需要先绑定您的实体卡号。\n是否立即前往个人中心进行绑定？")
                .setPositiveButton("去绑定", (dialog, which) -> {
                    View bottomNav = getActivity().findViewById(R.id.bottom_navigation);
                    if (bottomNav != null) {
                        ((BottomNavigationView) bottomNav).setSelectedItemId(R.id.nav_profile);
                    }
                })
                .setNegativeButton("稍后再说", null)
                .show();
    }
}