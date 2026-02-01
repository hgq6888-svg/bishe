package com.example.my_bishe.ui;

import android.graphics.Color;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;
import androidx.annotation.NonNull;
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
import java.util.ArrayList;
import java.util.List;
import okhttp3.Request;
import okhttp3.Response;

public class HomeFragment extends Fragment {
    private TextView tvTemp, tvHumi, tvLux;
    private SwipeRefreshLayout swipeRefresh;
    private RecyclerView recyclerView;
    private SeatAdapter adapter;
    private final List<Seat> seatList = new ArrayList<>();
    private final Gson gson = new Gson();

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_home, container, false);
        tvTemp = view.findViewById(R.id.tv_temp);
        tvHumi = view.findViewById(R.id.tv_humi);
        tvLux = view.findViewById(R.id.tv_lux);
        swipeRefresh = view.findViewById(R.id.swipe_refresh);
        recyclerView = view.findViewById(R.id.recycler_view);

        recyclerView.setLayoutManager(new GridLayoutManager(getContext(), 4));
        adapter = new SeatAdapter();
        recyclerView.setAdapter(adapter);
        swipeRefresh.setOnRefreshListener(this::fetchData);
        fetchData();
        return view;
    }

    private void fetchData() {
        swipeRefresh.setRefreshing(true);
        Request request = new Request.Builder().url(LoginActivity.BASE_URL + "/api/state").get().build();
        new Thread(() -> {
            try (Response response = LoginActivity.client.newCall(request).execute()) {
                if (response.isSuccessful()) {
                    JsonObject json = gson.fromJson(response.body().string(), JsonObject.class);
                    if (getActivity() == null) return;
                    getActivity().runOnUiThread(() -> {
                        if (json.has("latest") && !json.get("latest").isJsonNull()) {
                            JsonObject lat = json.getAsJsonObject("latest");
                            tvTemp.setText(lat.get("temp").getAsString() + "°C");
                            tvHumi.setText(lat.get("humi").getAsString() + "%");
                            tvLux.setText(lat.get("lux").getAsString() + "Lx");
                        }
                        seatList.clear();
                        JsonArray arr = json.getAsJsonArray("seats");
                        for (JsonElement e : arr) {
                            JsonObject obj = e.getAsJsonObject();
                            String state = obj.has("state") ? obj.get("state").getAsString() : "FREE";
                            seatList.add(new Seat(obj.get("seat_id").getAsString(), state));
                        }
                        adapter.notifyDataSetChanged();
                        swipeRefresh.setRefreshing(false);
                    });
                }
            } catch (Exception e) {
                if (getActivity() != null) getActivity().runOnUiThread(() -> swipeRefresh.setRefreshing(false));
            }
        }).start();
    }

    class Seat {
        String id; String state;
        public Seat(String id, String state) { this.id = id; this.state = state; }
    }

    class SeatAdapter extends RecyclerView.Adapter<SeatAdapter.Holder> {
        @NonNull @Override
        public Holder onCreateViewHolder(@NonNull ViewGroup p, int t) {
            View v = LayoutInflater.from(p.getContext()).inflate(R.layout.item_seat, p, false);
            return new Holder(v);
        }
        @Override
        public void onBindViewHolder(@NonNull Holder h, int i) {
            Seat s = seatList.get(i);
            h.tv.setText(s.id);
            if (s.state.equals("FREE")) h.v.setBackgroundColor(Color.parseColor("#4CAF50")); // 绿
            else if (s.state.equals("RESERVED")) h.v.setBackgroundColor(Color.parseColor("#FFC107")); // 黄
            else h.v.setBackgroundColor(Color.parseColor("#F44336")); // 红
        }
        @Override
        public int getItemCount() { return seatList.size(); }
        class Holder extends RecyclerView.ViewHolder {
            TextView tv; View v;
            public Holder(@NonNull View iv) { super(iv); tv = iv.findViewById(R.id.tv_seat_id); v = iv.findViewById(R.id.layout_seat_bg); }
        }
    }
}