package com.example.my_bishe;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import com.example.my_bishe.ui.*;
import com.google.android.material.bottomnavigation.BottomNavigationView;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 1. 获取当前用户角色
        SharedPreferences sp = getSharedPreferences("config", Context.MODE_PRIVATE);
        String role = sp.getString("role", "user"); // 默认为 user

        BottomNavigationView bottomNav = findViewById(R.id.bottom_navigation);

        // 2. 根据角色控制菜单显示
        // 如果不是管理员，隐藏“数据”和“管理”页面
        if (!"admin".equals(role)) {
            Menu menu = bottomNav.getMenu();
            // 隐藏 nav_stats (数据/报表)
            MenuItem statsItem = menu.findItem(R.id.nav_stats);
            if (statsItem != null) statsItem.setVisible(false);

            // 隐藏 nav_admin (管理)
            MenuItem adminItem = menu.findItem(R.id.nav_admin);
            if (adminItem != null) adminItem.setVisible(false);
        }

        bottomNav.setOnNavigationItemSelectedListener(new BottomNavigationView.OnNavigationItemSelectedListener() {
            @Override
            public boolean onNavigationItemSelected(@NonNull MenuItem item) {
                Fragment selectedFragment = null;
                int id = item.getItemId();

                if (id == R.id.nav_home) selectedFragment = new HomeFragment();
                else if (id == R.id.nav_reserve) selectedFragment = new ReserveFragment();
                else if (id == R.id.nav_stats) selectedFragment = new AdminFragment(); // 此时普通用户点不到这个
                else if (id == R.id.nav_profile) selectedFragment = new ProfileFragment();
                else if (id == R.id.nav_admin) selectedFragment = new AdminFragment(); // 此时普通用户点不到这个

                if (selectedFragment != null) {
                    getSupportFragmentManager().beginTransaction()
                            .replace(R.id.fragment_container, selectedFragment)
                            .commit();
                }
                return true;
            }
        });

        // 默认显示首页
        if (savedInstanceState == null) {
            getSupportFragmentManager().beginTransaction()
                    .replace(R.id.fragment_container, new HomeFragment())
                    .commit();
        }
    }
}