package com.example.glvulkantest;

public class Native
{
    static {
        System.loadLibrary("glvulkantest_native");
    }
    public static native long create(Object asset_manager, Object view, int width, int height);
    public static native void destroy(long ptr);
    public static native void resize(long ptr, int width, int height);
    public static native void draw(long ptr);
}
