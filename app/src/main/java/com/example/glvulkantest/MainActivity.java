package com.example.glvulkantest;

import androidx.appcompat.app.AppCompatActivity;
import android.content.res.AssetManager;
import android.os.Bundle;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        GLVulkanView view = (GLVulkanView) findViewById(R.id.display);
        AssetManager assets =getAssets();
        view.set_asset_manager(assets);
    }
}