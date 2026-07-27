package com.ssnwt.helloxr.ble;

import android.app.Application;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.ssnwt.vr.androidmanager.AndroidInterface;
import com.ssnwt.vr.androidmanager.wifi.WifiUtils;

/**
 * Keeps the device hotspot configured and enabled independently of the UI activity.
 */
final class HotspotManager {
    private static final String TAG = "HotspotManager";
    private static final String HOTSPOT_SSID = "BOARD";
    private static final String HOTSPOT_PASSWORD = "12345678";
    private static final long RETRY_DELAY_MS = 5000L;
    private static final long CHECK_INTERVAL_MS = 15000L;
    private static final long RESTART_DELAY_MS = 1000L;

    private final Application application;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Runnable ensureRunnable = this::ensureHotspot;
    private final Runnable initRetryRunnable = this::requestApiInitialization;
    private final AndroidInterface.InitListener initListener =
            new AndroidInterface.InitListener() {
                @Override
                public void onInitialized() {
                    mainHandler.post(
                            () -> {
                                apiInitializationRequested = false;
                                if (running) {
                                    ensureHotspot();
                                }
                            });
                }

                @Override
                public void onReleased() {
                    mainHandler.post(
                            () -> {
                                apiInitializationRequested = false;
                                if (running) {
                                    scheduleApiInitializationRetry();
                                }
                            });
                }

                @Override
                public void onInitError() {
                    mainHandler.post(
                            () -> {
                                apiInitializationRequested = false;
                                if (running) {
                                    Log.e(TAG, "SVR AndroidInterface initialization failed");
                                    scheduleApiInitializationRetry();
                                }
                            });
                }
            };

    private WifiUtils wifiUtils;
    private boolean running;
    private boolean apiInitializationRequested;
    private boolean configurationApplied;

    HotspotManager(Application application) {
        this.application = application;
    }

    void start() {
        Log.i(TAG, "Starting hotspot manager");
        if (running) {
            scheduleEnsure(0L);
            return;
        }
        running = true;
        configurationApplied = false;
        requestApiInitialization();
    }

    void stop() {
        Log.i(TAG, "Stopping hotspot manager; leaving hotspot state unchanged");
        running = false;
        apiInitializationRequested = false;
        mainHandler.removeCallbacks(ensureRunnable);
        mainHandler.removeCallbacks(initRetryRunnable);
        // Do not stop the hotspot here. It is a device-level service and should
        // remain available if this foreground service is recreated by Android.
        wifiUtils = null;
    }

    /**
     * Recreate the device hotspot after STA provisioning has completed. The manager remains
     * running, so the normal periodic ensure logic continues to keep BOARD available.
     */
    void restartAfterWifiProvisioning() {
        if (!running) {
            return;
        }
        Log.i(TAG, "Restarting hotspot after WiFi provisioning");
        mainHandler.removeCallbacks(ensureRunnable);
        configurationApplied = false;
        mainHandler.post(
                () -> {
                    if (!running) {
                        return;
                    }
                    try {
                        if (wifiUtils == null) {
                            AndroidInterface androidInterface = AndroidInterface.getInstance();
                            if (androidInterface.isInitialized()) {
                                wifiUtils = androidInterface.getWifiUtils();
                            }
                        }
                        if (wifiUtils != null) {
                            boolean stopped = wifiUtils.stopHotspot();
                            Log.i(TAG, "Hotspot stop requested before restart: " + stopped);
                        } else {
                            Log.w(TAG, "WifiUtils is not available for hotspot restart");
                        }
                    } catch (Exception e) {
                        Log.e(TAG, "Failed to stop hotspot before restart", e);
                    }
                    scheduleEnsure(RESTART_DELAY_MS);
                });
    }

    private void requestApiInitialization() {
        if (!running) {
            return;
        }
        try {
            AndroidInterface androidInterface = AndroidInterface.getInstance();
            if (androidInterface.isInitialized()) {
                apiInitializationRequested = false;
                ensureHotspot();
                return;
            }
            if (apiInitializationRequested) {
                return;
            }
            apiInitializationRequested = true;
            androidInterface.init(application, initListener);
        } catch (Exception e) {
            apiInitializationRequested = false;
            Log.e(TAG, "Failed to initialize SVR AndroidInterface", e);
            scheduleApiInitializationRetry();
        }
    }

    private void ensureHotspot() {
        if (!running) {
            return;
        }
        try {
            AndroidInterface androidInterface = AndroidInterface.getInstance();
            if (!androidInterface.isInitialized()) {
                requestApiInitialization();
                return;
            }

            if (wifiUtils == null) {
                wifiUtils = androidInterface.getWifiUtils();
            }
            if (wifiUtils == null) {
                Log.w(TAG, "WifiUtils is not available yet");
                scheduleEnsure(RETRY_DELAY_MS);
                return;
            }

            boolean enabled = wifiUtils.isHotspotEnabled();
            if (!configurationApplied || !enabled) {
                boolean started =
                        wifiUtils.startHotspot(
                                HOTSPOT_SSID,
                                HOTSPOT_PASSWORD,
                                WifiUtils.SECURITY_WPA2);
                Log.i(
                        TAG,
                        "Hotspot start requested: ssid="
                                + HOTSPOT_SSID
                                + ", enabledBefore="
                                + enabled
                                + ", result="
                                + started);
                if (!started) {
                    scheduleEnsure(RETRY_DELAY_MS);
                    return;
                }
                configurationApplied = true;
            } else {
                Log.d(TAG, "Hotspot is enabled: ssid=" + HOTSPOT_SSID);
            }
            scheduleEnsure(CHECK_INTERVAL_MS);
        } catch (Exception e) {
            Log.e(TAG, "Failed to ensure hotspot", e);
            scheduleEnsure(RETRY_DELAY_MS);
        }
    }

    private void scheduleEnsure(long delayMs) {
        if (!running) {
            return;
        }
        mainHandler.removeCallbacks(ensureRunnable);
        mainHandler.postDelayed(ensureRunnable, delayMs);
    }

    private void scheduleApiInitializationRetry() {
        if (!running) {
            return;
        }
        mainHandler.removeCallbacks(initRetryRunnable);
        mainHandler.postDelayed(initRetryRunnable, RETRY_DELAY_MS);
    }
}
