package com.ssnwt.helloxr.ble;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;

import com.ssnwt.vr.androidmanager.AndroidInterface;
import com.ssnwt.vr.androidmanager.wifi.WifiInfo;
import com.ssnwt.vr.androidmanager.wifi.WifiUtils;

import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Enumeration;

public class WifiConnector {
    private static final long CONNECT_TIMEOUT_MS = 30000L;
    private static final String TAG = "WifiConnector";

    public interface OnWifiConnectListener {
        void onWifiConnected(String ipAddress);

        void onWifiConnecting();

        void onWifiFailed(String message);
    }

    private OnWifiConnectListener pendingListener;
    private String pendingPassword;
    private WifiUtils svrWifiUtils;
    private String targetSsid;
    private Runnable timeoutRunnable;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean isConnecting = false;
    private ArrayList<WifiInfo> cachedScanResults = new ArrayList<>();
    private boolean listenerRegistered = false;

    public WifiConnector(Context context) {
        ensureWifiUtils();
    }

    public void connectWifi(String ssid, String password, OnWifiConnectListener listener) {
        ensureWifiUtils();
        if (isConnecting) {
            Log.w(TAG, "Already connecting to a network, ignoring request");
            if (listener != null) {
                listener.onWifiFailed("Already connecting to another network");
            }
            return;
        }
        if (svrWifiUtils == null) {
            Log.e(TAG, "WifiUtils not available");
            if (listener != null) {
                listener.onWifiFailed("WiFi service not available");
            }
            return;
        }

        String connectedSsid = getConnectedSsid();
        if (ssid != null
                && (ssid.equals(connectedSsid) || ("\"" + ssid + "\"").equals(connectedSsid))) {
            String ipAddress = queryIpAddress();
            Log.i(TAG, "Already connected to: " + connectedSsid + ", IP: " + ipAddress);
            if (ipAddress != null && !ipAddress.isEmpty()) {
                if (listener != null) {
                    listener.onWifiConnected(ipAddress);
                }
                return;
            }
            Log.w(TAG, "SSID matched but IP is empty, falling back to normal connect flow");
        }

        isConnecting = true;
        targetSsid = ssid;
        pendingPassword = password;
        pendingListener = listener;

        if (listener != null) {
            listener.onWifiConnecting();
        }

        Log.i(TAG, "Connecting to WiFi SSID: " + ssid);
        if (!svrWifiUtils.isOpenWifi()) {
            Log.i(TAG, "WiFi is disabled, enabling...");
            svrWifiUtils.openWifi();
        }

        timeoutRunnable =
                () -> {
                    if (isConnecting) {
                        Log.e(TAG, "WiFi connection timeout after " + CONNECT_TIMEOUT_MS + "ms");
                        notifyFailed("连接超时", listener);
                    }
                };
        mainHandler.postDelayed(timeoutRunnable, CONNECT_TIMEOUT_MS);

        WifiInfo wifiInfo = findInList(ssid, cachedScanResults);
        if (wifiInfo != null) {
            Log.d(TAG, "Found WiFi in cached scan results: " + wifiInfo);
            doConnect(wifiInfo, password, listener);
            return;
        }

        Log.d(TAG, "Target WiFi not in cached results, starting scan...");
        svrWifiUtils.searchWifi();
    }

    public void disconnect() {
        Runnable runnable = timeoutRunnable;
        if (runnable != null) {
            mainHandler.removeCallbacks(runnable);
            timeoutRunnable = null;
        }
        isConnecting = false;
        targetSsid = null;
        pendingPassword = null;
        pendingListener = null;
    }

    public String getConnectedSsid() {
        try {
            WifiInfo connectedWifi = svrWifiUtils.getConnectedWifi2();
            if (connectedWifi != null) {
                String ssid = connectedWifi.getSSID();
                return ssid != null ? ssid : "";
            }
        } catch (Exception e) {
            Log.w(TAG, "Error getting connected SSID: " + e.getMessage());
        }
        return "";
    }

    public String getIpAddress() {
        return queryIpAddress();
    }

    public boolean isConnecting() {
        return isConnecting;
    }

    private void doConnect(WifiInfo wifiInfo, String password, OnWifiConnectListener listener) {
        String ssid = wifiInfo.getSSID();
        String bssid = wifiInfo.getBSSID();
        String capabilities = wifiInfo.getCapabilities();
        Log.i(
                TAG,
                "Connecting via WifiUtils: SSID="
                        + ssid
                        + " BSSID="
                        + bssid
                        + " capabilities="
                        + capabilities);
        svrWifiUtils.connectWifi(ssid, bssid, capabilities, password);
        mainHandler.postDelayed(() -> pollConnectionStatus(listener), 2000L);
    }

