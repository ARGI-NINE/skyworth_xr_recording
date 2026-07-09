package com.ssnwt.helloxr.ble;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattServer;
import android.bluetooth.BluetoothGattServerCallback;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.AdvertiseCallback;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import com.ssnwt.vr.androidmanager.AndroidInterface;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.UUID;

public class BleServerManager {
    public static final String SERVICE_UUID = "0000FFE0-0000-1000-8000-00805F9B34FB";
    public static final String CHAR_WIFI_SSID_UUID = "0000FFE1-0000-1000-8000-00805F9B34FB";
    public static final String CHAR_WIFI_PASSWORD_UUID = "0000FFE2-0000-1000-8000-00805F9B34FB";
    public static final String CHAR_WIFI_STATUS_UUID = "0000FFE3-0000-1000-8000-00805F9B34FB";
    public static final String CHAR_IP_ADDRESS_UUID = "0000FFE4-0000-1000-8000-00805F9B34FB";
    public static final String CONTROL_SERVICE_UUID = "a1b2c3d4-e5f6-7890-abcd-1234567890ab";
    public static final String CHAR_CONTROL_CMD_UUID = "a1b2c3d4-e5f6-7890-abcd-000000000005";
    public static final String CHAR_CMD_RESPONSE_UUID = "a1b2c3d4-e5f6-7890-abcd-000000000006";
    public static final String CHAR_ERROR_MSG_UUID = "a1b2c3d4-e5f6-7890-abcd-000000000007";

    public static final int WIFI_STATUS_IDLE = 0;
    public static final int WIFI_STATUS_CONNECTING = 1;
    public static final int WIFI_STATUS_CONNECTED = 2;
    public static final int WIFI_STATUS_FAILED = 3;

    private static final String TAG = "BleServerManager";
    private static final UUID SERVICE_ID = UUID.fromString(SERVICE_UUID);
    private static final UUID CONTROL_SERVICE_ID = UUID.fromString(CONTROL_SERVICE_UUID);
    private static final UUID CHAR_WIFI_SSID_ID = UUID.fromString(CHAR_WIFI_SSID_UUID);
    private static final UUID CHAR_WIFI_PASSWORD_ID = UUID.fromString(CHAR_WIFI_PASSWORD_UUID);
    private static final UUID CHAR_WIFI_STATUS_ID = UUID.fromString(CHAR_WIFI_STATUS_UUID);
    private static final UUID CHAR_IP_ADDRESS_ID = UUID.fromString(CHAR_IP_ADDRESS_UUID);
    private static final UUID CHAR_CONTROL_CMD_ID = UUID.fromString(CHAR_CONTROL_CMD_UUID);
    private static final UUID CHAR_CMD_RESPONSE_ID = UUID.fromString(CHAR_CMD_RESPONSE_UUID);
    private static final UUID CHAR_ERROR_MSG_ID = UUID.fromString(CHAR_ERROR_MSG_UUID);
    private static final UUID CCCD_UUID =
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final byte WIFI_CONNECT_TRIGGER = 0x01;
    private static final int MAX_WIFI_SSID_BYTES = 32;
    private static final int MAX_WIFI_PASSWORD_BYTES = 64;
    private static final int WIFI_TRIGGER_BYTES = 1;
    private static final int MANUFACTURER_ID = 4884;
    private static final int RESTART_ADVERTISE_DELAY_MS = 1500;
    private static final int RESTART_ADVERTISE_MAX_RETRIES = 3;

    public interface OnConnectionStateChangeListener {
        void onConnectionStateChanged(boolean connected);
    }

    public interface OnWifiProvisionRequestListener {
        void onWifiProvisionRequested(String ssid, String password);
    }

    public interface OnControlCommandListener {
        void onControlCommand(String command);
    }

    public interface OnControlChannelReadyListener {
        void onControlChannelReady();
    }

    private final Context context;
    private final BluetoothManager bluetoothManager;
    private final BluetoothAdapter bluetoothAdapter;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private BluetoothLeAdvertiser advertiser;
    private BluetoothGattServer gattServer;
    private BluetoothDevice connectedDevice;

