package com.example.my_bishe.ui;

import android.content.Intent;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import androidx.fragment.app.Fragment;
import com.example.my_bishe.*;
import com.google.gson.Gson;
import com.google.gson.JsonObject;
import okhttp3.*;

public class ProfileFragment extends Fragment {
    private TextView tvUser, tvStatus;
    private EditText etUid;

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup c, Bundle s) {
        View v = inflater.inflate(R.layout.fragment_profile, c, false);
        tvUser = v.findViewById(R.id.tv_username);
        tvStatus = v.findViewById(R.id.tv_uid_status);
        etUid = v.findViewById(R.id.et_uid);

        v.findViewById(R.id.btn_bind).setOnClickListener(x -> bindUid());
        v.findViewById(R.id.btn_logout).setOnClickListener(x -> {
            startActivity(new Intent(getActivity(), LoginActivity.class));
            if(getActivity()!=null) getActivity().finish();
        });

        loadProfile();
        return v;
    }

    private void loadProfile() {
        new Thread(() -> {
            try (Response r = LoginActivity.client.newCall(new Request.Builder().url(LoginActivity.BASE_URL + "/api/user/profile").get().build()).execute()) {
                JsonObject json = new Gson().fromJson(r.body().string(), JsonObject.class);
                if(getActivity()!=null) getActivity().runOnUiThread(() -> {
                    tvUser.setText(json.get("username").getAsString());
                    String uid = json.get("uid").getAsString();
                    if (!uid.isEmpty()) {
                        tvStatus.setText("已绑定: " + uid);
                        etUid.setText(uid);
                        tvStatus.setTextColor(android.graphics.Color.parseColor("#4CAF50"));
                    }
                });
            } catch (Exception e) {}
        }).start();
    }

    private void bindUid() {
        String uid = etUid.getText().toString().trim();
        JsonObject j = new JsonObject(); j.addProperty("uid", uid);
        RequestBody b = RequestBody.create(j.toString(), MediaType.parse("application/json"));
        new Thread(() -> {
            try {
                LoginActivity.client.newCall(new Request.Builder().url(LoginActivity.BASE_URL + "/api/user/bind").post(b).build()).execute();
                if(getActivity()!=null) getActivity().runOnUiThread(() -> {
                    Toast.makeText(getContext(), "绑定成功", Toast.LENGTH_SHORT).show();
                    loadProfile();
                });
            } catch (Exception e) {}
        }).start();
    }
}