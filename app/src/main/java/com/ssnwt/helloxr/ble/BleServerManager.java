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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
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

    public interface OnConnectionStateChangeListener {
        void onConnectionStateChanged(boolean connected);
    }

    public interface OnWifiProvisionRequestListener {
        void onWifiProvisionRequested(String ssid, String password);
    }

    public interface OnControlCommandListener {
        void onControlCommand(BluetoothDevice device, String command);
    }

    public interface OnControlChannelReadyListener {
        void onControlChannelReady(BluetoothDevice device);
    }

    public interface OnDeviceDisconnectedListener {
        void onDeviceDisconnected(BluetoothDevice device);
    }

    private final Context context;
    private final BluetoothManager bluetoothManager;
    private final BluetoothAdapter bluetoothAdapter;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private BluetoothLeAdvertiser advertiser;
    private BluetoothGattServer gattServer;
    private BluetoothGattService provisioningService;
    private BluetoothGattService controlService;
    private final Set<BluetoothDevice> connectedDevices = new HashSet<>();
    private final Map<UUID, Set<BluetoothDevice>> notificationSubscribers = new HashMap<>();
    private final Set<BluetoothDevice> controlChannelReadyDevices = new HashSet<>();
    private BluetoothDevice provisioningDevice;
    private BluetoothDevice lastControlDevice;

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
    private OnDeviceDisconnectedListener deviceDisconnectedListener;

    private String pendingSsid;
    private String pendingPassword;
    private String currentIpAddress = "";
    private int currentWifiStatus = WIFI_STATUS_IDLE;
    private boolean isAdvertising = false;
    private boolean gattServicesReady = false;
    private boolean advertisingStartPending = false;
    private byte[] pendingManufacturerPayload;

    private final AdvertiseCallback advertiseCallback = new AdvertiseCallback() {
        @Override
        public void onStartSuccess(AdvertiseSettings settingsInEffect) {
            synchronized (BleServerManager.this) {
                advertisingStartPending = false;
                isAdvertising = true;
            }
            Log.i(TAG, "BLE advertising started");
        }

        @Override
        public void onStartFailure(int errorCode) {
            synchronized (BleServerManager.this) {
                advertisingStartPending = false;
                isAdvertising = false;
            }
            Log.e(TAG, "BLE advertising failed: " + errorCode);
            mainHandler.postDelayed(BleServerManager.this::startAdvertising, 1000L);
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
                        boolean wasEmpty;
                        synchronized (BleServerManager.this) {
                            wasEmpty = connectedDevices.isEmpty();
                            connectedDevices.add(device);
                        }
                        Log.i(TAG, "BLE client connected; total=" + getConnectedDeviceCount());
                        if (wasEmpty && connectionStateChangeListener != null) {
                            connectionStateChangeListener.onConnectionStateChanged(true);
                        }
                        return;
                    }

                    if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                        boolean isNowEmpty;
                        boolean resetProvisioning;
                        synchronized (BleServerManager.this) {
                            connectedDevices.remove(device);
                            removeNotificationSubscriptions(device);
                            controlChannelReadyDevices.remove(device);
                            resetProvisioning = device.equals(provisioningDevice);
                            if (resetProvisioning) {
                                provisioningDevice = null;
                            }
                            if (device.equals(lastControlDevice)) {
                                lastControlDevice = null;
                            }
                            isNowEmpty = connectedDevices.isEmpty();
                        }
                        if (resetProvisioning) {
                            resetProvisioningState();
                        }
                        Log.i(TAG, "BLE client disconnected; total=" + getConnectedDeviceCount());
                        if (deviceDisconnectedListener != null) {
                            deviceDisconnectedListener.onDeviceDisconnected(device);
                        }
                        if (isNowEmpty && connectionStateChangeListener != null) {
                            connectionStateChangeListener.onConnectionStateChanged(false);
                        }
                    }
                }

                @Override
                public void onServiceAdded(int status, BluetoothGattService service) {
                    handleServiceAdded(status, service);
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
                    logCharacteristicWrite(
                            device,
                            requestId,
                            characteristic,
                            preparedWrite,
                            responseNeeded,
                            offset,
                            value);
                    int responseStatus = BluetoothGatt.GATT_SUCCESS;
                    if (preparedWrite || offset != 0) {
                        responseStatus = BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                    } else if (value == null) {
                        responseStatus = BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
                    } else {
                        responseStatus = handleCharacteristicWrite(device, characteristic, value);
                    }

                    if (responseNeeded && gattServer != null) {
                        gattServer.sendResponse(device, requestId, responseStatus, 0, null);
                    }
                    Log.i(
                            TAG,
                            "GATT write handled: address="
                                    + device.getAddress()
                                    + " uuid="
                                    + characteristic.getUuid()
                                    + " status="
                                    + responseStatus);
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
                    boolean enabled =
                            descriptor != null
                                    && descriptor.getCharacteristic() != null
                                    && isSubscribed(
                                            descriptor.getCharacteristic().getUuid(), device);
                    byte[] value =
                            enabled
                                    ? BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                    : BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE;
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
                    Log.i(
                            TAG,
                            "GATT descriptor write received: address="
                                    + device.getAddress()
                                    + " requestId="
                                    + requestId
                                    + " uuid="
                                    + (descriptor == null ? null : descriptor.getUuid())
                                    + " characteristic="
                                    + (descriptor == null || descriptor.getCharacteristic() == null
                                            ? null
                                            : descriptor.getCharacteristic().getUuid())
                                    + " prepared="
                                    + preparedWrite
                                    + " offset="
                                    + offset
                                    + " length="
                                    + (value == null ? 0 : value.length));
                    int responseStatus = BluetoothGatt.GATT_SUCCESS;
                    if (preparedWrite || offset != 0) {
                        responseStatus = BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
                    } else if (descriptor == null || value == null) {
                        responseStatus = BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
                    } else {
                        handleDescriptorWrite(device, descriptor, value);
                    }

                    if (responseNeeded && gattServer != null) {
                        gattServer.sendResponse(device, requestId, responseStatus, 0, null);
                    }
                    Log.i(
                            TAG,
                            "GATT descriptor write handled: address="
                                    + device.getAddress()
                                    + " status="
                                    + responseStatus);
                }

                @Override
                public void onNotificationSent(BluetoothDevice device, int status) {
                    if (status != BluetoothGatt.GATT_SUCCESS) {
                        Log.w(
                                TAG,
                                "Notification send failed: address="
                                        + device.getAddress()
                                        + " status="
                                        + status);
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
            if (!AndroidInterface.getInstance().isInitialized()) {
                Log.w(TAG, "SVR AndroidInterface is not initialized; BLE advertising is deferred");
                return null;
            }
            String serial = AndroidInterface.getInstance().getDeviceUtils().getSerialNumber();
            if (serial == null || serial.isEmpty()) {
                Log.e(TAG, "SVR device serial is unavailable; BLE advertising is deferred");
                return null;
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
            return null;
        }
    }

    private void logCharacteristicWrite(
            BluetoothDevice device,
            int requestId,
            BluetoothGattCharacteristic characteristic,
            boolean preparedWrite,
            boolean responseNeeded,
            int offset,
            byte[] value) {
        UUID uuid = characteristic != null ? characteristic.getUuid() : null;
        String payload;
        if (CHAR_WIFI_PASSWORD_ID.equals(uuid)) {
            payload = "<redacted>";
        } else if (value == null) {
            payload = "<null>";
        } else if (CHAR_WIFI_STATUS_ID.equals(uuid)) {
            payload = bytesToHex(value);
        } else {
            payload = new String(value, StandardCharsets.UTF_8);
        }
        Log.i(
                TAG,
                "GATT write received: address="
                        + device.getAddress()
                        + " requestId="
                        + requestId
                        + " uuid="
                        + uuid
                        + " prepared="
                        + preparedWrite
                        + " responseNeeded="
                        + responseNeeded
                        + " offset="
                        + offset
                        + " length="
                        + (value == null ? 0 : value.length)
                        + " payload="
                        + payload);
    }

    private String bytesToHex(byte[] value) {
        StringBuilder builder = new StringBuilder(value.length * 2);
        for (byte item : value) {
            builder.append(String.format("%02X", item & 0xFF));
        }
        return builder.toString();
    }

    private synchronized boolean claimProvisioningDevice(BluetoothDevice device) {
        if (provisioningDevice == null) {
            provisioningDevice = device;
            Log.i(TAG, "Provisioning client selected: address=" + device.getAddress());
            return true;
        }
        if (provisioningDevice.equals(device)) {
            return true;
        }
        Log.w(
                TAG,
                "Rejecting provisioning write from address="
                        + device.getAddress()
                        + "; activeAddress="
                        + provisioningDevice.getAddress());
        return false;
    }

    private int handleCharacteristicWrite(
            BluetoothDevice device, BluetoothGattCharacteristic characteristic, byte[] value) {
        UUID uuid = characteristic.getUuid();
        if (CHAR_WIFI_SSID_ID.equals(uuid)) {
            if (value.length > MAX_WIFI_SSID_BYTES) {
                Log.w(TAG, "Rejecting FFE1 write larger than " + MAX_WIFI_SSID_BYTES + " bytes");
                return BluetoothGatt.GATT_INVALID_ATTRIBUTE_LENGTH;
            }
            if (!claimProvisioningDevice(device)) {
                return BluetoothGatt.GATT_FAILURE;
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
            if (!claimProvisioningDevice(device)) {
                return BluetoothGatt.GATT_FAILURE;
            }
            pendingPassword = new String(value, StandardCharsets.UTF_8);
            Log.d(TAG, "WiFi password received");
            return BluetoothGatt.GATT_SUCCESS;
        }
        if (CHAR_WIFI_STATUS_ID.equals(uuid)) {
            return handleWifiStatusWrite(device, value);
        }
        if (CHAR_CONTROL_CMD_ID.equals(uuid)) {
            String command = new String(value, StandardCharsets.UTF_8);
            synchronized (this) {
                lastControlDevice = device;
            }
            Log.i(
                    TAG,
                    "Control command received: address="
                            + device.getAddress()
                            + " payload="
                            + command);
            if (controlCommandListener != null) {
                controlCommandListener.onControlCommand(device, command);
            }
            return BluetoothGatt.GATT_SUCCESS;
        }
        Log.w(TAG, "Unsupported write characteristic: " + uuid);
        return BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED;
    }

    private int handleWifiStatusWrite(BluetoothDevice device, byte[] value) {
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
        if (!claimProvisioningDevice(device)) {
            return BluetoothGatt.GATT_FAILURE;
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

    private void handleDescriptorWrite(
            BluetoothDevice device, BluetoothGattDescriptor descriptor, byte[] value) {
        if (descriptor == null || descriptor.getCharacteristic() == null) {
            return;
        }
        UUID uuid = descriptor.getCharacteristic().getUuid();
        boolean enabled = isNotificationEnabled(value);
        setNotificationSubscription(uuid, device, enabled);
        Log.i(
                TAG,
                "CCCD updated: address="
                        + device.getAddress()
                        + " uuid="
                        + uuid
                        + " enabled="
                        + enabled);
        if (CHAR_WIFI_STATUS_ID.equals(uuid)) {
            if (enabled) {
                notifyCharacteristic(device, wifiStatusChar);
            }
            return;
        }
        if (CHAR_IP_ADDRESS_ID.equals(uuid)) {
            if (enabled && currentIpAddress != null && !currentIpAddress.isEmpty()) {
                notifyCharacteristic(device, ipAddressChar);
            }
            return;
        }
        if (CHAR_CMD_RESPONSE_ID.equals(uuid)) {
            updateControlChannelReadyState(device);
            return;
        }
        if (CHAR_ERROR_MSG_ID.equals(uuid)) {
            updateControlChannelReadyState(device);
        }
    }

    private synchronized void setNotificationSubscription(
            UUID uuid, BluetoothDevice device, boolean enabled) {
        Set<BluetoothDevice> subscribers = notificationSubscribers.get(uuid);
        if (enabled) {
            if (subscribers == null) {
                subscribers = new HashSet<>();
                notificationSubscribers.put(uuid, subscribers);
            }
            subscribers.add(device);
        } else if (subscribers != null) {
            subscribers.remove(device);
            if (subscribers.isEmpty()) {
                notificationSubscribers.remove(uuid);
            }
        }
    }

    private synchronized boolean isSubscribed(UUID uuid, BluetoothDevice device) {
        Set<BluetoothDevice> subscribers = notificationSubscribers.get(uuid);
        return subscribers != null && subscribers.contains(device);
    }

    private synchronized void removeNotificationSubscriptions(BluetoothDevice device) {
        for (Set<BluetoothDevice> subscribers : notificationSubscribers.values()) {
            subscribers.remove(device);
        }
        notificationSubscribers.entrySet().removeIf(entry -> entry.getValue().isEmpty());
    }

    private void updateControlChannelReadyState(BluetoothDevice device) {
        boolean isReady =
                isSubscribed(CHAR_CMD_RESPONSE_ID, device)
                        && isSubscribed(CHAR_ERROR_MSG_ID, device);
        if (!isReady) {
            synchronized (this) {
                controlChannelReadyDevices.remove(device);
            }
            return;
        }
        synchronized (this) {
            if (controlChannelReadyDevices.contains(device) || controlChannelReadyListener == null) {
                return;
            }
            controlChannelReadyDevices.add(device);
        }
        Log.i(TAG, "Control channel ready: address=" + device.getAddress());
        controlChannelReadyListener.onControlChannelReady(device);
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
        if (wifiStatusChar != null) {
            wifiStatusChar.setValue(new byte[] {(byte) currentWifiStatus});
        }
        if (ipAddressChar != null) {
            ipAddressChar.setValue(currentIpAddress);
        }
        // Control characteristic values belong to independent clients and are not
        // provisioning state. Keep them untouched when the phone disconnects.
    }

    @SuppressLint("MissingPermission")
    private void notifyCharacteristic(BluetoothGattCharacteristic characteristic) {
        if (gattServer == null || characteristic == null) {
            return;
        }
        ArrayList<BluetoothDevice> subscribers;
        synchronized (this) {
            Set<BluetoothDevice> devices = notificationSubscribers.get(characteristic.getUuid());
            if (devices == null || devices.isEmpty()) {
                return;
            }
            subscribers = new ArrayList<>(devices);
        }
        for (BluetoothDevice device : subscribers) {
            notifyCharacteristic(device, characteristic);
        }
    }

    @SuppressLint("MissingPermission")
    private void notifyCharacteristic(
            BluetoothDevice device, BluetoothGattCharacteristic characteristic) {
        if (gattServer == null
                || device == null
                || characteristic == null
                || !isSubscribed(characteristic.getUuid(), device)) {
            return;
        }
        boolean queued = gattServer.notifyCharacteristicChanged(device, characteristic, false);
        Log.d(
                TAG,
                "Notification queued: address="
                        + device.getAddress()
                        + " uuid="
                        + characteristic.getUuid()
                        + " queued="
                        + queued);
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

        provisioningService =
                new BluetoothGattService(SERVICE_ID, BluetoothGattService.SERVICE_TYPE_PRIMARY);
        controlService =
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

        boolean provisioningAddRequested = gattServer.addService(provisioningService);
        if (!provisioningAddRequested) {
            Log.e(TAG, "Failed to request provisioning GATT service addition");
        }
        return provisioningAddRequested;
    }

    @SuppressLint("MissingPermission")
    private void handleServiceAdded(int status, BluetoothGattService service) {
        if (service == null) {
            Log.e(TAG, "GATT service callback returned no service");
            return;
        }
        UUID uuid = service.getUuid();
        Log.i(TAG, "GATT service added: uuid=" + uuid + " status=" + status);
        if (status != BluetoothGatt.GATT_SUCCESS || gattServer == null) {
            Log.e(TAG, "GATT service initialization failed: uuid=" + uuid + " status=" + status);
            return;
        }
        if (SERVICE_ID.equals(uuid)) {
            if (!gattServer.addService(controlService)) {
                Log.e(TAG, "Failed to request control GATT service addition");
            }
            return;
        }
        if (!CONTROL_SERVICE_ID.equals(uuid)) {
            return;
        }
        synchronized (this) {
            gattServicesReady = true;
        }
        Log.i(TAG, "All GATT services ready");
        startPendingAdvertisingIfReady();
    }

    @SuppressLint("MissingPermission")
    public void close() {
        stopAdvertising();
        mainHandler.removeCallbacksAndMessages(null);
        ArrayList<BluetoothDevice> devices;
        synchronized (this) {
            devices = new ArrayList<>(connectedDevices);
        }
        if (gattServer != null) {
            for (BluetoothDevice device : devices) {
                gattServer.cancelConnection(device);
            }
        }
        synchronized (this) {
            connectedDevices.clear();
            notificationSubscribers.clear();
            controlChannelReadyDevices.clear();
            provisioningDevice = null;
            lastControlDevice = null;
            gattServicesReady = false;
            advertisingStartPending = false;
            pendingManufacturerPayload = null;
        }
        if (gattServer != null) {
            gattServer.close();
            gattServer = null;
        }
        Log.d(TAG, "BLE server closed");
    }

    public synchronized boolean isDeviceConnected() {
        return !connectedDevices.isEmpty();
    }

    public synchronized int getConnectedDeviceCount() {
        return connectedDevices.size();
    }

    public void notifyIpAddress(String ipAddress) {
        currentIpAddress = ipAddress != null ? ipAddress : "";
        if (ipAddressChar == null) {
            Log.w(TAG, "Cannot update IP address: characteristic unavailable");
            return;
        }
        ipAddressChar.setValue(currentIpAddress);
        BluetoothDevice device;
        synchronized (this) {
            device = provisioningDevice;
        }
        notifyCharacteristic(device, ipAddressChar);
    }

    public void notifyWifiStatus(int status) {
        currentWifiStatus = status;
        if (wifiStatusChar == null) {
            Log.w(TAG, "Cannot update WiFi status: characteristic unavailable");
            return;
        }
        wifiStatusChar.setValue(new byte[] {(byte) currentWifiStatus});
        BluetoothDevice device;
        synchronized (this) {
            device = provisioningDevice;
        }
        notifyCharacteristic(device, wifiStatusChar);
    }

    public void sendCommandResponse(String response) {
        BluetoothDevice device;
        synchronized (this) {
            device = lastControlDevice;
        }
        sendCommandResponse(device, response);
    }

    public void sendCommandResponse(BluetoothDevice device, String response) {
        if (cmdResponseChar == null) {
            Log.w(TAG, "Cannot send command response: characteristic unavailable");
            return;
        }
        cmdResponseChar.setValue(response != null ? response : "");
        notifyCharacteristic(device, cmdResponseChar);
    }

    public void sendErrorMessage(String message) {
        BluetoothDevice device;
        synchronized (this) {
            device = lastControlDevice;
        }
        sendErrorMessage(device, message);
    }

    public void sendErrorMessage(BluetoothDevice device, String message) {
        if (errorMsgChar == null) {
            Log.w(TAG, "Cannot send error message: characteristic unavailable");
            return;
        }
        errorMsgChar.setValue(message != null ? message : "");
        notifyCharacteristic(device, errorMsgChar);
    }

    @SuppressLint("MissingPermission")
    public void finishProvisioningSession() {
        BluetoothDevice device;
        synchronized (this) {
            device = provisioningDevice;
        }
        if (gattServer == null || device == null) {
            return;
        }
        Log.i(TAG, "Finishing provisioning client only: address=" + device.getAddress());
        gattServer.cancelConnection(device);
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

    public void setOnDeviceDisconnectedListener(OnDeviceDisconnectedListener listener) {
        this.deviceDisconnectedListener = listener;
    }

    @SuppressLint("MissingPermission")
    public synchronized boolean startAdvertising() {
        if (isAdvertising || advertisingStartPending) {
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
        String serial = getDeviceSerial();
        if (serial == null || serial.isEmpty()) {
            Log.e(TAG, "BLE advertising blocked until a valid device serial is available");
            return false;
        }
        pendingManufacturerPayload = serial.getBytes(StandardCharsets.UTF_8);
        Log.i(TAG, "BLE manufacturer payload ready: length=" + pendingManufacturerPayload.length);
        if (!setupGattServer()) {
            Log.e(TAG, "Failed to setup GATT server");
            return false;
        }
        if (gattServicesReady) {
            startPendingAdvertisingIfReady();
        } else {
            Log.i(TAG, "BLE advertising deferred until all GATT services are ready");
        }
        return true;
    }

    @SuppressLint("MissingPermission")
    private synchronized void startPendingAdvertisingIfReady() {
        if (isAdvertising || advertisingStartPending || !gattServicesReady) {
            return;
        }
        if (advertiser == null || pendingManufacturerPayload == null) {
            Log.e(TAG, "BLE advertising prerequisites are incomplete");
            return;
        }

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
                                pendingManufacturerPayload)
                        .build();

        advertisingStartPending = true;
        advertiser.startAdvertising(settings, data, scanResponse, advertiseCallback);
    }

    @SuppressLint("MissingPermission")
    public void stopAdvertising() {
        if ((!isAdvertising && !advertisingStartPending) || advertiser == null) {
            return;
        }
        advertiser.stopAdvertising(advertiseCallback);
        isAdvertising = false;
        advertisingStartPending = false;
        Log.d(TAG, "BLE advertising stopped");
    }
}