    private BluetoothGattCharacteristic wifiSsidChar;
    private BluetoothGattCharacteristic wifiPasswordChar;
    private BluetoothGattCharacteristic wifiStatusChar;
    private BluetoothGattCharacteristic ipAddressChar;
    private BluetoothGattCharacteristic controlCmdChar;
    private BluetoothGattCharacteristic cmdResponseChar;
    private BluetoothGattCharacteristic errorMsgChar;

    private OnConnectionStateChangeListener connectionStateChangeListener;
    private OnWifiProvisionRequestListener wifiProvisionRequestListener;
    private OnControlCommandListener controlCommandListener;
    private OnControlChannelReadyListener controlChannelReadyListener;

    private String pendingSsid;
    private String pendingPassword;
    private String currentIpAddress = "";
    private int currentWifiStatus = WIFI_STATUS_IDLE;
    private boolean isAdvertising = false;
    private boolean restartAdvertisingOnDisconnect = true;
    private boolean wifiStatusNotifyEnabled = false;
    private boolean ipAddressNotifyEnabled = false;
    private boolean cmdResponseNotifyEnabled = false;
    private boolean errorMsgNotifyEnabled = false;
    private boolean controlChannelReadyNotified = false;

    private final AdvertiseCallback advertiseCallback = new AdvertiseCallback() {
        @Override
        public void onStartSuccess(AdvertiseSettings settingsInEffect) {
            isAdvertising = true;
            Log.i(TAG, "BLE advertising started");
        }

        @Override
        public void onStartFailure(int errorCode) {
            isAdvertising = false;
            Log.e(TAG, "BLE advertising failed: " + errorCode);
        }
    };

    @SuppressLint("MissingPermission")
    private final BluetoothGattServerCallback gattServerCallback =
            new BluetoothGattServerCallback() {
                @Override
                public void onConnectionStateChange(BluetoothDevice device, int status, int newState) {
                    Log.i(TAG,
                            "Connection state changed: address="
                                    + device.getAddress()
                                    + " status="
                                    + status
                                    + " newState="
                                    + newState);
                    if (newState == BluetoothProfile.STATE_CONNECTED) {
                        connectedDevice = device;
                        wifiStatusNotifyEnabled = false;
                        ipAddressNotifyEnabled = false;
                        cmdResponseNotifyEnabled = false;
                        errorMsgNotifyEnabled = false;
                        controlChannelReadyNotified = false;
                        stopAdvertising();
                        if (connectionStateChangeListener != null) {
                            connectionStateChangeListener.onConnectionStateChanged(true);
                        }
                        return;
                    }

                    if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        boolean shouldRestartAdvertising = restartAdvertisingOnDisconnect;
                        connectedDevice = null;
                        wifiStatusNotifyEnabled = false;
                        ipAddressNotifyEnabled = false;
                        cmdResponseNotifyEnabled = false;
                        errorMsgNotifyEnabled = false;
                        controlChannelReadyNotified = false;
                        resetProvisioningState();
                        if (connectionStateChangeListener != null) {
                            connectionStateChangeListener.onConnectionStateChanged(false);
                        }
                        if (shouldRestartAdvertising) {
                            restartAdvertisingWithRetry(0);
                        } else {
                            Log.i(TAG, "Provisioning complete, keeping BLE advertising stopped");
                        }
                    }
                }

                @Override
                public void onCharacteristicReadRequest(
                        BluetoothDevice device,
                        int requestId,
                        int offset,
                        BluetoothGattCharacteristic characteristic) {
                    if (gattServer == null) {
                        return;
                    }
                    if ((characteristic.getPermissions()
                                    & BluetoothGattCharacteristic.PERMISSION_READ)
                            == 0) {
                        gattServer.sendResponse(
                                device,
                                requestId,
                                BluetoothGatt.GATT_READ_NOT_PERMITTED,
                                0,
                                null);
                        return;
                    }
                    gattServer.sendResponse(
                            device,
                            requestId,
                            BluetoothGatt.GATT_SUCCESS,
                            0,
                            characteristic.getValue());
                }

