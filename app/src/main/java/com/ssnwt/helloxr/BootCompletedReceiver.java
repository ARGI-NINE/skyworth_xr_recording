package com.ssnwt.helloxr;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class BootCompletedReceiver extends BroadcastReceiver {
    private static final String TAG = "BootCompletedReceiver";
    private static final String ACTION_QUICKBOOT_POWERON = "android.intent.action.QUICKBOOT_POWERON";
    private static final String ACTION_HTC_QUICKBOOT_POWERON = "com.htc.intent.action.QUICKBOOT_POWERON";

    private static boolean isSupportedBootAction(String action) {
        return Intent.ACTION_BOOT_COMPLETED.equals(action)
                || ACTION_QUICKBOOT_POWERON.equals(action)
                || ACTION_HTC_QUICKBOOT_POWERON.equals(action);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || intent.getAction() == null) {
            return;
        }

        String action = intent.getAction();
        if (!isSupportedBootAction(action)) {
            Log.w(TAG, "Ignoring unsupported boot action: " + action);
            return;
        }

        Log.i(TAG, "Boot broadcast received, launching VrNativeActivity: " + action);
        Intent launchIntent = new Intent(context, VrNativeActivity.class);
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(launchIntent);
    }
}
