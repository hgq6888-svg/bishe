package com.example.my_bishe.ui;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.cardview.widget.CardView; // 必须导入这个
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;
import com.example.my_bishe.LoginActivity;
import com.example.my_bishe.R;
import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import okhttp3.Request;
import okhttp3.Response;

public class HomeFragment extends Fragment {

    private TextView tvWelcome, tvTemp, tvHumi, tvLux, tvTime;
    private SwipeRefreshLayout swipeRefresh;
    private RecyclerView recyclerView;
    private SeatAdapter adapter;
    private final List<Seat> seatList = new ArrayList<>();
    private final Gson gson = new Gson();

    // 自动刷新
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable refreshRunnable = new Runnable() {
        @Override
        public void run() {
            fetchData(false);
            handler.postDelayed(this, 3000); // 3秒刷新一次
        }
    };

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_home, container, false);

        tvWelcome = view.findViewById(R.id.tv_welcome_user);
        tvTemp = view.findViewById(R.id.tv_temp);
        tvHumi = view.findViewById(R.id.tv_humi);
        tvLux = view.findViewById(R.id.tv_lux);
        tvTime = view.findViewById(R.id.tv_update_time);
        swipeRefresh = view.findViewById(R.id.swipe_refresh);
        recyclerView = view.findViewById(R.id.recycler_view);

        recyclerView.setLayoutManager(new GridLayoutManager(getContext(), 4));
        adapter = new SeatAdapter();
        recyclerView.setAdapter(adapter);

        swipeRefresh.setOnRefreshListener(() -> fetchData(true));

        return view;
    }

    @Override
    public void onResume() {
        super.onResume();
        updateUserInfo();
        fetchData(true);
        handler.postDelayed(refreshRunnable, 3000);
    }

    @Override
    public void onPause() {
        super.onPause();
        handler.removeCallbacks(refreshRunnable);
    }

    private void updateUserInfo() {
        if (getContext() != null) {
            SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
            String username = sp.getString("username", "访客");
            tvWelcome.setText("用户：" + username);
        }
    }

    private void fetchData(boolean showLoading) {
        if (showLoading) swipeRefresh.setRefreshing(true);

        String baseUrl = LoginActivity.BASE_URL;
        if (baseUrl == null || baseUrl.isEmpty()) {
            if (getContext() != null) {
                SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
                String ip = sp.getString("ip", "192.168.0.104");
                baseUrl = "http://" + ip + ":5000";
            } else return;
        }

        Request request = new Request.Builder().url(baseUrl + "/api/state").get().build();

        new Thread(() -> {
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                if (response.isSuccessful() && response.body() != null) {
                    String respStr = response.body().string();
                    JsonObject json = gson.fromJson(respStr, JsonObject.class);

                    if (getActivity() == null) return;
                    getActivity().runOnUiThread(() -> {
                        if (json.has("latest") && !json.get("latest").isJsonNull()) {
                            JsonObject lat = json.getAsJsonObject("latest");
                            tvTemp.setText(getStringVal(lat, "temp", "--") + "°C");
                            tvHumi.setText(getStringVal(lat, "humi", "--") + "%");
                            tvLux.setText(getStringVal(lat, "lux", "--") + " Lx");
                        }

                        SimpleDateFormat sdf = new SimpleDateFormat("HH:mm:ss", Locale.getDefault());
                        tvTime.setText(sdf.format(new Date()));

                        seatList.clear();
                        if (json.has("seats")) {
                            JsonArray arr = json.getAsJsonArray("seats");
                            for (JsonElement e : arr) {
                                JsonObject obj = e.getAsJsonObject();
                                String id = getStringVal(obj, "seat_id", "??");
                                String rawState = "";
                                if (obj.has("state")) rawState = obj.get("state").getAsString();

                                boolean hasRes = false;
                                if (obj.has("active_reservation") && !obj.get("active_reservation").isJsonNull()) {
                                    hasRes = true;
                                }

                                seatList.add(new Seat(id, rawState, hasRes));
                            }
                        }
                        adapter.notifyDataSetChanged();
                        swipeRefresh.setRefreshing(false);
                    });
                }
            } catch (Exception e) {
                e.printStackTrace();
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> swipeRefresh.setRefreshing(false));
                }
            }
        }).start();
    }

    private String getStringVal(JsonObject obj, String key, String def) {
        if (obj.has(key) && !obj.get(key).isJsonNull()) return obj.get(key).getAsString();
        return def;
    }

    static class Seat {
        String id;
        String rawState;
        boolean hasReservation;
        public Seat(String id, String rawState, boolean hasReservation) {
            this.id = id; this.rawState = rawState; this.hasReservation = hasReservation;
        }
    }

    class SeatAdapter extends RecyclerView.Adapter<SeatAdapter.Holder> {
        @NonNull @Override
        public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_seat, parent, false);
            return new Holder(v);
        }

        @Override
        public void onBindViewHolder(@NonNull Holder holder, int position) {
            Seat seat = seatList.get(position);
            holder.tvId.setText(seat.id);

            // === 颜色优先级逻辑 (关键修改) ===
            int color;
            String s = seat.rawState.toUpperCase();

            // 1. 优先判断是否被占用 (IN_USE / 2) -> 红色
            if (s.equals("2") || s.contains("USE") || s.contains("BUSY") || s.contains("OCCUPY")) {
                color = Color.parseColor("#EF4444"); // 红色
            }
            // 2. 其次判断是否有预约 (RESERVED) -> 黄色
            // (注意：签到后虽然还有 active_reservation，但上面第一步会先捕获 IN_USE 状态)
            else if (seat.hasReservation) {
                color = Color.parseColor("#FFC107"); // 黄色
            }
            // 3. 最后是空闲 -> 绿色
            else {
                color = Color.parseColor("#10B981"); // 绿色
            }

            // 设置颜色
            holder.cardView.setCardBackgroundColor(color);
        }

        @Override
        public int getItemCount() { return seatList.size(); }

        class Holder extends RecyclerView.ViewHolder {
            TextView tvId;
            CardView cardView; // 使用 CardView
            public Holder(@NonNull View itemView) {
                super(itemView);
                tvId = itemView.findViewById(R.id.tv_seat_id);
                // 修复点：这里改成 R.id.card_seat
                cardView = itemView.findViewById(R.id.card_seat);
            }
        }
    }
}