                @Override
                public void onCharacteristicWriteRequest(
                        BluetoothDevice device,
                        int requestId,
                        BluetoothGattCharacteristic characteristic,
                        boolean preparedWrite,
                        boolean responseNeeded,
                        int offset,
                        byte[] value) {
                    int responseStatus = BluetoothGatt.GATT_SUCCESS;
                    if (preparedWrite || offset != 0) {
                        responseStatus = BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                    } else if (value == null) {
                        responseStatus = BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
                    } else {
                        responseStatus = handleCharacteristicWrite(characteristic, value);
                    }

                    if (responseNeeded && gattServer != null) {
                        gattServer.sendResponse(device, requestId, responseStatus, 0, null);
                    }
                }

                @Override
                public void onDescriptorReadRequest(
                        BluetoothDevice device,
                        int requestId,
                        int offset,
                        BluetoothGattDescriptor descriptor) {
                    if (gattServer == null) {
                        return;
                    }
                    byte[] value = descriptor.getValue();
                    if (value == null) {
                        value = BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE;
                    }
                    gattServer.sendResponse(
                            device,
                            requestId,
                            BluetoothGatt.GATT_SUCCESS,
                            0,
                            value);
                }

                @Override
                public void onDescriptorWriteRequest(
                        BluetoothDevice device,
                        int requestId,
                        BluetoothGattDescriptor descriptor,
                        boolean preparedWrite,
                        boolean responseNeeded,
                        int offset,
                        byte[] value) {
                    int responseStatus = BluetoothGatt.GATT_SUCCESS;
                    if (preparedWrite || offset != 0) {
                        responseStatus = BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                    } else if (descriptor != null) {
                        descriptor.setValue(value);
                        handleDescriptorWrite(descriptor, value);
                    }

                    if (responseNeeded && gattServer != null) {
                        gattServer.sendResponse(device, requestId, responseStatus, 0, null);
                    }
                }