    private void ensureWifiUtils() {
        if (svrWifiUtils == null) {
            try {
                svrWifiUtils = AndroidInterface.getInstance().getWifiUtils();
            } catch (Exception e) {
                Log.w(TAG, "Failed to get WifiUtils: " + e.getMessage());
            }
        }
        if (svrWifiUtils == null || listenerRegistered) {
            return;
        }
        listenerRegistered = true;
        svrWifiUtils.setListener2(
                new WifiUtils.WifiListener2() {
                    @Override
                    public void onConnecting(int state, String ssid) {
                        Log.d(TAG, "WiFi connecting: state=" + state + " ssid=" + ssid);
                    }

                    @Override
                    public void onOpened(boolean opened) {
                        Log.d(TAG, "WiFi opened: " + opened);
                    }

                    @Override
                    public void onRssiLevelChanegd(int level) {
                    }

                    @Override
                    public void onSearchResult(ArrayList<WifiInfo> results) {
                        Log.d(TAG, "Scan results: " + (results != null ? results.size() : 0));
                        if (results != null) {
                            cachedScanResults = results;
                        }
                        if (!isConnecting
                                || targetSsid == null
                                || results == null
                                || results.isEmpty()) {
                            return;
                        }
                        WifiInfo wifiInfo = findInList(targetSsid, results);
                        if (wifiInfo == null) {
                            Log.d(
                                    TAG,
                                    "Target WiFi not in this scan batch, waiting for more results: "
                                            + targetSsid);
                            return;
                        }
                        Log.d(TAG, "Found WiFi after scan: " + wifiInfo);
                        doConnect(wifiInfo, pendingPassword, pendingListener);
                    }
                });
    }

    private WifiInfo findInList(String ssid, ArrayList<WifiInfo> wifiList) {
        if (!TextUtils.isEmpty(ssid) && wifiList != null) {
            for (WifiInfo wifiInfo : wifiList) {
                if (ssid.equalsIgnoreCase(wifiInfo.getSSID())) {
                    return wifiInfo;
                }
            }
        }
        return null;
    }

    private void notifyConnected(String ipAddress, OnWifiConnectListener listener) {
        isConnecting = false;
        Runnable runnable = timeoutRunnable;
        if (runnable != null) {
            mainHandler.removeCallbacks(runnable);
            timeoutRunnable = null;
        }
        if (listener != null) {
            mainHandler.post(() -> listener.onWifiConnected(ipAddress));
        }
    }

    private void notifyFailed(String message, OnWifiConnectListener listener) {
        isConnecting = false;
        Runnable runnable = timeoutRunnable;
        if (runnable != null) {
            mainHandler.removeCallbacks(runnable);
            timeoutRunnable = null;
        }
        Log.e(TAG, "WiFi connection failed: " + message);
        if (listener != null) {
            mainHandler.post(() -> listener.onWifiFailed(message));
        }
    }

    private void pollConnectionStatus(OnWifiConnectListener listener) {
        if (!isConnecting) {
            return;
        }
        String connectedSsid = getConnectedSsid();
        Log.d(
                TAG,
                "Polling connection status, connected SSID: "
                        + connectedSsid
                        + ", target: "
                        + targetSsid);
        if (targetSsid != null
                && (targetSsid.equals(connectedSsid)
                        || ("\"" + targetSsid + "\"").equals(connectedSsid))) {
            String ipAddress = queryIpAddress();
            if (ipAddress == null || ipAddress.isEmpty()) {
                Log.d(TAG, "SSID matched but IP not ready yet, continuing to poll");
                mainHandler.postDelayed(() -> pollConnectionStatus(listener), 2000L);
                return;
            }
            Log.i(TAG, "WiFi connected, IP: " + ipAddress);
            notifyConnected(ipAddress, listener);
            return;
        }
        mainHandler.postDelayed(() -> pollConnectionStatus(listener), 2000L);
    }

    private String queryIpAddress() {
        try {
            Enumeration<NetworkInterface> networkInterfaces = NetworkInterface.getNetworkInterfaces();
            if (networkInterfaces == null) {
                return "";
            }
            while (networkInterfaces.hasMoreElements()) {
                NetworkInterface networkInterface = networkInterfaces.nextElement();
                if (networkInterface.isUp() && !networkInterface.isLoopback()) {
                    Enumeration<InetAddress> inetAddresses = networkInterface.getInetAddresses();
                    while (inetAddresses.hasMoreElements()) {
                        InetAddress inetAddress = inetAddresses.nextElement();
                        if (!inetAddress.isLoopbackAddress()
                                && inetAddress.getAddress().length == 4) {
                            return inetAddress.getHostAddress();
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting IP address: " + e.getMessage());
        }
        return "";
    }
}
