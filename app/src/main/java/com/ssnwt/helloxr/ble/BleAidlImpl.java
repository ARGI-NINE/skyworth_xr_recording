package com.ssnwt.helloxr.ble;

import android.os.Handler;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;
import android.bluetooth.BluetoothDevice;

import com.ssnwt.helloxr.IBleCallback;
import com.ssnwt.helloxr.IBleService;

import java.util.concurrent.CopyOnWriteArrayList;

public class BleAidlImpl extends IBleService.Stub {
    private static final String TAG = "BleAidlImpl";
    private static final long PROVISIONING_DISCONNECT_DELAY_MS = 750L;

    public interface BleControlListener {
        boolean onControlCommand(BluetoothDevice device, String command);

        void onBleConnectionChanged(boolean connected);
    }

    private final BleServerManager bleServerManager;
    private final WifiConnector wifiConnector;
    private final BleControlListener bleControlListener;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final CopyOnWriteArrayList<IBleCallback> callbacks = new CopyOnWriteArrayList<>();

    public BleAidlImpl(
            BleServerManager bleServerManager,
            WifiConnector wifiConnector,
            BleControlListener bleControlListener) {
        this.bleServerManager = bleServerManager;
        this.wifiConnector = wifiConnector;
        this.bleControlListener = bleControlListener;
        setupListeners();
    }

    private void notifyBleConnectionChanged(boolean connected) {
        for (IBleCallback callback : callbacks) {
            try {
                callback.onBleConnectionChanged(connected);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify ble connection", e);
                callbacks.remove(callback);
            }
        }
    }

    private void notifyCommand(String command) {
        if (callbacks.isEmpty()) {
            Log.w(TAG, "Dropping BLE command because no callbacks are registered: " + command);
            return;
        }
        for (IBleCallback callback : callbacks) {
            try {
                callback.onCommand(command);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify command", e);
                callbacks.remove(callback);
            }
        }
    }

    private void notifyWifiConnected(String ipAddress) {
        for (IBleCallback callback : callbacks) {
            try {
                callback.onWifiConnected(ipAddress);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify wifi connected", e);
                callbacks.remove(callback);
            }
        }
    }

    private void notifyWifiFailed(String message) {
        for (IBleCallback callback : callbacks) {
            try {
                callback.onWifiFailed(message);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to notify wifi failed", e);
                callbacks.remove(callback);
            }
        }
    }

    private void setupListeners() {
        bleServerManager.setOnWifiProvisionRequestListener(
                (ssid, password) ->
                        mainHandler.post(
                                () -> {
                                    Log.i(TAG, "WiFi connect requested via FFE3 for SSID: " + ssid);
                                    bleServerManager.notifyWifiStatus(
                                            BleServerManager.WIFI_STATUS_CONNECTING);
                                    wifiConnector.connectWifi(
                                            ssid,
                                            password,
                                            new WifiConnector.OnWifiConnectListener() {
                                                @Override
                                                public void onWifiConnected(String ipAddress) {
                                                    Log.i(TAG, "WiFi connected, IP: " + ipAddress);
                                                    mainHandler.post(
                                                            () -> {
                                                                bleServerManager.notifyWifiStatus(
                                                                        BleServerManager
                                                                                .WIFI_STATUS_CONNECTED);
                                                                if (ipAddress != null
                                                                        && !ipAddress.isEmpty()) {
                                                                    bleServerManager.notifyIpAddress(
                                                                            ipAddress);
                                                                } else {
                                                                    Log.w(
                                                                            TAG,
                                                                            "WiFi connected without IP notification payload");
                                                                }
                                                                notifyWifiConnected(ipAddress);
                                                                mainHandler.postDelayed(
                                                                        bleServerManager
                                                                                ::finishProvisioningSession,
                                                                        PROVISIONING_DISCONNECT_DELAY_MS);
                                                            });
                                                }

                                                @Override
                                                public void onWifiConnecting() {
                                                    mainHandler.post(
                                                            () ->
                                                                    bleServerManager.notifyWifiStatus(
                                                                            BleServerManager
                                                                                    .WIFI_STATUS_CONNECTING));
                                                }

                                                @Override
                                                public void onWifiFailed(String message) {
                                                    Log.e(TAG, "WiFi connection failed: " + message);
                                                    mainHandler.post(
                                                            () -> {
                                                                bleServerManager.notifyWifiStatus(
                                                                        BleServerManager
                                                                                .WIFI_STATUS_FAILED);
                                                                notifyWifiFailed(message);
                                                            });
                                                }
                                            });
                                }));
        bleServerManager.setOnControlCommandListener(
                (device, command) -> {
                    Log.i(
                            TAG,
                            "Control command from BLE: address="
                                    + device.getAddress()
                                    + " payload="
                                    + command);
                    if (bleControlListener != null
                            && bleControlListener.onControlCommand(device, command)) {
                        return;
                    }
                    notifyCommand(command);
                });
        bleServerManager.setOnConnectionStateChangeListener(
                connected -> {
                    Log.i(TAG, "BLE connection changed: " + connected);
                    if (bleControlListener != null) {
                        bleControlListener.onBleConnectionChanged(connected);
                    }
                    notifyBleConnectionChanged(connected);
                });
    }

    @Override
    public String getWifiIpAddress() {
        return wifiConnector.getIpAddress();
    }

    @Override
    public String getWifiSsid() {
        return wifiConnector.getConnectedSsid();
    }

    @Override
    public void sendCommandResponse(String response) {
        bleServerManager.sendCommandResponse(response);
    }

    @Override
    public void sendErrorMessage(String message) {
        bleServerManager.sendErrorMessage(message);
    }

    @Override
    public void registerCallback(IBleCallback callback) {
        if (callback == null) {
            return;
        }
        callbacks.add(callback);
        Log.i(TAG, "Callback registered, total: " + callbacks.size());
        if (bleServerManager.isDeviceConnected()) {
            try {
                callback.onBleConnectionChanged(true);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to replay ble connection state", e);
                callbacks.remove(callback);
            }
        }
        String ipAddress = wifiConnector.getIpAddress();
        if (ipAddress != null && !ipAddress.isEmpty()) {
            try {
                callback.onWifiConnected(ipAddress);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to replay wifi connection state", e);
                callbacks.remove(callback);
            }
        }
    }

    @Override
    public void unregisterCallback(IBleCallback callback) {
        if (callback == null) {
            return;
        }
        callbacks.remove(callback);
        Log.i(TAG, "Callback unregistered, total: " + callbacks.size());
    }
}
