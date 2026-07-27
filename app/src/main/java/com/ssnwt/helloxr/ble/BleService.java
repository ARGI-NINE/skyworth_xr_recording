package com.ssnwt.helloxr.ble;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.bluetooth.BluetoothDevice;
import android.content.Intent;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import androidx.core.app.NotificationCompat;

import com.ssnwt.helloxr.R;
import com.ssnwt.vr.androidmanager.AndroidInterface;

import org.json.JSONObject;

import java.util.HashMap;
import java.util.ArrayList;
import java.util.Map;

public class BleService extends Service implements BleAidlImpl.BleControlListener {
    private static final String CHANNEL_ID = "ble_service_channel";
    private static final int NOTIFICATION_ID = 1001;
    private static final long BLE_ADVERTISING_RETRY_DELAY_MS = 1000L;
    private static final String TAG = "BleService";

    private BleAidlImpl bleAidlImpl;
    private BleServerManager bleServerManager;
    private WifiConnector wifiConnector;
    private HotspotManager hotspotManager;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Map<String, Long> timeSyncHandles = new HashMap<>();
    private boolean bleApiInitializationRequested;
    private final Runnable bleAdvertisingRetry = this::startBleAdvertising;

    private final AndroidInterface.InitListener bleApiInitListener =
            new AndroidInterface.InitListener() {
                @Override
                public void onInitialized() {
                    mainHandler.post(
                            () -> {
                                bleApiInitializationRequested = false;
                                startBleAdvertising();
                            });
                }

                @Override
                public void onReleased() {
                    mainHandler.post(
                            () -> {
                                bleApiInitializationRequested = false;
                                scheduleBleAdvertisingRetry();
                            });
                }

                @Override
                public void onInitError() {
                    mainHandler.post(
                            () -> {
                                bleApiInitializationRequested = false;
                                Log.e(TAG, "SVR AndroidInterface initialization failed");
                                scheduleBleAdvertisingRetry();
                            });
                }
            };

    private native void nativeInitBleService(String filesDir);

    private native long nativeCreateTimeSyncHandle();

    private native void nativeDestroyTimeSyncHandle(long handle);

    private native String nativeOnBleReady(long handle, long nowBootTimeNs);

    private native String nativeOnBleCommand(long handle, String commandJson, long recvBootTimeNs);

    private native void nativeOnBleDisconnected(long handle);

    static {
        System.loadLibrary("mixedreality");
    }

    private void dispatchNativePayload(BluetoothDevice device, String payload) {
        if (payload == null || payload.isEmpty() || bleServerManager == null) {
            return;
        }
        try {
            JSONObject json = new JSONObject(payload);
            if ("failed".equals(json.optString("status"))) {
                bleServerManager.sendErrorMessage(device, payload);
            } else {
                bleServerManager.sendCommandResponse(device, payload);
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to parse native BLE payload, sending as response", e);
            bleServerManager.sendCommandResponse(device, payload);
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

    private void onControlChannelReady(BluetoothDevice device) {
        long handle = getOrCreateTimeSyncHandle(device);
        if (handle == 0L) {
            return;
        }
        dispatchNativePayload(
                device,
                nativeOnBleReady(handle, SystemClock.elapsedRealtimeNanos()));
    }

    private synchronized long getOrCreateTimeSyncHandle(BluetoothDevice device) {
        if (device == null) {
            return 0L;
        }
        String address = device.getAddress();
        Long existing = timeSyncHandles.get(address);
        if (existing != null) {
            return existing;
        }
        long handle = nativeCreateTimeSyncHandle();
        if (handle != 0L) {
            timeSyncHandles.put(address, handle);
            Log.i(TAG, "Created BLE time-sync session: address=" + address);
        }
        return handle;
    }

    private synchronized void releaseTimeSyncHandle(BluetoothDevice device) {
        if (device == null) {
            return;
        }
        Long handle = timeSyncHandles.remove(device.getAddress());
        if (handle == null || handle == 0L) {
            return;
        }
        nativeOnBleDisconnected(handle);
        nativeDestroyTimeSyncHandle(handle);
        Log.i(TAG, "Released BLE time-sync session: address=" + device.getAddress());
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

        AndroidInterface androidInterface = AndroidInterface.getInstance();
        if (!androidInterface.isInitialized()) {
            if (!bleApiInitializationRequested) {
                bleApiInitializationRequested = true;
                Log.i(TAG, "Waiting for SVR AndroidInterface before BLE advertising");
                try {
                    androidInterface.init(getApplication(), bleApiInitListener);
                } catch (Exception e) {
                    bleApiInitializationRequested = false;
                    Log.e(TAG, "Failed to initialize SVR AndroidInterface", e);
                    scheduleBleAdvertisingRetry();
                }
            }
            return;
        }

        bleApiInitializationRequested = false;
        boolean started = bleServerManager.startAdvertising();
        Log.i(TAG, "BLE advertising started: " + started);
        if (!started) {
            Log.e(TAG, "Failed to start BLE advertising, retrying");
            scheduleBleAdvertisingRetry();
        }
    }

    private void scheduleBleAdvertisingRetry() {
        if (bleServerManager == null) {
            return;
        }
        mainHandler.removeCallbacks(bleAdvertisingRetry);
        mainHandler.postDelayed(bleAdvertisingRetry, BLE_ADVERTISING_RETRY_DELAY_MS);
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
        bleServerManager.setOnDeviceDisconnectedListener(this::releaseTimeSyncHandle);
        nativeInitBleService(getFilesDir().getAbsolutePath());
        bleAidlImpl =
                new BleAidlImpl(
                        bleServerManager,
                        wifiConnector,
                        this,
                        () -> {
                            if (hotspotManager != null) {
                                hotspotManager.restartAfterWifiProvisioning();
                            }
                        });
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
        ArrayList<Long> handles;
        synchronized (this) {
            handles = new ArrayList<>(timeSyncHandles.values());
            timeSyncHandles.clear();
        }
        for (long handle : handles) {
            nativeOnBleDisconnected(handle);
            nativeDestroyTimeSyncHandle(handle);
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
        if (bleServerManager != null) {
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
    public boolean onControlCommand(BluetoothDevice device, String command) {
        if (!isTimeSyncCommand(command)) {
            return false;
        }
        long handle = getOrCreateTimeSyncHandle(device);
        if (handle == 0L) {
            return false;
        }
        dispatchNativePayload(
                device,
                nativeOnBleCommand(
                        handle,
                        command,
                        SystemClock.elapsedRealtimeNanos()));
        return true;
    }

    @Override
    public void onBleConnectionChanged(boolean connected) {
        // Per-device native sessions are released by the device disconnect callback.
    }
}
