package com.ssnwt.helloxr.ble;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import androidx.core.app.NotificationCompat;

import com.ssnwt.helloxr.R;

import org.json.JSONObject;

public class BleService extends Service implements BleAidlImpl.BleControlListener {
    private static final String CHANNEL_ID = "ble_service_channel";
    private static final int NOTIFICATION_ID = 1001;
    private static final String TAG = "BleService";

    private BleAidlImpl bleAidlImpl;
    private BleServerManager bleServerManager;
    private WifiConnector wifiConnector;
    private HotspotManager hotspotManager;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private long timeSyncHandle = 0L;

    private native void nativeInitBleService(String filesDir);

    private native long nativeCreateTimeSyncHandle();

    private native void nativeDestroyTimeSyncHandle(long handle);

    private native String nativeOnBleReady(long handle, long nowBootTimeNs);

    private native String nativeOnBleCommand(long handle, String commandJson, long recvBootTimeNs);

    private native void nativeOnBleDisconnected(long handle);

    static {
        System.loadLibrary("mixedreality");
    }

    private void dispatchNativePayload(String payload) {
        if (payload == null || payload.isEmpty() || bleServerManager == null) {
            return;
        }
        try {
            JSONObject json = new JSONObject(payload);
            if ("failed".equals(json.optString("status"))) {
                bleServerManager.sendErrorMessage(payload);
            } else {
                bleServerManager.sendCommandResponse(payload);
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to parse native BLE payload, sending as response", e);
            bleServerManager.sendCommandResponse(payload);
        }
    }

    private boolean isTimeSyncCommand(String command) {
        try {
            JSONObject json = new JSONObject(command);
            String type = json.optString("type");
            return "start_time_sync".equals(type)
                    || "time_sync_request".equals(type)
                    || "cancel_sync".equals(type)
                    || "sync_result".equals(type)
                    || "get_time_sync_status".equals(type);
        } catch (Exception e) {
            return command != null
                    && (command.contains("start_time_sync")
                            || command.contains("time_sync_request")
                            || command.contains("cancel_sync")
                            || command.contains("sync_result")
                            || command.contains("get_time_sync_status"));
        }
    }

    private void onControlChannelReady() {
        if (timeSyncHandle == 0L) {
            return;
        }
        dispatchNativePayload(
                nativeOnBleReady(timeSyncHandle, SystemClock.elapsedRealtimeNanos()));
    }

    private Notification buildNotification() {
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("XR Camera Control")
                .setContentText("BLE service running")
                .setSmallIcon(R.mipmap.ic_launcher)
                .setOngoing(true)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationChannel channel =
                new NotificationChannel(
                        CHANNEL_ID,
                        "BLE Service",
                        NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("BLE connection service");
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }
    }

    private void startBleAdvertising() {
        if (bleServerManager == null) {
            return;
        }
        boolean started = bleServerManager.startAdvertising();
        Log.i(TAG, "BLE advertising started: " + started);
        if (!started) {
            Log.e(TAG, "Failed to start BLE advertising, retrying in 3s");
            mainHandler.postDelayed(bleServerManager::startAdvertising, 3000L);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "BleService onBind");
        return bleAidlImpl;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "BleService onCreate");
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, buildNotification());
        hotspotManager = new HotspotManager(getApplication());
        hotspotManager.start();
        wifiConnector = new WifiConnector(this);
        bleServerManager = new BleServerManager(this);
        bleServerManager.setOnControlChannelReadyListener(this::onControlChannelReady);
        nativeInitBleService(getFilesDir().getAbsolutePath());
        timeSyncHandle = nativeCreateTimeSyncHandle();
        bleAidlImpl = new BleAidlImpl(bleServerManager, wifiConnector, this);
        startBleAdvertising();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "BleService onDestroy");
        if (hotspotManager != null) {
            hotspotManager.stop();
            hotspotManager = null;
        }
        mainHandler.removeCallbacksAndMessages(null);
        if (wifiConnector != null) {
            wifiConnector.disconnect();
            wifiConnector = null;
        }
        if (bleServerManager != null) {
            bleServerManager.close();
            bleServerManager = null;
        }
        if (timeSyncHandle != 0L) {
            nativeDestroyTimeSyncHandle(timeSyncHandle);
            timeSyncHandle = 0L;
        }
        bleAidlImpl = null;
        super.onDestroy();
    }

    @Override
    public void onRebind(Intent intent) {
        Log.i(TAG, "BleService onRebind");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "BleService onStartCommand");
        if (hotspotManager != null) {
            hotspotManager.start();
        }
        if (bleServerManager != null && !bleServerManager.isDeviceConnected()) {
            startBleAdvertising();
        }
        return START_STICKY;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "BleService onUnbind");
        return true;
    }

    @Override
    public boolean onControlCommand(String command) {
        if (timeSyncHandle == 0L || !isTimeSyncCommand(command)) {
            return false;
        }
        dispatchNativePayload(
                nativeOnBleCommand(
                        timeSyncHandle,
                        command,
                        SystemClock.elapsedRealtimeNanos()));
        return true;
    }

    @Override
    public void onBleConnectionChanged(boolean connected) {
        if (connected || timeSyncHandle == 0L) {
            return;
        }
        nativeOnBleDisconnected(timeSyncHandle);
    }
}
