package com.ssnwt.helloxr.ble;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;

import com.ssnwt.vr.androidmanager.AndroidInterface;
import com.ssnwt.vr.androidmanager.wifi.WifiInfo;
import com.ssnwt.vr.androidmanager.wifi.WifiUtils;

import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class WifiConnector {
    private static final long CONNECT_TIMEOUT_MS = 30000L;
    private static final long DISCONNECT_SETTLE_DELAY_MS = 1000L;
    private static final long PLATFORM_SCAN_RESULT_DELAY_MS = 2500L;
    private static final long PLATFORM_SCAN_RETRY_DELAY_MS = 1000L;
    private static final int PLATFORM_SCAN_RETRY_LIMIT = 4;
    private static final long CANDIDATE_ATTEMPT_TIMEOUT_MS = 5000L;
    private static final String TAG = "WifiConnector";
    private final Context context;

    private static final class WifiCandidate {
        final String ssid;
        final String bssid;
        final String capabilities;
        final int rssi;
        final int frequency;

        WifiCandidate(String ssid, String bssid, String capabilities, int rssi, int frequency) {
            this.ssid = ssid;
            this.bssid = bssid;
            this.capabilities = capabilities;
            this.rssi = rssi;
            this.frequency = frequency;
        }

        @Override
        public String toString() {
            return "SSID="
                    + ssid
                    + " BSSID="
                    + bssid
                    + " RSSI="
                    + rssi
                    + " frequency="
                    + frequency
                    + " capabilities="
                    + capabilities;
        }
    }

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
    private Runnable candidateTimeoutRunnable;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean isConnecting = false;
    private ArrayList<WifiInfo> cachedScanResults = new ArrayList<>();
    private final ArrayList<WifiCandidate> connectionCandidates = new ArrayList<>();
    private int candidateIndex;
    private int scanResultRetryCount;
    private int connectionAttemptId;
    private boolean listenerRegistered = false;

    public WifiConnector(Context context) {
        this.context = context.getApplicationContext();
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

        isConnecting = true;
        connectionCandidates.clear();
        candidateIndex = 0;
        scanResultRetryCount = 0;
        connectionAttemptId++;
        removeCandidateTimeout();
        targetSsid = ssid;
        pendingPassword = password;
        pendingListener = listener;

        if (listener != null) {
            listener.onWifiConnecting();
        }

        String connectedSsid = getConnectedSsid();
        Log.i(
                TAG,
                "WiFi provisioning requested: current SSID="
                        + connectedSsid
                        + ", target SSID="
                        + ssid
                        + "; disconnecting current STA first");
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
        cachedScanResults.clear();
        try {
            svrWifiUtils.disconnectWifi();
            Log.i(TAG, "Current STA WiFi disconnect requested");
        } catch (Exception e) {
            Log.w(TAG, "Failed to disconnect current STA WiFi, continuing with new connection", e);
        }
        mainHandler.postDelayed(this::startFreshScan, DISCONNECT_SETTLE_DELAY_MS);
    }

    public void disconnect() {
        Runnable runnable = timeoutRunnable;
        if (runnable != null) {
            mainHandler.removeCallbacks(runnable);
            timeoutRunnable = null;
        }
        isConnecting = false;
        connectionAttemptId++;
        targetSsid = null;
        pendingPassword = null;
        pendingListener = null;
        connectionCandidates.clear();
        removeCandidateTimeout();
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

    private void startFreshScan() {
        if (!isConnecting) {
            return;
        }
        Log.d(TAG, "Starting fresh WiFi scan for requested SSID: " + targetSsid);
        try {
            boolean started = svrWifiUtils.searchWifi();
            Log.d(TAG, "Fresh WiFi scan requested: " + started);
        } catch (Exception e) {
            Log.w(TAG, "Fresh WiFi scan request failed", e);
        }
        mainHandler.postDelayed(this::loadPlatformScanCandidates, PLATFORM_SCAN_RESULT_DELAY_MS);
    }

    /**
     * The vendor API collapses multiple APs with the same SSID into one WifiInfo. Read the
     * platform scan list instead so every BSSID remains available for retry.
     */
    private void loadPlatformScanCandidates() {
        if (!isConnecting || TextUtils.isEmpty(targetSsid)) {
            return;
        }

        ArrayList<WifiCandidate> candidates = collectPlatformCandidates();
        if (candidates.isEmpty()) {
            scanResultRetryCount++;
            if (scanResultRetryCount <= PLATFORM_SCAN_RETRY_LIMIT) {
                Log.d(
                        TAG,
                        "Target SSID not found in platform scan results, retry "
                                + scanResultRetryCount
                                + "/"
                                + PLATFORM_SCAN_RETRY_LIMIT
                                + ": "
                                + targetSsid);
                mainHandler.postDelayed(
                        this::loadPlatformScanCandidates, PLATFORM_SCAN_RETRY_DELAY_MS);
                return;
            }

            ArrayList<WifiCandidate> vendorCandidates = collectVendorCandidates();
            if (!vendorCandidates.isEmpty()) {
                Log.w(
                        TAG,
                        "Platform scan did not expose candidates; using vendor scan fallback: "
                                + vendorCandidates.size());
                connectionCandidates.clear();
                connectionCandidates.addAll(vendorCandidates);
                candidateIndex = 0;
                tryNextCandidate();
                return;
            }

            Log.w(TAG, "No BSSID candidates found; using direct SDK WiFi connection fallback");
            startDirectConnectionFallback();
            return;
        }

        connectionCandidates.clear();
        connectionCandidates.addAll(candidates);
        candidateIndex = 0;
        Log.i(
                TAG,
                "Platform scan found "
                        + connectionCandidates.size()
                        + " BSSID candidates for SSID="
                        + targetSsid);
        for (int i = 0; i < connectionCandidates.size(); i++) {
            Log.i(TAG, "WiFi candidate[" + i + "]: " + connectionCandidates.get(i));
        }
        tryNextCandidate();
    }

    private ArrayList<WifiCandidate> collectPlatformCandidates() {
        ArrayList<WifiCandidate> candidates = new ArrayList<>();
        try {
            WifiManager wifiManager =
                    (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
            if (wifiManager == null) {
                return candidates;
            }
            List<ScanResult> scanResults = wifiManager.getScanResults();
            if (scanResults == null) {
                return candidates;
            }
            Set<String> seenBssids = new HashSet<>();
            for (ScanResult scanResult : scanResults) {
                if (scanResult == null
                        || !ssidMatches(targetSsid, scanResult.SSID)
                        || TextUtils.isEmpty(scanResult.BSSID)
                        || !seenBssids.add(scanResult.BSSID)) {
                    continue;
                }
                candidates.add(
                        new WifiCandidate(
                                targetSsid,
                                scanResult.BSSID,
                                scanResult.capabilities != null ? scanResult.capabilities : "",
                                scanResult.level,
                                scanResult.frequency));
            }
            Collections.sort(
                    candidates,
                    Comparator.comparingInt((WifiCandidate candidate) -> candidate.rssi)
                            .reversed());
        } catch (SecurityException e) {
            Log.w(TAG, "Cannot read platform WiFi scan results", e);
        } catch (Exception e) {
            Log.w(TAG, "Failed to collect platform WiFi scan results", e);
        }
        return candidates;
    }

    private ArrayList<WifiCandidate> collectVendorCandidates() {
        ArrayList<WifiCandidate> candidates = new ArrayList<>();
        if (cachedScanResults == null) {
            return candidates;
        }
        Set<String> seenBssids = new HashSet<>();
        for (WifiInfo wifiInfo : cachedScanResults) {
            if (wifiInfo == null
                    || !ssidMatches(targetSsid, wifiInfo.getSSID())
                    || TextUtils.isEmpty(wifiInfo.getBSSID())
                    || !seenBssids.add(wifiInfo.getBSSID())) {
                continue;
            }
            candidates.add(
                    new WifiCandidate(
                            targetSsid,
                            wifiInfo.getBSSID(),
                            wifiInfo.getCapabilities(),
                            wifiInfo.getRssi(),
                            wifiInfo.getFrequency()));
        }
        Collections.sort(
                candidates,
                Comparator.comparingInt((WifiCandidate candidate) -> candidate.rssi).reversed());
        return candidates;
    }

    private boolean ssidMatches(String requestedSsid, String scannedSsid) {
        return !TextUtils.isEmpty(requestedSsid)
                && (requestedSsid.equals(scannedSsid)
                        || ("\"" + requestedSsid + "\"").equals(scannedSsid));
    }

    private void tryNextCandidate() {
        if (!isConnecting) {
            return;
        }
        removeCandidateTimeout();
        if (candidateIndex >= connectionCandidates.size()) {
            notifyFailed("All WiFi BSSID candidates failed", pendingListener);
            return;
        }

        WifiCandidate candidate = connectionCandidates.get(candidateIndex++);
        int attemptId = ++connectionAttemptId;
        Log.i(
                TAG,
                "Connecting WiFi candidate "
                        + candidateIndex
                        + "/"
                        + connectionCandidates.size()
                        + ": "
                        + candidate);
        try {
            int result =
                    svrWifiUtils.connectWifi(
                            candidate.ssid,
                            candidate.bssid,
                            candidate.capabilities,
                            pendingPassword);
            Log.d(TAG, "WifiUtils candidate connection request result=" + result);
        } catch (Exception e) {
            Log.w(TAG, "WiFi candidate connection request failed: " + candidate.bssid, e);
            mainHandler.postDelayed(this::tryNextCandidate, 250L);
            return;
        }

        candidateTimeoutRunnable =
                () -> {
                    if (isConnecting && attemptId == connectionAttemptId) {
                        Log.w(
                                TAG,
                                "WiFi candidate timed out, trying next BSSID: "
                                        + candidate.bssid);
                        tryNextCandidate();
                    }
                };
        mainHandler.postDelayed(candidateTimeoutRunnable, CANDIDATE_ATTEMPT_TIMEOUT_MS);
        mainHandler.postDelayed(
                () -> pollConnectionStatus(pendingListener, attemptId), 1500L);
    }

    private void startDirectConnectionFallback() {
        if (!isConnecting || TextUtils.isEmpty(targetSsid)) {
            return;
        }
        int attemptId = ++connectionAttemptId;
        try {
            Log.i(
                    TAG,
                    "Connecting directly via WifiUtils because no scan candidates were available: SSID="
                            + targetSsid);
            WifiUtils.connectWifi(context, targetSsid, pendingPassword);
            mainHandler.postDelayed(
                    () -> pollConnectionStatus(pendingListener, attemptId), 1500L);
        } catch (Exception e) {
            Log.e(TAG, "Direct WiFi connection request failed", e);
            notifyFailed("WiFi connection request failed", pendingListener);
        }
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
                    }
                });
    }

    private void notifyConnected(String ipAddress, OnWifiConnectListener listener) {
        isConnecting = false;
        connectionAttemptId++;
        connectionCandidates.clear();
        removeCandidateTimeout();
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
        connectionAttemptId++;
        connectionCandidates.clear();
        removeCandidateTimeout();
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

    private void removeCandidateTimeout() {
        if (candidateTimeoutRunnable != null) {
            mainHandler.removeCallbacks(candidateTimeoutRunnable);
            candidateTimeoutRunnable = null;
        }
    }

    private void pollConnectionStatus(OnWifiConnectListener listener, int attemptId) {
        if (!isConnecting || attemptId != connectionAttemptId) {
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
                mainHandler.postDelayed(
                        () -> pollConnectionStatus(listener, attemptId), 1000L);
                return;
            }
            Log.i(TAG, "WiFi connected, IP: " + ipAddress);
            notifyConnected(ipAddress, listener);
            return;
        }
        mainHandler.postDelayed(() -> pollConnectionStatus(listener, attemptId), 1000L);
    }

    private String queryIpAddress() {
        return queryStaIpAddress(context);
    }

    public static String queryStaIpAddress(Context context) {
        if (context == null) {
            return "";
        }
        try {
            ConnectivityManager connectivityManager =
                    (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (connectivityManager == null) {
                return "";
            }
            for (Network network : connectivityManager.getAllNetworks()) {
                NetworkCapabilities capabilities =
                        connectivityManager.getNetworkCapabilities(network);
                if (capabilities == null
                        || !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                    continue;
                }
                LinkProperties properties = connectivityManager.getLinkProperties(network);
                if (properties == null) {
                    continue;
                }
                // wlan1 is the persistent BOARD hotspot. Only report the STA address so
                // provisioning never returns the hotspot IP as the newly connected WiFi IP.
                if ("wlan1".equals(properties.getInterfaceName())) {
                    continue;
                }
                for (LinkAddress linkAddress : properties.getLinkAddresses()) {
                    InetAddress address = linkAddress.getAddress();
                    if (address != null
                            && address.getAddress().length == 4
                            && !address.isAnyLocalAddress()
                            && !address.isLoopbackAddress()) {
                        return address.getHostAddress();
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting IP address: " + e.getMessage());
        }
        return "";
    }
}
