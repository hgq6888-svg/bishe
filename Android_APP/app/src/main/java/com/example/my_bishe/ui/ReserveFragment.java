package com.example.my_bishe.ui;

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

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle s) {
        View v = inflater.inflate(R.layout.fragment_reserve, container, false);
        spinner = v.findViewById(R.id.spinner_seats);
        rgDuration = v.findViewById(R.id.rg_duration);
        btnSubmit = v.findViewById(R.id.btn_submit);

        adapter = new ArrayAdapter<>(getContext(), android.R.layout.simple_spinner_dropdown_item, freeSeats);
        spinner.setAdapter(adapter);

        // 每次进入页面时加载最新数据
        loadFreeSeats();
        btnSubmit.setOnClickListener(view -> doReserve());
        return v;
    }

    private void loadFreeSeats() {
        new Thread(() -> {
            Request request = new Request.Builder().url(LoginActivity.BASE_URL + "/api/state").get().build();
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                JsonObject json = new Gson().fromJson(response.body().string(), JsonObject.class);
                freeSeats.clear();
                if (json.has("seats")) {
                    for (JsonElement e : json.getAsJsonArray("seats")) {
                        JsonObject s = e.getAsJsonObject();
                        String state = "";
                        if (s.has("state")) state = s.get("state").getAsString().toUpperCase();

                        if (state.equals("FREE") || state.equals("0")) {
                            freeSeats.add(s.get("seat_id").getAsString());
                        }
                    }
                }
                if(getActivity()!=null) getActivity().runOnUiThread(() -> {
                    adapter.notifyDataSetChanged();
                    if(freeSeats.isEmpty()) {
                        Toast.makeText(getContext(), "暂无空闲座位", Toast.LENGTH_SHORT).show();
                    } else {
                        // === 新增：自动选中从首页跳转过来的座位 ===
                        SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
                        String targetId = sp.getString("intent_seat_id", "");
                        if (!targetId.isEmpty()) {
                            int index = freeSeats.indexOf(targetId);
                            if (index >= 0) {
                                spinner.setSelection(index);
                                Toast.makeText(getContext(), "已选择座位: " + targetId, Toast.LENGTH_SHORT).show();
                            } else {
                                Toast.makeText(getContext(), "该座位已被占用或不可用", Toast.LENGTH_SHORT).show();
                            }
                            // 清除标记，防止下次手动进入时误触发
                            sp.edit().remove("intent_seat_id").apply();
                        }
                    }
                });
            } catch (Exception e) { e.printStackTrace(); }
        }).start();
    }

    private void doReserve() {
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
                    if (s.contains("true") || s.contains("ok")) {
                        Toast.makeText(getContext(), "预约成功！", Toast.LENGTH_SHORT).show();
                        // 预约成功后跳回首页
                        View bottomNav = getActivity().findViewById(R.id.bottom_navigation);
                        if (bottomNav != null) {
                            bottomNav.post(() -> ((com.google.android.material.bottomnavigation.BottomNavigationView) bottomNav).setSelectedItemId(R.id.nav_home));
                        }
                    } else {
                        Toast.makeText(getContext(), "失败: " + s, Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (Exception e) {
                if(getActivity()!=null) getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "网络错误", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }
}