package com.example.reactivecppmarkup;

import static android.content.ClipDescription.MIMETYPE_TEXT_PLAIN;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Debug;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.AttributeSet;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;

import androidx.annotation.NonNull;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

public class ExtendedNative extends Activity implements SurfaceHolder.Callback2, ViewTreeObserver.OnGlobalLayoutListener {

    NativeContentView content;
    boolean isDestroyed;
    boolean isSoftKeyboardShown;
    int window_width;
    int window_height;
    int window_x;
    int window_y;
    SurfaceHolder surface_holder;

    static class NativeContentView extends androidx.appcompat.widget.AppCompatEditText
    {
        ExtendedNative activity;

        public NativeContentView(Context context) {
            super(context);
        }

        public NativeContentView(Context context, AttributeSet attrs) {
            super(context, attrs);
        }
    }

    public static native void android_bootstrap(ExtendedNative activity, AssetManager manager);
    public static native void android_set_surface(Surface surface);
    public static native void android_window_changed(int width, int height);
    public static native void android_on_pause();
    public static native void android_on_resume();

    public static native void android_on_key(int key_code, int unicode, int action);

    public static native void android_on_soft_keyboard(boolean is_shown);
    public static native void android_on_soft_key(String entered);

    public static native void android_on_mouse_button(int button, int action);
    public static native void android_on_mouse_scroll(float x_axis, float y_axis);
    public static native void android_on_mouse_move(float x, float y);

    public static native void android_on_gesture(int pointer_count, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4);
    public static native void android_on_touch(int action);


    static
    {
        System.loadLibrary("rcm");
    }

    public String get_text_clipboard()
    {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if(clipboard == null)
        {
            return null;
        }

        ClipDescription primary_desc = clipboard.getPrimaryClipDescription();
        if(primary_desc == null || !primary_desc.hasMimeType(MIMETYPE_TEXT_PLAIN))
        {
            return null;
        }

        ClipData item = clipboard.getPrimaryClip();

        if(item != null)
        {
            ClipData.Item clipboard_data = item.getItemAt(0);
            return clipboard_data.getText().toString();
        }
        return null;
    }

    public void set_text_clipboard(String text)
    {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if(clipboard == null)
        {
            return;
        }

        clipboard.setPrimaryClip(ClipData.newPlainText("", text));
    }

