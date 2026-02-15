package com.example.my_bishe.ui;

import android.app.AlertDialog;
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
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.cardview.widget.CardView;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;
import com.example.my_bishe.LoginActivity;
import com.example.my_bishe.R;
import com.google.android.material.bottomnavigation.BottomNavigationView;
import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class HomeFragment extends Fragment {

    private TextView tvWelcome, tvTemp, tvHumi, tvLux, tvTime;
    private SwipeRefreshLayout swipeRefresh;
    private RecyclerView recyclerView;
    private SeatAdapter adapter;
    private final List<Seat> seatList = new ArrayList<>();
    private final Gson gson = new Gson();

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable refreshRunnable = new Runnable() {
        @Override
        public void run() {
            // 自动刷新时，不显示 loading 动画，以免打扰用户
            fetchData(false);
            handler.postDelayed(this, 3000);
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
        fetchData(true); // 进入页面时主动刷新一次
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
        // 双重保险：如果 LoginActivity 的静态变量为空，从本地读取
        if (baseUrl == null || baseUrl.isEmpty() || baseUrl.equals("http://null:5000")) {
            if (getContext() != null) {
                SharedPreferences sp = getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
                String ip = sp.getString("ip", "192.168.0.104");
                baseUrl = "http://" + ip + ":5000";
            } else {
                if(showLoading) swipeRefresh.setRefreshing(false);
                return;
            }
        }

        Request request = new Request.Builder().url(baseUrl + "/api/state").get().build();

        new Thread(() -> {
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                if (response.isSuccessful() && response.body() != null) {
                    String respStr = response.body().string();
                    JsonObject json = gson.fromJson(respStr, JsonObject.class);

                    if (getActivity() == null) return;
                    getActivity().runOnUiThread(() -> {
                        // 更新环境数据
                        if (json.has("latest") && !json.get("latest").isJsonNull()) {
                            JsonObject lat = json.getAsJsonObject("latest");
                            tvTemp.setText(getStringVal(lat, "temp", "--") + "°C");
                            tvHumi.setText(getStringVal(lat, "humi", "--") + "%");
                            tvLux.setText(getStringVal(lat, "lux", "--") + " Lx");
                        }

                        // 更新时间
                        SimpleDateFormat sdf = new SimpleDateFormat("HH:mm:ss", Locale.getDefault());
                        tvTime.setText(sdf.format(new Date()));

                        // 更新座位列表
                        seatList.clear();
                        if (json.has("seats")) {
                            JsonArray arr = json.getAsJsonArray("seats");
                            for (JsonElement e : arr) {
                                JsonObject obj = e.getAsJsonObject();
                                String id = getStringVal(obj, "seat_id", "??");
                                String rawState = "";
                                if (obj.has("state")) rawState = obj.get("state").getAsString();

                                boolean hasRes = false;
                                int resId = -1;
                                String resUser = "";
                                String resStatus = "";

                                if (obj.has("active_reservation") && !obj.get("active_reservation").isJsonNull()) {
                                    hasRes = true;
                                    JsonObject r = obj.getAsJsonObject("active_reservation");
                                    if(r.has("id")) resId = r.get("id").getAsInt();
                                    resUser = getStringVal(r, "user", "");
                                    resStatus = getStringVal(r, "status", "");
                                }

                                seatList.add(new Seat(id, rawState, hasRes, resId, resUser, resStatus));
                            }
                        }
                        adapter.notifyDataSetChanged();

                        // === [关键] 成功后停止转圈 ===
                        swipeRefresh.setRefreshing(false);
                    });
                } else {
                    // === [关键] 服务器返回错误（如 401, 500）时也要停止转圈 ===
                    if (getActivity() != null) {
                        getActivity().runOnUiThread(() -> {
                            Toast.makeText(getContext(), "获取数据失败: " + response.code(), Toast.LENGTH_SHORT).show();
                            swipeRefresh.setRefreshing(false);
                        });
                    }
                }
            } catch (Exception e) {
                e.printStackTrace();
                // === [关键] 网络异常时也要停止转圈 ===
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        Toast.makeText(getContext(), "网络错误，请检查IP", Toast.LENGTH_SHORT).show();
                        swipeRefresh.setRefreshing(false);
                    });
                }
            }
        }).start();
    }

    private String getStringVal(JsonObject obj, String key, String def) {
        if (obj.has(key) && !obj.get(key).isJsonNull()) return obj.get(key).getAsString();
        return def;
    }

    private void sendCancelRequest(int reservationId) {
        JsonObject json = new JsonObject();
        json.addProperty("reservation_id", reservationId);

        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request req = new Request.Builder().url(LoginActivity.BASE_URL + "/api/cancel").post(body).build();

        new Thread(() -> {
            try (Response resp = LoginActivity.client.newCall(req).execute()) {
                String s = resp.body().string();
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> {
                        if (s.contains("true") || s.contains("ok")) {
                            Toast.makeText(getContext(), "操作成功", Toast.LENGTH_SHORT).show();
                            fetchData(true);
                        } else {
                            Toast.makeText(getContext(), "失败: " + s, Toast.LENGTH_SHORT).show();
                        }
                    });
                }
            } catch (Exception e) {
                if (getActivity() != null) {
                    getActivity().runOnUiThread(() -> Toast.makeText(getContext(), "网络错误", Toast.LENGTH_SHORT).show());
                }
            }
        }).start();
    }

    static class Seat {
        String id;
        String rawState;
        boolean hasReservation;
        int resId;
        String resUser;
        String resStatus;

        public Seat(String id, String rawState, boolean hasReservation, int resId, String resUser, String resStatus) {
            this.id = id; this.rawState = rawState; this.hasReservation = hasReservation;
            this.resId = resId; this.resUser = resUser; this.resStatus = resStatus;
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

            int color;
            String s = seat.rawState.toUpperCase();
            boolean isInUse = s.equals("2") || s.contains("USE") || s.contains("BUSY") || s.contains("OCCUPY");

            if (isInUse) color = Color.parseColor("#EF4444"); // 红色
            else if (seat.hasReservation) color = Color.parseColor("#FFC107"); // 黄色
            else color = Color.parseColor("#10B981"); // 绿色
            holder.cardView.setCardBackgroundColor(color);

            SharedPreferences sp = holder.itemView.getContext().getSharedPreferences("config", Context.MODE_PRIVATE);
            String currentUser = sp.getString("username", "");

            // 1. 自己的座位：弹窗取消/签退
            if (seat.hasReservation && seat.resUser.equals(currentUser)) {
                holder.cardView.setOnClickListener(v -> {
                    String actionText = isInUse ? "强制签退" : "取消预约";
                    String msg = "当前座位: " + seat.id + "\n状态: " + (isInUse ? "使用中" : "已预约") + "\n确定要" + actionText + "吗？";
                    new AlertDialog.Builder(v.getContext())
                            .setTitle("座位管理")
                            .setMessage(msg)
                            .setPositiveButton(actionText, (dialog, which) -> sendCancelRequest(seat.resId))
                            .setNegativeButton("返回", null)
                            .show();
                });
            }
            // 2. 空闲座位：跳转预约
            else if (!isInUse && !seat.hasReservation) {
                holder.cardView.setOnClickListener(v -> {
                    sp.edit().putString("intent_seat_id", seat.id).apply();
                    if (getActivity() != null) {
                        View bottomNav = getActivity().findViewById(R.id.bottom_navigation);
                        if (bottomNav instanceof BottomNavigationView) {
                            ((BottomNavigationView) bottomNav).setSelectedItemId(R.id.nav_reserve);
                        }
                    }
                });
            }
            // 3. 别人的座位
            else {
                holder.cardView.setOnClickListener(null);
            }
        }

        @Override
        public int getItemCount() { return seatList.size(); }

        class Holder extends RecyclerView.ViewHolder {
            TextView tvId;
            CardView cardView;
            public Holder(@NonNull View itemView) {
                super(itemView);
                tvId = itemView.findViewById(R.id.tv_seat_id);
                cardView = itemView.findViewById(R.id.card_seat);
            }
        }
    }
}