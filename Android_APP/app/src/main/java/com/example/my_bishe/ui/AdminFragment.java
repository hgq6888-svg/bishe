package com.example.my_bishe.ui;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import androidx.fragment.app.Fragment;
import com.example.my_bishe.R;

public class AdminFragment extends Fragment {
    @Override
    public View onCreateView(LayoutInflater i, ViewGroup c, Bundle s) {
        // 直接使用一个简单的 TextView 展示，你可以后续扩展
        TextView tv = new TextView(getContext());
        tv.setText("后台管理与数据报表功能\n(请在 Web 端使用完整功能)\n\n1. 异常报警\n2. 用户管理");
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(18);
        return tv;
    }
}