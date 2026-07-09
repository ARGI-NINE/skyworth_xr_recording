package com.ssnwt.helloxr.ble;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "BootReceiver";
    private static final long RETRY_DELAY_MS = 5000L;
    private static final String ACTION_QUICKBOOT_POWERON = "android.intent.action.QUICKBOOT_POWERON";
    private static final String ACTION_HTC_QUICKBOOT_POWERON = "com.htc.intent.action.QUICKBOOT_POWERON";

    private static Context getApplicationContext(Context context) {
        Context applicationContext = context.getApplicationContext();
        return applicationContext != null ? applicationContext : context;
    }

    private static boolean isSupportedBootAction(String action) {
        return Intent.ACTION_BOOT_COMPLETED.equals(action)
                || Intent.ACTION_LOCKED_BOOT_COMPLETED.equals(action)
                || ACTION_QUICKBOOT_POWERON.equals(action)
                || ACTION_HTC_QUICKBOOT_POWERON.equals(action);
    }

    private static void requestBleServiceStart(Context context, Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    public static void startBleService(Context context) {
        Context appContext = getApplicationContext(context);
        Intent intent = new Intent(appContext, BleService.class);
        try {
            requestBleServiceStart(appContext, intent);
            Log.i(TAG, "BleService start requested");
        } catch (Exception e) {
            Log.e(TAG, "Failed to start BleService, retrying in 5s: " + e.getMessage());
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                try {
                    requestBleServiceStart(appContext, intent);
                } catch (Exception retryError) {
                    Log.e(TAG, "BleService retry failed: " + retryError.getMessage());
                }
            }, RETRY_DELAY_MS);
        }
    }

    public static void startBootFlow(Context context, String action) {
        if (!isSupportedBootAction(action)) {
            Log.w(TAG, "Ignoring non-boot action: " + action);
            return;
        }
        startBleService(context);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) {
            return;
        }
        String action = intent.getAction();
        Log.i(TAG, "Received action: " + action);
        startBootFlow(context, action);
    }
}