                @Override
                public void onNotificationSent(BluetoothDevice device, int status) {
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        Log.w(TAG, "Notification send failed: " + status);
                    }
                }
            };

    public BleServerManager(Context context) {
        this.context = context.getApplicationContext();
        this.bluetoothManager =
                (BluetoothManager) this.context.getSystemService(Context.BLUETOOTH_SERVICE);
        this.bluetoothAdapter = bluetoothManager != null ? bluetoothManager.getAdapter() : null;
    }

    private BluetoothGattDescriptor createCccd() {
        BluetoothGattDescriptor descriptor =
                new BluetoothGattDescriptor(
                        CCCD_UUID,
                        BluetoothGattDescriptor.PERMISSION_READ
                                | BluetoothGattDescriptor.PERMISSION_WRITE);
        descriptor.setValue(BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
        return descriptor;
    }

    private String getDeviceSerial() {
        try {
            String serial = AndroidInterface.getInstance().getDeviceUtils().getSerialNumber();
            if (serial == null || serial.isEmpty()) {
                return "VR-UNKNOWN";
            }
            if (serial.length() > 11) {
                String truncatedSerial = serial.substring(serial.length() - 11);
                Log.w(
                        TAG,
                        "Serial truncated from "
                                + serial.length()
                                + " to 11 chars: "
                                + truncatedSerial);
                serial = truncatedSerial;
            }
            Charset charset = StandardCharsets.UTF_8;
            byte[] serialBytes = serial.getBytes(charset);
            int deviceNameBytes =
                    bluetoothAdapter == null || bluetoothAdapter.getName() == null
                            ? 0
                            : bluetoothAdapter.getName().getBytes(charset).length;
            int maxSerialBytes = 27 - (deviceNameBytes + 2);
            if (maxSerialBytes < 1) {
                maxSerialBytes = 1;
            }
            if (serialBytes.length <= maxSerialBytes) {
                return serial;
            }
            int safeLength = Math.min(maxSerialBytes, serialBytes.length - 1);
            while (safeLength > 0 && (serialBytes[safeLength] & 0xC0) == 0x80) {
                safeLength--;
            }
            String truncatedSerial = new String(serialBytes, 0, safeLength, StandardCharsets.UTF_8);
            Log.w(
                    TAG,
                    "Serial truncated from "
                            + serialBytes.length
                            + " to "
                            + safeLength
                            + " bytes: "
                            + truncatedSerial);
            return truncatedSerial;
        } catch (Exception e) {
            Log.e(TAG, "Failed to get device serial", e);
            return "VR-UNKNOWN";
        }
    }

    private int handleCharacteristicWrite(BluetoothGattCharacteristic characteristic, byte[] value) {
        UUID uuid = characteristic.getUuid();
        if (CHAR_WIFI_SSID_ID.equals(uuid)) {
            if (value.length > MAX_WIFI_SSID_BYTES) {
                Log.w(TAG, "Rejecting FFE1 write larger than " + MAX_WIFI_SSID_BYTES + " bytes");
                return BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
            }
            pendingSsid = new String(value, StandardCharsets.UTF_8);
            Log.d(TAG, "WiFi SSID received: " + pendingSsid);
            return BluetoothGatt.GATT_SUCCESS;
        }
        if (CHAR_WIFI_PASSWORD_ID.equals(uuid)) {
            if (value.length > MAX_WIFI_PASSWORD_BYTES) {
                Log.w(
                        TAG,
                        "Rejecting FFE2 write larger than "
                                + MAX_WIFI_PASSWORD_BYTES
                                + " bytes");
                return BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
            }
            pendingPassword = new String(value, StandardCharsets.UTF_8);
            Log.d(TAG, "WiFi password received");
            return BluetoothGatt.GATT_SUCCESS;
        }
        if (CHAR_WIFI_STATUS_ID.equals(uuid)) {
            return handleWifiStatusWrite(value);
        }
        if (CHAR_CONTROL_CMD_ID.equals(uuid)) {
            String command = new String(value, StandardCharsets.UTF_8);
            Log.d(TAG, "Control command received: " + command);
            if (controlCommandListener != null) {
                controlCommandListener.onControlCommand(command);
            }
            return BluetoothGatt.GATT_SUCCESS;
        }
        Log.w(TAG, "Unsupported write characteristic: " + uuid);
        return BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
    }

    private int handleWifiStatusWrite(byte[] value) {
        if (value == null || value.length != WIFI_TRIGGER_BYTES) {
            Log.w(
                    TAG,
                    "Rejecting FFE3 payload length: "
                            + (value == null ? 0 : value.length)
                            + ", expected "
                            + WIFI_TRIGGER_BYTES);
            return BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
        }
        if (value[0] != WIFI_CONNECT_TRIGGER) {
            Log.w(TAG, "Rejecting unsupported FFE3 trigger value: " + (value[0] & 0xFF));
            return BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
        }
        if (pendingSsid == null || pendingSsid.isEmpty()) {
            Log.w(TAG, "WiFi connect trigger ignored: SSID missing");
            notifyWifiStatus(WIFI_STATUS_FAILED);
            return BluetoothGatt.GATT_SUCCESS;
        }
        if (pendingPassword == null) {
            Log.w(TAG, "WiFi connect trigger ignored: password missing");
            notifyWifiStatus(WIFI_STATUS_FAILED);
            return BluetoothGatt.GATT_SUCCESS;
        }
        if (wifiProvisionRequestListener != null) {
            wifiProvisionRequestListener.onWifiProvisionRequested(pendingSsid, pendingPassword);
        } else {
            Log.w(TAG, "No WiFi provision listener registered");
            notifyWifiStatus(WIFI_STATUS_FAILED);
        }
        return BluetoothGatt.GATT_SUCCESS;
    }

    private void handleDescriptorWrite(BluetoothGattDescriptor descriptor, byte[] value) {
        if (descriptor == null || descriptor.getCharacteristic() == null) {
            return;
        }
        UUID uuid = descriptor.getCharacteristic().getUuid();
        boolean enabled = isNotificationEnabled(value);
        if (CHAR_WIFI_STATUS_ID.equals(uuid)) {
            wifiStatusNotifyEnabled = enabled;
            if (enabled) {
                notifyCharacteristic(wifiStatusChar);
            }
            return;
        }
        if (CHAR_IP_ADDRESS_ID.equals(uuid)) {
            ipAddressNotifyEnabled = enabled;
            if (enabled && currentIpAddress != null && !currentIpAddress.isEmpty()) {
                notifyCharacteristic(ipAddressChar);
            }
            return;
        }
        if (CHAR_CMD_RESPONSE_ID.equals(uuid)) {
            cmdResponseNotifyEnabled = enabled;
            updateControlChannelReadyState();
            return;
        }
        if (CHAR_ERROR_MSG_ID.equals(uuid)) {
            errorMsgNotifyEnabled = enabled;
            updateControlChannelReadyState();
        }
    }

    private void updateControlChannelReadyState() {
        boolean isReady =
                connectedDevice != null && cmdResponseNotifyEnabled && errorMsgNotifyEnabled;
        if (!isReady) {
            controlChannelReadyNotified = false;
            return;
        }
        if (controlChannelReadyNotified || controlChannelReadyListener == null) {
            return;
        }
        controlChannelReadyNotified = true;
        controlChannelReadyListener.onControlChannelReady();
    }

    private boolean isNotificationEnabled(byte[] value) {
        return Arrays.equals(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE, value)
                || Arrays.equals(BluetoothGattDescriptor.ENABLE_INDICATION_VALUE, value);
    }

    private void resetProvisioningState() {
        pendingSsid = null;
        pendingPassword = null;
        currentIpAddress = "";
        currentWifiStatus = WIFI_STATUS_IDLE;
        restartAdvertisingOnDisconnect = true;
        if (wifiStatusChar != null) {
            wifiStatusChar.setValue(new byte[] {(byte) currentWifiStatus});
        }
        if (ipAddressChar != null) {
            ipAddressChar.setValue(currentIpAddress);
        }
        if (cmdResponseChar != null) {
            cmdResponseChar.setValue(new byte[0]);
        }
        if (errorMsgChar != null) {
            errorMsgChar.setValue(new byte[0]);
        }
    }

    @SuppressLint("MissingPermission")
    private void notifyCharacteristic(BluetoothGattCharacteristic characteristic) {
        if (gattServer == null || connectedDevice == null || characteristic == null) {
            return;
        }
        UUID uuid = characteristic.getUuid();
        if (CHAR_WIFI_STATUS_ID.equals(uuid) && !wifiStatusNotifyEnabled) {
            return;
        }
        if (CHAR_IP_ADDRESS_ID.equals(uuid) && !ipAddressNotifyEnabled) {
            return;
        }
        if (CHAR_CMD_RESPONSE_ID.equals(uuid) && !cmdResponseNotifyEnabled) {
            return;
        }
        if (CHAR_ERROR_MSG_ID.equals(uuid) && !errorMsgNotifyEnabled) {
            return;
        }
        gattServer.notifyCharacteristicChanged(connectedDevice, characteristic, false);
    }

    @SuppressLint("MissingPermission")
    private void restartAdvertisingWithRetry(int attempt) {
        mainHandler.postDelayed(
                () -> {
                    boolean started = startAdvertising();
                    int nextAttempt = attempt + 1;
                    Log.i(TAG, "Restart advertising (attempt " + nextAttempt + "): " + started);
                    if (!started && nextAttempt < RESTART_ADVERTISE_MAX_RETRIES) {
                        restartAdvertisingWithRetry(nextAttempt);
                    } else if (!started) {
                        Log.e(
                                TAG,
                                "Failed to restart advertising after "
                                        + RESTART_ADVERTISE_MAX_RETRIES
                                        + " retries");
                    }
                },
                RESTART_ADVERTISE_DELAY_MS);
    }

    @SuppressLint("MissingPermission")
    private boolean setupGattServer() {
        if (gattServer != null) {
            return true;
        }
        if (bluetoothManager == null) {
            Log.e(TAG, "BluetoothManager unavailable");
            return false;
        }
        gattServer = bluetoothManager.openGattServer(context, gattServerCallback);
        if (gattServer == null) {
            Log.e(TAG, "Failed to open GATT server");
            return false;
        }

        BluetoothGattService provisioningService =
                new BluetoothGattService(SERVICE_ID, BluetoothGattService.SERVICE_TYPE_PRIMARY);
        BluetoothGattService controlService =
                new BluetoothGattService(
                        CONTROL_SERVICE_ID, BluetoothGattService.SERVICE_TYPE_PRIMARY);

        wifiSsidChar =
                new BluetoothGattCharacteristic(
                        CHAR_WIFI_SSID_ID,
                        BluetoothGattCharacteristic.PROPERTY_WRITE,
                        BluetoothGattCharacteristic.PERMISSION_WRITE);
        wifiPasswordChar =
                new BluetoothGattCharacteristic(
                        CHAR_WIFI_PASSWORD_ID,
                        BluetoothGattCharacteristic.PROPERTY_WRITE,
                        BluetoothGattCharacteristic.PERMISSION_WRITE);
        wifiStatusChar =
                new BluetoothGattCharacteristic(
                        CHAR_WIFI_STATUS_ID,
                        BluetoothGattCharacteristic.PROPERTY_WRITE
                                | BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                        BluetoothGattCharacteristic.PERMISSION_WRITE);
        wifiStatusChar.addDescriptor(createCccd());
        ipAddressChar =
                new BluetoothGattCharacteristic(
                        CHAR_IP_ADDRESS_ID,
                        BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                        0);
        ipAddressChar.addDescriptor(createCccd());
        controlCmdChar =
                new BluetoothGattCharacteristic(
                        CHAR_CONTROL_CMD_ID,
                        BluetoothGattCharacteristic.PROPERTY_WRITE,
                        BluetoothGattCharacteristic.PERMISSION_WRITE);
        cmdResponseChar =
                new BluetoothGattCharacteristic(
                        CHAR_CMD_RESPONSE_ID,
                        BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                        0);
        cmdResponseChar.addDescriptor(createCccd());
        errorMsgChar =
                new BluetoothGattCharacteristic(
                        CHAR_ERROR_MSG_ID,
                        BluetoothGattCharacteristic.PROPERTY_NOTIFY,
                        0);
        errorMsgChar.addDescriptor(createCccd());

        resetProvisioningState();

        provisioningService.addCharacteristic(wifiSsidChar);
        provisioningService.addCharacteristic(wifiPasswordChar);
        provisioningService.addCharacteristic(wifiStatusChar);
        provisioningService.addCharacteristic(ipAddressChar);
        controlService.addCharacteristic(controlCmdChar);
        controlService.addCharacteristic(cmdResponseChar);
        controlService.addCharacteristic(errorMsgChar);

        boolean provisioningAdded = gattServer.addService(provisioningService);
        boolean controlAdded = provisioningAdded && gattServer.addService(controlService);
        if (provisioningAdded && controlAdded) {
            Log.d(TAG, "GATT server setup complete with service: " + SERVICE_UUID);
        } else {
            Log.e(
                    TAG,
                    "Failed to add GATT services: provisioning="
                            + provisioningAdded
                            + " control="
                            + controlAdded);
        }
        return provisioningAdded && controlAdded;
    }

    @SuppressLint("MissingPermission")
    public void close() {
        stopAdvertising();
        mainHandler.removeCallbacksAndMessages(null);
        if (gattServer != null && connectedDevice != null) {
            gattServer.cancelConnection(connectedDevice);
        }
        connectedDevice = null;
        wifiStatusNotifyEnabled = false;
        ipAddressNotifyEnabled = false;
        cmdResponseNotifyEnabled = false;
        errorMsgNotifyEnabled = false;
        controlChannelReadyNotified = false;
        if (gattServer != null) {
            gattServer.close();
            gattServer = null;
        }
        Log.d(TAG, "BLE server closed");
    }

    public boolean isDeviceConnected() {
        return connectedDevice != null;
    }

    public void notifyIpAddress(String ipAddress) {
        currentIpAddress = ipAddress != null ? ipAddress : "";
        if (ipAddressChar == null) {
            Log.w(TAG, "Cannot update IP address: characteristic unavailable");
            return;
        }
        ipAddressChar.setValue(currentIpAddress);
        notifyCharacteristic(ipAddressChar);
    }

    public void notifyWifiStatus(int status) {
        currentWifiStatus = status;
        if (wifiStatusChar == null) {
            Log.w(TAG, "Cannot update WiFi status: characteristic unavailable");
            return;
        }
        wifiStatusChar.setValue(new byte[] {(byte) currentWifiStatus});
        notifyCharacteristic(wifiStatusChar);
    }

    public void sendCommandResponse(String response) {
        if (cmdResponseChar == null) {
            Log.w(TAG, "Cannot send command response: characteristic unavailable");
            return;
        }
        cmdResponseChar.setValue(response != null ? response : "");
        notifyCharacteristic(cmdResponseChar);
    }

    public void sendErrorMessage(String message) {
        if (errorMsgChar == null) {
            Log.w(TAG, "Cannot send error message: characteristic unavailable");
            return;
        }
        errorMsgChar.setValue(message != null ? message : "");
        notifyCharacteristic(errorMsgChar);
    }

    @SuppressLint("MissingPermission")
    public void finishProvisioningSession() {
        restartAdvertisingOnDisconnect = false;
        stopAdvertising();
        if (gattServer == null || connectedDevice == null) {
            return;
        }
        gattServer.cancelConnection(connectedDevice);
    }

    public void setOnConnectionStateChangeListener(OnConnectionStateChangeListener listener) {
        this.connectionStateChangeListener = listener;
    }

    public void setOnWifiProvisionRequestListener(OnWifiProvisionRequestListener listener) {
        this.wifiProvisionRequestListener = listener;
    }

    public void setOnControlCommandListener(OnControlCommandListener listener) {
        this.controlCommandListener = listener;
    }

    public void setOnControlChannelReadyListener(OnControlChannelReadyListener listener) {
        this.controlChannelReadyListener = listener;
    }

    @SuppressLint("MissingPermission")
    public boolean startAdvertising() {
        if (isAdvertising) {
            Log.d(TAG, "Already advertising, skipping");
            return true;
        }
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            Log.e(TAG, "Bluetooth is not available or not enabled");
            return false;
        }
        advertiser = bluetoothAdapter.getBluetoothLeAdvertiser();
        if (advertiser == null) {
            Log.e(TAG, "BLE advertising is not supported on this device");
            return false;
        }
        if (!setupGattServer()) {
            Log.e(TAG, "Failed to setup GATT server");
            return false;
        }

        restartAdvertisingOnDisconnect = true;
        resetProvisioningState();

        AdvertiseSettings settings =
                new AdvertiseSettings.Builder()
                        .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
                        .setConnectable(true)
                        .setTimeout(0)
                        .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
                        .build();

        AdvertiseData data =
                new AdvertiseData.Builder()
                        .setIncludeDeviceName(true)
                        .addServiceUuid(new ParcelUuid(SERVICE_ID))
                        .build();

        AdvertiseData scanResponse =
                new AdvertiseData.Builder()
                        .setIncludeDeviceName(true)
                        .addManufacturerData(
                                MANUFACTURER_ID,
                                getDeviceSerial().getBytes(StandardCharsets.UTF_8))
                        .build();

        advertiser.startAdvertising(settings, data, scanResponse, advertiseCallback);
        return true;
    }

    @SuppressLint("MissingPermission")
    public void stopAdvertising() {
        if (!isAdvertising || advertiser == null) {
            return;
        }
        advertiser.stopAdvertising(advertiseCallback);
        isAdvertising = false;
        Log.d(TAG, "BLE advertising stopped");
    }
}
