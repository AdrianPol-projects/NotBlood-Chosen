package com.adrianpol.notblood;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

import org.libsdl.app.SDLActivity;

/** NotBlood: SDL's activity plus the few things the game asks Android for (see androidbits.cpp). */
public class NotBloodActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /** Opens the system setting that lets NotBlood use the NotBlood folder in the main storage. */
    public static void requestAllFilesAccess() {
        final SDLActivity activity = mSingleton;
        if (activity == null || Build.VERSION.SDK_INT < 30)
            return;
        activity.runOnUiThread(() -> {
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + activity.getPackageName())));
            } catch (Exception e) {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        });
    }

    /** Renders at w x h and lets the hardware scaler fill the screen (0, 0: the screen's size). */
    public static void setRenderSize(final int w, final int h) {
        final SDLActivity activity = mSingleton;
        if (activity == null)
            return;
        activity.runOnUiThread(() -> {
            if (mSurface == null)
                return;
            if (w > 0 && h > 0)
                mSurface.getHolder().setFixedSize(w, h);
            else
                mSurface.getHolder().setSizeFromLayout();
        });
    }
}