    public void show_soft_keyboard()
    {
        if(isSoftKeyboardShown)
        {
            return; // Already shown
        }

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                content.setEnabled(true);
                content.requestFocus();

                InputMethodManager input_manager = (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
                input_manager.toggleSoftInputFromWindow(content.getApplicationWindowToken(), InputMethodManager.SHOW_FORCED, 0);
                //input_manager.showSoftInput(content, InputMethodManager.SHOW_FORCED);
            }
        });
    }

    public void hide_soft_keyboard()
    {
       // if(!isSoftKeyboardShown)
       // {
       //     return; // Already hidden
       // }

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager input_manager = (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
                input_manager.hideSoftInputFromWindow(content.getApplicationWindowToken(), 0);
                content.setEnabled(false);
            }
        });
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event)
    {
        android_on_key(event.getKeyCode(), event.getUnicodeChar(event.getMetaState()), event.getAction());

        if(isSoftKeyboardShown)
        {
            return super.dispatchKeyEvent(event);
        }

        // Note(Leo): When not using a soft keyboard we capture all keyboard events
        return true;
    }

    public void touch_cursor_positions(MotionEvent motion)
    {
        int pointer_count = motion.getPointerCount();

        // Note(Leo): We only support 5 touch points here since thats what the platform layer also supports
        float[] x_positions = new float[5];
        float[] y_positions = new float[5];

        if(pointer_count > 5)
        {
            pointer_count = 5;
        }

        for(int i = 0; i < pointer_count; i++)
        {
            x_positions[i] = motion.getX(i);
            y_positions[i] = motion.getY(i);
        }

        android_on_gesture(pointer_count, x_positions[0], y_positions[0], x_positions[1], y_positions[1], x_positions[2], y_positions[2], x_positions[3], y_positions[3], x_positions[4], y_positions[4]);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent motion)
    {

        switch(motion.getAction())
        {
            case(MotionEvent.ACTION_DOWN):
            {
                touch_cursor_positions(motion);

                android_on_touch(1);
                break;
            }
            case(MotionEvent.ACTION_POINTER_DOWN):
            {

                break;
            }
            case(MotionEvent.ACTION_MOVE):
            {
                touch_cursor_positions(motion);
                break;
            }
            case(MotionEvent.ACTION_UP):
            {
                touch_cursor_positions(motion);

                android_on_touch(0);
                break;
            }
            case(MotionEvent.ACTION_POINTER_UP):
            {

                break;
            }
        }

        return super.dispatchTouchEvent(motion);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent motion)
    {

        Log.d("Vulkan tutorials", "Mouse event");

        if(motion.isFromSource(InputDevice.SOURCE_CLASS_POINTER))
        {
            switch (motion.getAction())
            {
                case(MotionEvent.ACTION_HOVER_ENTER):
                case(MotionEvent.ACTION_HOVER_MOVE):
                {
                    android_on_mouse_move(motion.getX(), motion.getY());
                    Log.d("Vulkan tutorials", "Mouse move");
                    break;
                }
                case(MotionEvent.ACTION_BUTTON_PRESS):
                case(MotionEvent.ACTION_BUTTON_RELEASE):
                {
                    int button = 0;
                    switch(motion.getActionButton())
                    {
                        case(MotionEvent.BUTTON_PRIMARY):
                        {
                            button = 0;
                            break;
                        }
                        case(MotionEvent.BUTTON_SECONDARY):
                        {
                            button = 1;
                            break;
                        }
                        case(MotionEvent.BUTTON_TERTIARY):
                        {
                            button = 2;
                        }
                        default:
                        {
                            return super.dispatchGenericMotionEvent(motion);
                        }
                    }

                    int action;
                    if(motion.getAction() == MotionEvent.ACTION_BUTTON_PRESS)
                    {
                        action = 1;
                    }
                    else if(motion.getAction() == MotionEvent.ACTION_BUTTON_RELEASE)
                    {
                        action = 0;
                    }
                    else
                    {
                        return super.dispatchGenericMotionEvent(motion);
                    }

                    android_on_mouse_button(button, action);

                    break;
                }

                case(MotionEvent.ACTION_SCROLL):
                {
                    android_on_mouse_scroll(motion.getAxisValue(MotionEvent.AXIS_HSCROLL), motion.getAxisValue(MotionEvent.AXIS_VSCROLL));

                    break;
                }

            }
        }

        return super.dispatchGenericMotionEvent(motion);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        getWindow().takeSurface(this);
        getWindow().setFormat(PixelFormat.RGB_565);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_UNSPECIFIED | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        content = new NativeContentView(this);
        content.activity = this;
        setContentView(content);
        content.requestFocus();
        content.getViewTreeObserver().addOnGlobalLayoutListener(this);

        ViewCompat.setOnApplyWindowInsetsListener(content, (v, insets) -> {
            boolean new_value = insets.isVisible(WindowInsetsCompat.Type.ime());
            if(new_value != isSoftKeyboardShown)
            {
                android_on_soft_keyboard(new_value);
                isSoftKeyboardShown = new_value;
            }

            return insets;
        });
        content.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after)
            {
                content.setVisibility(View.INVISIBLE);
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count)
            {
            }

            @Override
            public void afterTextChanged(Editable s)
            {
                //Log.d("Vulkan Tutorials", content.getText().toString());
                String added = content.getText().toString();
                if(!added.isEmpty())
                {
                    android_on_soft_key(added);
                }

                s.clear();

            }
        });

        android_bootstrap(this, getAssets());

        hide_soft_keyboard();

        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onPause()
    {
        android_on_pause();

        super.onPause();
    }


    @Override
    protected void onResume()
    {
        class ResumeThread extends Thread
        {
            public void run()
            {
                android_on_resume();
            }

        }
        ResumeThread resume_thread = new ResumeThread();
        resume_thread.start();

        Log.d("Vulkan Tutorials", "Resume command!");

        super.onResume();
    }

    @Override
    public void surfaceRedrawNeeded(@NonNull SurfaceHolder holder)
    {
        surface_holder = holder;
        Log.d("Vulkan Tutorials", "Surface redraw command!");
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder)
    {
        Log.d("Vulkan Tutorials", "Surface create command!");
        if(!isDestroyed)
        {
            surface_holder = holder;
            android_set_surface(holder.getSurface());
        }
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height)
    {
        Log.d("Vulkan Tutorials", "Surface changed command!");
        if(!isDestroyed)
        {
            surface_holder = holder;
            android_set_surface(holder.getSurface());
        }
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder)
    {
        Log.d("Vulkan Tutorials", "Surface destroy command!");
    }

    @Override
    public void onGlobalLayout()
    {
        Log.d("Vulkan Tutorials", "Global layout command!");
        int[] content_location = new int[2];
        content.getLocationInWindow(content_location);
        int w = content.getWidth();
        int h = content.getHeight();
        if (content_location[0] != window_x || content_location[1] != window_y || w != window_width || h != window_y)
        {
            window_x = content_location[0];
            window_y = content_location[1];
            window_width = w;
            window_height = h;
            if (!isDestroyed)
            {
                android_window_changed(window_width, window_height);
            }
        }
    }
}