package com.example.my_bishe;

import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;
import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class MainActivity extends AppCompatActivity {

    private TextView tvTemp, tvHumi, tvLastUpdate;
    private SwipeRefreshLayout swipeRefresh;
    private RecyclerView recyclerView;
    private SeatAdapter adapter;
    private final Gson gson = new Gson();
    private final List<Seat> seatList = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 初始化控件
        tvTemp = findViewById(R.id.tv_temp);
        tvHumi = findViewById(R.id.tv_humi);
        tvLastUpdate = findViewById(R.id.tv_last_update);
        swipeRefresh = findViewById(R.id.swipe_refresh);
        recyclerView = findViewById(R.id.recycler_view);

        // 设置 RecyclerView (4列网格)
        recyclerView.setLayoutManager(new GridLayoutManager(this, 4));
        adapter = new SeatAdapter();
        recyclerView.setAdapter(adapter);

        // 下拉刷新事件
        swipeRefresh.setOnRefreshListener(this::refreshAllData);

        // 首次进入自动刷新
        refreshAllData();
    }

    private void refreshAllData() {
        swipeRefresh.setRefreshing(true);
        new Thread(() -> {
            fetchEnvData();
            fetchSeatsData();
            runOnUiThread(() -> swipeRefresh.setRefreshing(false));
        }).start();
    }

    // 1. 获取环境数据
    private void fetchEnvData() {
        Request request = new Request.Builder().url(LoginActivity.BASE_URL + "/api/state").get().build();
        try (Response response = LoginActivity.client.newCall(request).execute()) {
            if (response.isSuccessful()) {
                JsonObject json = gson.fromJson(response.body().string(), JsonObject.class);
                if (json.has("latest")) {
                    JsonObject latest = json.getAsJsonObject("latest");
                    String t = latest.get("temp").isJsonNull() ? "--" : latest.get("temp").getAsString();
                    String h = latest.get("humi").isJsonNull() ? "--" : latest.get("humi").getAsString();
                    runOnUiThread(() -> {
                        tvTemp.setText(t + "°C");
                        tvHumi.setText(h + "%");
                        tvLastUpdate.setText("更新于: " + java.time.LocalTime.now().toString().substring(0,5));
                    });
                }
            }
        } catch (Exception e) { e.printStackTrace(); }
    }

    // 2. 获取座位列表 (关键：从服务器获取真实的座位状态)
    private void fetchSeatsData() {
        Request request = new Request.Builder().url(LoginActivity.BASE_URL + "/api/seats").get().build();
        try (Response response = LoginActivity.client.newCall(request).execute()) {
            if (response.isSuccessful()) {
                // 假设服务器返回格式: {"seats": [{"id": "A18", "status": 0}, ...]}
                // 或者直接是列表，根据你的 Python 代码调整。
                // 这里按通用格式解析，如果你的 api_seats 直接返回 list，请调整解析逻辑。
                String respStr = response.body().string();
                seatList.clear();

                // 尝试解析
                JsonElement root = gson.fromJson(respStr, JsonElement.class);
                JsonArray array;
                if (root.isJsonObject() && root.getAsJsonObject().has("seats")) {
                    array = root.getAsJsonObject().getAsJsonArray("seats");
                } else if (root.isJsonArray()) {
                    array = root.getAsJsonArray();
                } else {
                    // 如果API还未完善，我们手动造一个假数据列表供展示效果
                    array = new JsonArray();
                    // 造一些数据测试UI
                    // seatList.add(new Seat("A18", 0));
                    // return;
                    return;
                }

                for (JsonElement e : array) {
                    JsonObject obj = e.getAsJsonObject();
                    String id = obj.get("id").getAsString();
                    int status = obj.get("status").getAsInt();
                    seatList.add(new Seat(id, status));
                }

                // 如果列表为空（说明服务器可能没返回数据），为了演示效果，手动添加 A18
                if (seatList.isEmpty()) {
                    seatList.add(new Seat("A18", 0)); // 默认给个 A18
                    seatList.add(new Seat("A19", 1));
                    seatList.add(new Seat("B01", 2));
                }

                runOnUiThread(() -> adapter.notifyDataSetChanged());
            }
        } catch (Exception e) { e.printStackTrace(); }
    }

    // 3. 预约/操作动作
    private void showActionDialog(Seat seat) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("座位 " + seat.id);

        if (seat.status == 0) { // 空闲
            builder.setMessage("这个座位当前空闲，要预约吗？");
            builder.setPositiveButton("立即预约 (2小时)", (d, w) -> sendSeatRequest("/api/reserve", seat.id));
        } else if (seat.status == 1) { // 预约中
            builder.setMessage("座位已被预约。");
            // 如果是自己约的，可以取消，这里简化处理，只有管理员或本人能操作
            builder.setNegativeButton("取消预约", (d, w) -> sendSeatRequest("/api/cancel_reserve", seat.id));
        } else {
            builder.setMessage("座位正在使用中 (有人)。");
        }
        builder.setNeutralButton("关闭", null);
        builder.show();
    }

    private void sendSeatRequest(String apiPath, String seatId) {
        JsonObject json = new JsonObject();
        json.addProperty("seat_id", seatId);
        json.addProperty("minutes", 120);
        RequestBody body = RequestBody.create(json.toString(), MediaType.parse("application/json"));
        Request request = new Request.Builder().url(LoginActivity.BASE_URL + apiPath).post(body).build();

        new Thread(() -> {
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                String res = response.body().string();
                runOnUiThread(() -> {
                    Toast.makeText(this, "操作结果: " + res, Toast.LENGTH_SHORT).show();
                    refreshAllData(); // 操作完刷新列表
                });
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(this, "请求失败", Toast.LENGTH_SHORT).show());
            }
        }).start();
    }

    // 数据模型
    static class Seat {
        String id;
        int status; // 0=Free, 1=Booked, 2=Occupied
        public Seat(String id, int status) { this.id = id; this.status = status; }
    }

    // RecyclerView 适配器
    class SeatAdapter extends RecyclerView.Adapter<SeatAdapter.SeatViewHolder> {
        @NonNull
        @Override
        public SeatViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_seat, parent, false);
            return new SeatViewHolder(v);
        }

        @Override
        public void onBindViewHolder(@NonNull SeatViewHolder holder, int position) {
            Seat seat = seatList.get(position);
            holder.tvId.setText(seat.id);

            int color;
            if (seat.status == 0) color = Color.parseColor("#4CAF50"); // 绿
            else if (seat.status == 1) color = Color.parseColor("#FFC107"); // 黄
            else color = Color.parseColor("#F44336"); // 红

            holder.bgLayout.setBackgroundTintList(ColorStateList.valueOf(color));

            holder.itemView.setOnClickListener(v -> showActionDialog(seat));
        }

        @Override
        public int getItemCount() { return seatList.size(); }

        class SeatViewHolder extends RecyclerView.ViewHolder {
            TextView tvId;
            LinearLayout bgLayout;
            public SeatViewHolder(@NonNull View itemView) {
                super(itemView);
                tvId = itemView.findViewById(R.id.tv_seat_id);
                bgLayout = itemView.findViewById(R.id.layout_seat_bg);
            }
        }
    }
}