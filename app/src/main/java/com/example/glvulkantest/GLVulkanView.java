package com.example.glvulkantest;

import android.content.Context;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLVulkanView extends GLSurfaceView
{
    private static class GLVulanRenderer implements GLSurfaceView.Renderer, AutoCloseable
    {
        private GLVulkanView m_owner = null;
        private long m_cptr = 0;
        public AssetManager m_asset_manager = null;

        GLVulanRenderer(GLVulkanView owner)
        {
            m_owner = owner;
        }

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config)
        {
            int width = m_owner.getWidth();
            int height = m_owner.getHeight();
            m_cptr = Native.create(m_asset_manager, m_owner, width, height);
        }

        @Override
        public void close()
        {
            if (m_cptr!=0){
                Native.destroy(m_cptr);
                m_cptr = 0;
            }
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height)
        {
            if (m_cptr!=0) {
                Native.resize(m_cptr, width, height);
            }
        }

        @Override
        public void onDrawFrame(GL10 gl)
        {
            if (m_cptr!=0) {
                Native.draw(m_cptr);
            }
        }
    }

    private GLVulanRenderer m_renderer = null;

    public GLVulkanView(Context context)
    {
        super(context);
        init(context);
    }

    public GLVulkanView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context)
    {
        setEGLContextClientVersion(2);
        m_renderer = new GLVulanRenderer(this);
        setRenderer(m_renderer);
    }

    public void set_asset_manager(AssetManager as)
    {
        m_renderer.m_asset_manager = as;
    }

    @Override
    protected void onDetachedFromWindow ()
    {
        m_renderer.close();
        m_renderer = null;
        super.onDetachedFromWindow();
    }


}

