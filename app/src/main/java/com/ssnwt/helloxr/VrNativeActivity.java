/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

package com.ssnwt.helloxr;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.NativeActivity;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.media.MediaRecorder;
import android.net.Uri;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.RemoteException;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import com.ssnwt.helloxr.ble.BleService;
import com.ssnwt.vr.androidmanager.AndroidInterface;
import com.ssnwt.vr.androidmanager.SystemEventUtils;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.speech.tts.TextToSpeech;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class VrNativeActivity extends NativeActivity implements SystemEventUtils.Listener, TextToSpeech.OnInitListener {
    private static final String TAG = "VrNativeActivity";

    static {
        // Explicitly load the native library so JNI can find native methods
        // NativeActivity loads via android.app.lib_name but doesn't register JNI methods
        System.loadLibrary("mixedreality");
    }
    public static final String FIRST_TIME_TAG = "first_time";
    public static final String ASSETS_SUB_FOLDER_NAME = "raw";
    public static final int BUFFER_SIZE = 1024;

    public static final String ACTION_MEDIA_MOUNTED_CUSTOM = "com.ssnwt.action.MEDIA_MOUNTED";
    public static final String ACTION_MEDIA_EJECT_CUSTOM = "com.ssnwt.action.MEDIA_EJECT";
    private static final String EXPORT_DIR_NAME = "Export";
    private static final String USB_DEBUG_LOG_NAME = "usb_debug.log";

    // Native control and bridge methods.
    public native void nativeRequestSnapshot();
    public native void nativeStartRecording();
    public native void nativeStopRecording();
    public native void nativeStartExporter(String exportPath);
    public native void nativeStopExporter();
    public native String nativeGetSdkStateJson();
    public native String nativeConsumeSdkErrorJson();
    public native void nativeUpdatePlatformState(
            boolean hasBattery,
            int batteryLevel,
            boolean batteryCharging,
            float batteryVoltage,
            boolean hasBatteryTemperature,
            float batteryTemperature,
            boolean hasWifi,
            boolean wifiConnected,
            String wifiSsid,
            int wifiRssi,
            int wifiChannel,
            String wifiIpAddress,
            long updateTimeMs);
    private BatteryManager mBatteryManager;
    private BatteryInfo mBatteryInfo;
    private boolean isRegisterReceiver = false;
    private MediaRecorder mRecorder;
    private boolean isRecording = false;
    private WifiManager mWifiManager;
    private WifiManager.LocalOnlyHotspotReservation mReservation;
    private TextToSpeech mTts;
    private boolean mTtsReady = false;
    // SoundPool for pre-generated audio prompts
    private SoundPool mSoundPool;
    private int mSoundImageSaved;
    private int mSoundImageFailed;
    private int mSoundRecordingStart;
    private int mSoundRecordingStop;
    private int mSoundUsbRecognized;
    private int mSoundCopyFinished;
    private int mSoundUsbUnplugged;
    private int mSoundCopyFailed;
    private int mSoundNoDatasetRemains;
    private int mSoundStorageFullStart;
    private int mSoundStorageFullStop;
    private String mDatasetRootPath;
    private String mActiveExportRoot;
    private final Handler mBleHandler = new Handler(Looper.getMainLooper());
    private IBleService mBleService;
    private boolean mBleServiceBound = false;
    private final IBleCallback mBleCallback =
            new IBleCallback.Stub() {
                @Override
                public void onCommand(String command) {
                    Log.i(TAG, "Ignoring deprecated BLE control callback payload: " + command);
                }

                @Override
                public void onWifiConnected(String ipAddress) {
                    Log.i(TAG, "BLE WiFi connected callback: " + ipAddress);
                    requestPlatformStatePush();
                }

                @Override
                public void onWifiFailed(String message) {
                    Log.w(TAG, "BLE WiFi failed callback: " + message);
                    requestPlatformStatePush();
                }

                @Override
                public void onBleConnectionChanged(boolean connected) {
                    Log.i(TAG, "BLE connection callback: " + connected);
                    requestPlatformStatePush();
                }
            };
    private final ServiceConnection mBleServiceConnection =
            new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder service) {
                    mBleService = IBleService.Stub.asInterface(service);
                    mBleServiceBound = true;
                    Log.i(TAG, "BleService bound");
                    registerBleCallback();
                }

                @Override
                public void onServiceDisconnected(ComponentName name) {
                    Log.w(TAG, "BleService disconnected");
                    mBleService = null;
                    mBleServiceBound = false;
                    requestPlatformStatePush();
                    bindBleService();
                }
            };
    private Handler mHandler = new Handler() {
        @Override public void handleMessage(@NonNull Message msg) {
            switch (msg.what) {
                case 100:
                    WifiConfiguration config = (WifiConfiguration) msg.obj;
                    Log.d(TAG, "ssid:" + config.SSID + ", password:" + config.preSharedKey);
                    break;
            }
        }
    };

    void setImmersiveSticky() {
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private void bindBleService() {
        if (mBleServiceBound) {
            return;
        }
        Intent intent = new Intent(this, BleService.class);
        boolean bound = bindService(intent, mBleServiceConnection, Context.BIND_AUTO_CREATE);
        Log.i(TAG, "bindBleService requested: " + bound);
    }

    private void unregisterBleCallback() {
        if (mBleService == null) {
            return;
        }
        try {
            mBleService.unregisterCallback(mBleCallback);
        } catch (RemoteException e) {
            Log.w(TAG, "Failed to unregister BLE callback", e);
        }
    }

    private void unbindBleService() {
        unregisterBleCallback();
        if (mBleServiceBound) {
            unbindService(mBleServiceConnection);
        }
        mBleService = null;
        mBleServiceBound = false;
    }

    private void registerBleCallback() {
        if (mBleService == null) {
            return;
        }
        try {
            mBleService.registerCallback(mBleCallback);
            requestPlatformStatePush();
        } catch (RemoteException e) {
            Log.e(TAG, "Failed to register BLE callback", e);
        }
    }

    private void requestPlatformStatePush() {
        mBleHandler.post(this::pushPlatformStateToNative);
    }

    private void pushPlatformStateToNative() {
        PlatformStateSnapshot snapshot = collectPlatformStateSnapshot();
        nativeUpdatePlatformState(
                snapshot.hasBattery,
                snapshot.batteryLevel,
                snapshot.batteryCharging,
                snapshot.batteryVoltage,
                snapshot.hasBatteryTemperature,
                snapshot.batteryTemperature,
                snapshot.hasWifi,
                snapshot.wifiConnected,
                snapshot.wifiSsid,
                snapshot.wifiRssi,
                snapshot.wifiChannel,
                snapshot.wifiIpAddress,
                snapshot.updateTimeMs);
    }

    private PlatformStateSnapshot collectPlatformStateSnapshot() {
        PlatformStateSnapshot snapshot = new PlatformStateSnapshot();
        snapshot.hasBattery = mBatteryInfo != null && mBatteryInfo.hasSample();
        snapshot.batteryLevel = mBatteryInfo != null ? mBatteryInfo.level : -1;
        snapshot.batteryCharging =
                mBatteryInfo != null
                        && (mBatteryInfo.plugged != 0
                                || mBatteryInfo.status == BatteryManager.BATTERY_STATUS_CHARGING
                                || mBatteryInfo.status == BatteryManager.BATTERY_STATUS_FULL);
        snapshot.batteryVoltage = mBatteryInfo != null ? mBatteryInfo.voltage / 1000.0f : 0.0f;
        snapshot.hasBatteryTemperature =
                mBatteryInfo != null && mBatteryInfo.temperature > 0;
        snapshot.batteryTemperature =
                mBatteryInfo != null ? mBatteryInfo.temperature / 10.0f : 0.0f;
        fillWifiSnapshot(snapshot);
        snapshot.updateTimeMs = System.currentTimeMillis();
        return snapshot;
    }

    private void fillWifiSnapshot(PlatformStateSnapshot snapshot) {
        snapshot.hasWifi = mWifiManager != null;
        snapshot.wifiSsid = queryProvisionedWifiSsid();
        snapshot.wifiIpAddress = queryProvisionedWifiIpAddress();
        if (snapshot.wifiSsid.isEmpty() || snapshot.wifiIpAddress.isEmpty()) {
            tryFillWifiSnapshotFromManager(snapshot);
        }
        snapshot.wifiConnected =
                !snapshot.wifiSsid.isEmpty() || !snapshot.wifiIpAddress.isEmpty();
    }

    private void tryFillWifiSnapshotFromManager(PlatformStateSnapshot snapshot) {
        if (mWifiManager == null) {
            return;
        }
        try {
            WifiInfo wifiInfo = mWifiManager.getConnectionInfo();
            if (wifiInfo == null) {
                return;
            }
            if (TextUtils.isEmpty(snapshot.wifiSsid)) {
                snapshot.wifiSsid = sanitizeWifiSsid(wifiInfo.getSSID());
            }
            snapshot.wifiRssi = wifiInfo.getRssi();
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                snapshot.wifiChannel = frequencyToChannel(wifiInfo.getFrequency());
            }
        } catch (SecurityException e) {
            Log.w(TAG, "WiFi state access denied", e);
        } catch (Exception e) {
            Log.w(TAG, "Failed to read WiFi manager state", e);
        }
    }

    private String queryProvisionedWifiSsid() {
        if (mBleService == null) {
            return "";
        }
        try {
            return sanitizeWifiSsid(mBleService.getWifiSsid());
        } catch (RemoteException e) {
            Log.w(TAG, "Failed to query provisioned WiFi SSID", e);
            return "";
        }
    }

    private String queryProvisionedWifiIpAddress() {
        if (mBleService == null) {
            return "";
        }
        try {
            String ipAddress = mBleService.getWifiIpAddress();
            return ipAddress != null ? ipAddress.trim() : "";
        } catch (RemoteException e) {
            Log.w(TAG, "Failed to query provisioned WiFi IP", e);
            return "";
        }
    }

    private String sanitizeWifiSsid(String ssid) {
        if (ssid == null) {
            return "";
        }
        String trimmed = ssid.trim();
        if (trimmed.isEmpty()
                || "<unknown ssid>".equalsIgnoreCase(trimmed)
                || "unknown ssid".equalsIgnoreCase(trimmed)) {
            return "";
        }
        if (trimmed.length() >= 2 && trimmed.startsWith("\"") && trimmed.endsWith("\"")) {
            return trimmed.substring(1, trimmed.length() - 1);
        }
        return trimmed;
    }

    private int frequencyToChannel(int frequency) {
        if (frequency >= 2412 && frequency <= 2484) {
            if (frequency == 2484) {
                return 14;
            }
            return (frequency - 2407) / 5;
        }
        if (frequency >= 5000 && frequency <= 5895) {
            return (frequency - 5000) / 5;
        }
        if (frequency >= 5955 && frequency <= 7115) {
            return (frequency - 5950) / 5;
        }
        return 0;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        setContentView(R.layout.activity_main);

        setImmersiveSticky();

        View decorView = getWindow().getDecorView();
        decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility) {
                setImmersiveSticky();
            }
        });

        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        if (!prefs.getBoolean(FIRST_TIME_TAG, false)) {
            SharedPreferences.Editor editor = prefs.edit();
            editor.putBoolean(FIRST_TIME_TAG, true);
            editor.commit();
            copyAssetsToExternal();
        }

        initExporterConfig();
        super.onCreate(savedInstanceState);
        ensureManageExternalStoragePermission();
        initSvrApi();
        mBatteryManager = (BatteryManager) getSystemService(BATTERY_SERVICE);
        mBatteryInfo = new BatteryInfo();
        mBatteryInfo.init(this);

        mWifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE);

        // Initialize TTS (optional, may not be available on device)
        mTts = new TextToSpeech(this, this);

        // Initialize SoundPool for pre-generated audio prompts (always available)
        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_NOTIFICATION_EVENT)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        mSoundPool = new SoundPool.Builder().setMaxStreams(2).setAudioAttributes(attrs).build();
        mSoundImageSaved = mSoundPool.load(this, R.raw.image_saved, 1);
        mSoundImageFailed = mSoundPool.load(this, R.raw.image_failed, 1);
        mSoundRecordingStart = mSoundPool.load(this, R.raw.recording_start, 1);
        mSoundRecordingStop = mSoundPool.load(this, R.raw.recording_stop, 1);
        mSoundUsbRecognized = mSoundPool.load(this, R.raw.usb_recog, 1);
        mSoundCopyFinished = mSoundPool.load(this, R.raw.copy_finished, 1);
        mSoundCopyFailed = mSoundPool.load(this, R.raw.copy_failed, 1);
        mSoundNoDatasetRemains = mSoundPool.load(this, R.raw.no_data_remains, 1);
        mSoundUsbUnplugged = mSoundPool.load(this, R.raw.usb_unplug, 1);
        mSoundStorageFullStart = mSoundPool.load(this, R.raw.storage_full_start, 1);
        mSoundStorageFullStop = mSoundPool.load(this, R.raw.storage_full_stop, 1);
        bindBleService();
        requestPlatformStatePush();
    }

    private void onSvrApiInitialized() {
        AndroidInterface.getInstance().getSystemEventUtils().setListener(this);
    }

    @Override
    public void onInit(int status) {
        if (status == TextToSpeech.SUCCESS) {
            int result = mTts.setLanguage(Locale.CHINESE);
            if (result == TextToSpeech.LANG_MISSING_DATA || result == TextToSpeech.LANG_NOT_SUPPORTED) {
                Log.w(TAG, "Chinese TTS not supported, falling back to default");
                mTts.setLanguage(Locale.getDefault());
            }
            mTtsReady = true;
            Log.i(TAG, "TTS initialized successfully");
        } else {
            Log.e(TAG, "TTS initialization failed");
        }
    }

    public void speak(String text) {
        // Prefer TTS if available, otherwise play pre-generated audio
        if (mTtsReady && mTts != null) {
            mTts.speak(text, TextToSpeech.QUEUE_ADD, null, "tts_" + System.currentTimeMillis());
            Log.d(TAG, "TTS speak: " + text);
            return;
        }

        // Fallback: play pre-generated audio
        int soundId = 0;
        if (text.contains("图片已保存")) {
            soundId = mSoundImageSaved;
        } else if (text.contains("图片保存失败")) {
            soundId = mSoundImageFailed;
        } else if (text.contains("开始录制")) {
            soundId = mSoundRecordingStart;
        } else if (text.contains("视频已保存") || text.contains("录制已保存")) {
            soundId = mSoundRecordingStop;
        } else if (text.contains("u盘已识别")) {
            soundId = mSoundUsbRecognized;
        } else if (text.contains("u盘拷贝已完成")) {
            soundId = mSoundCopyFinished;
        } else if (text.contains("u盘已卸载")) {
            soundId = mSoundUsbUnplugged;
        } else if (text.contains("本轮存在拷贝失败")) {
            soundId = mSoundCopyFailed;
        } else if (text.contains("当前无数据需要拷贝")) {
            soundId = mSoundNoDatasetRemains;
        } else if (text.contains("无法继续保存")) {
            soundId = mSoundStorageFullStop;
        } else if (text.contains("无法录制")) {
            soundId = mSoundStorageFullStart;
        }
        if (soundId != 0 && mSoundPool != null) {
            mSoundPool.play(soundId, 1.0f, 1.0f, 1, 0, 1.0f);
            Log.d(TAG, "SoundPool play: " + text);
        } else {
            Log.w(TAG, "No audio for: " + text);
        }
    }

    private void initSvrApi() {
        if (!AndroidInterface.getInstance().isInitialized()) {
            AndroidInterface.getInstance().init(getApplication(), new AndroidInterface.InitListener() {
                @Override
                public void onInitialized() {
                    onSvrApiInitialized();
                }

                @Override public void onReleased() {
                }

                @Override public void onInitError() {
                }
            });
        }
    }

    @SuppressLint("MissingPermission")
    private void enableWiFiAP() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED) {
            return;
        }
        mWifiManager.startLocalOnlyHotspot(new WifiManager.LocalOnlyHotspotCallback() {
            @Override
            public void onStarted(WifiManager.LocalOnlyHotspotReservation reservation) {
                super.onStarted(reservation);
                mReservation = reservation;
                WifiConfiguration wifiConfiguration = reservation.getWifiConfiguration();
                mHandler.obtainMessage(100, wifiConfiguration).sendToTarget();
            }

            @Override
            public void onFailed(int reason) {
                super.onFailed(reason);
            }
        }, mHandler);
    }

    /**
     * 红灯亮
     */
    private void flashRedLight() {
        flashLed(1);
    }

    /**
     * 绿灯亮
     */
    private void flashGreenLight() {
        flashLed(2);
    }

    /**
     * 蓝灯亮
     */
    private void flashBlueLight() {
        flashLed(3);
    }

    /**
     * 红灯闪烁
     */
    private void blinkRedLed() {
        blinkLed(1);
    }

    /**
     * 绿灯闪烁
     */
    private void blinkGreenLed() {
        blinkLed(2);
    }

    /**
     * 蓝灯闪烁
     */
    private void blinkBlueLed() {
        blinkLed(3);
    }

    private void flashLed(int type) {
        AndroidInterface.getInstance().getDeviceUtils().flashLed(type);
    }

    private void blinkLed(int type) {
        AndroidInterface.getInstance().getDeviceUtils().blinkLed(type, 100, 100);
    }

    private void startAudioRecord() {
        if (isRecording) {
            return;
        }
        mRecorder = new MediaRecorder();

        mRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
        mRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
        mRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
        String filepath = getExternalCacheDir().getAbsolutePath() + File.separator
            + System.currentTimeMillis() + ".m4a";
        mRecorder.setOutputFile(filepath);
        mRecorder.setAudioSamplingRate(44100);
        mRecorder.setAudioEncodingBitRate(96000);

        try {
            mRecorder.prepare();
            mRecorder.start();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void stopAudioRecord() {
        if (isRecording && mRecorder != null) {
            mRecorder.stop();
            mRecorder.release();
            mRecorder = null;
        }
    }

    // Audio recording to specific path (for dataset recording)
    public void startAudioRecordToPath(String filepath) {
        if (isRecording) return;
        isRecording = true;
        mRecorder = new MediaRecorder();
        mRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
        mRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
        mRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
        mRecorder.setOutputFile(filepath);
        mRecorder.setAudioSamplingRate(44100);
        mRecorder.setAudioEncodingBitRate(96000);
        try {
            mRecorder.prepare();
            mRecorder.start();
            Log.i(TAG, "Audio recording started: " + filepath);
        } catch (IOException e) {
            Log.e(TAG, "Audio recording failed", e);
            mRecorder.release();
            mRecorder = null;
            isRecording = false;
        }
    }

    public void stopAudioRecordPath() {
        if (isRecording && mRecorder != null) {
            mRecorder.stop();
            mRecorder.release();
            mRecorder = null;
            isRecording = false;
            Log.i(TAG, "Audio recording stopped");
        }
    }

    @Override
    protected void onResume() {
        //Hide toolbar
        int SDK_INT = android.os.Build.VERSION.SDK_INT;
        if (SDK_INT >= 14 && SDK_INT < 19) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_LOW_PROFILE);
        } else if (SDK_INT >= 19) {
            setImmersiveSticky();
        }
        super.onResume();
        bindBleService();
        requestPlatformStatePush();
        if (!isRegisterReceiver) {
            IntentFilter filter = new IntentFilter();
            filter.addAction(Intent.ACTION_BATTERY_CHANGED);
            registerReceiver(mBroadcastReceiver, filter);

            IntentFilter usbFilter = new IntentFilter();
            usbFilter.addAction(Intent.ACTION_MEDIA_MOUNTED);
            usbFilter.addAction(Intent.ACTION_MEDIA_EJECT);
            usbFilter.addAction(ACTION_MEDIA_MOUNTED_CUSTOM);
            usbFilter.addAction(ACTION_MEDIA_EJECT_CUSTOM);
            usbFilter.addDataScheme("file");
            registerReceiver(mUsbReceiver, usbFilter);

            appendUsbDebug("onResume: USB receiver registered");
            refreshExportUsbRoot();

            isRegisterReceiver = true;
        }
    }

    @Override protected void onStop() {
        requestPlatformStatePush();
        super.onStop();
        if (isRegisterReceiver) {
            unregisterReceiver(mBroadcastReceiver);
            unregisterReceiver(mUsbReceiver);
            isRegisterReceiver = false;
        }
    }

    @Override protected void onDestroy() {
        pushPlatformStateToNative();
        unbindBleService();
        mBleHandler.removeCallbacksAndMessages(null);
        if (mSoundPool != null) {
            mSoundPool.release();
            mSoundPool = null;
        }
        if (mTts != null) {
            mTts.stop();
            mTts.shutdown();
            mTts = null;
        }
        if (mReservation != null) {
            mReservation.close();
            mReservation = null;
        }
        super.onDestroy();
    }

    /*
     * copy the Assets from assets/raw to app's external file dir
     */
    public void copyAssetsToExternal() {
        AssetManager assetManager = getAssets();
        String[] files = null;
        try {
            InputStream in = null;
            OutputStream out = null;

            files = assetManager.list(ASSETS_SUB_FOLDER_NAME);
            for (int i = 0; i < files.length; i++) {
                in = assetManager.open(ASSETS_SUB_FOLDER_NAME + "/" + files[i]);
                String outDir = getExternalFilesDir(null).toString() + "/";

                File outFile = new File(outDir, files[i]);
                out = new FileOutputStream(outFile);
                copyFile(in, out);
                in.close();
                in = null;
                out.flush();
                out.close();
                out = null;
            }
        } catch (IOException e) {
            Log.e("copyAssetsToExternal", "Failed to get asset file list.", e);
        }
        File file = getExternalFilesDir(null);
        Log.d("copyAssetsToExternal", "file:" + file.toString());
    }

    /*
     * read file from InputStream and write to OutputStream.
     */
    private void copyFile(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[BUFFER_SIZE];
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
    }

    private void initExporterConfig() {
        File externalFilesDir = getExternalFilesDir(null);
        if (externalFilesDir == null) {
            Log.w(TAG, "initExporterConfig: external files dir is null");
            appendUsbDebug("initExporterConfig: external files dir is null");
            return;
        }

        File datasetRoot = new File(externalFilesDir, "dataset");
        mDatasetRootPath = datasetRoot.getAbsolutePath();
        appendUsbDebug("initExporterConfig: dataset root=" + mDatasetRootPath);
    }

    private void ensureManageExternalStoragePermission() {
        boolean granted = Environment.isExternalStorageManager();
        appendUsbDebug("ensureManageExternalStoragePermission: granted=" + granted);
        if (granted) {
            return;
        }

        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
        intent.setData(Uri.parse("package:" + getPackageName()));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            startActivity(intent);
            appendUsbDebug("ensureManageExternalStoragePermission: opened app all files access settings");
        } catch (Exception e) {
            Log.w(TAG, "Failed to open app all files access settings", e);
            appendUsbDebug("ensureManageExternalStoragePermission: fallback settings " + e.getMessage());
            Intent fallbackIntent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            fallbackIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(fallbackIntent);
        }
    }

    private synchronized void appendUsbDebug(String message) {
        String line = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
                .format(new Date()) + " " + message + "\n";
        Log.i(TAG, line.trim());

        File externalFilesDir = getExternalFilesDir(null);
        if (externalFilesDir == null) {
            return;
        }

        File logFile = new File(externalFilesDir, USB_DEBUG_LOG_NAME);
        FileOutputStream fos = null;
        try {
            fos = new FileOutputStream(logFile, true);
            fos.write(line.getBytes());
            fos.flush();
        } catch (IOException e) {
            Log.e(TAG, "appendUsbDebug failed", e);
        } finally {
            if (fos != null) {
                try {
                    fos.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    private void refreshExportUsbRoot() {
        if (mDatasetRootPath == null || mDatasetRootPath.isEmpty()) {
            Log.w(TAG, "refreshExportUsbRoot: dataset root not ready");
            appendUsbDebug("refreshExportUsbRoot: dataset root not ready");
            return;
        }

        appendUsbDebug("refreshExportUsbRoot: begin");
        String usbRoot = findMountedUsbRoot();
        if (usbRoot == null) {
            Log.i(TAG, "refreshExportUsbRoot: no mounted removable storage");
            appendUsbDebug("refreshExportUsbRoot: no mounted removable storage");
            if (mActiveExportRoot == null) {
                return;
            }
            nativeStopExporter();
            mActiveExportRoot = null;
            speak("u盘已卸载");
            appendUsbDebug("refreshExportUsbRoot: nativeStopExporter called");
            return;
        }

        File exportRoot = new File(usbRoot, EXPORT_DIR_NAME);
        String exportRootPath = exportRoot.getAbsolutePath();
        boolean exportRootReady = exportRoot.exists() || exportRoot.mkdirs();
        appendUsbDebug("refreshExportUsbRoot: usbRoot=" + usbRoot
                + " exportRoot=" + exportRootPath
                + " ready=" + exportRootReady
                + " canWrite=" + exportRoot.canWrite());
        if (!exportRootReady) {
            Log.w(TAG, "refreshExportUsbRoot: export root is not ready " + exportRootPath);
            appendUsbDebug("refreshExportUsbRoot: export root is not ready " + exportRootPath);
            return;
        }
        if (exportRootPath.equals(mActiveExportRoot)) {
            appendUsbDebug("refreshExportUsbRoot: export root unchanged");
            return;
        }

        if (mActiveExportRoot != null) {
            nativeStopExporter();
            appendUsbDebug("refreshExportUsbRoot: switched exporter root, stopped previous exporter");
        }

        Log.i(TAG, "refreshExportUsbRoot: start exporter with " + exportRootPath);
        appendUsbDebug("refreshExportUsbRoot: preparing nativeStartExporter with " + exportRootPath);
        nativeStartExporter(exportRootPath);
        mActiveExportRoot = exportRootPath;
        speak("u盘已识别");
        appendUsbDebug("refreshExportUsbRoot: nativeStartExporter called successfully");
    }

    private String findMountedUsbRoot() {
        StorageManager storageManager = (StorageManager) getSystemService(Context.STORAGE_SERVICE);
        if (storageManager == null) {
            appendUsbDebug("findMountedUsbRoot: storage manager is null");
            return null;
        }

        List<StorageVolume> storageVolumes = storageManager.getStorageVolumes();
        appendUsbDebug("findMountedUsbRoot: volume count=" + storageVolumes.size());
        for (StorageVolume volume : storageVolumes) {
            if (volume == null) {
                appendUsbDebug("findMountedUsbRoot: volume is null");
                continue;
            }
            File directory = volume.getDirectory();
            String state = volume.getState();
            boolean removable = volume.isRemovable();
            boolean mounted = Environment.MEDIA_MOUNTED.equals(state);
            String path = directory != null ? directory.getAbsolutePath() : "null";
            appendUsbDebug("findMountedUsbRoot: candidate path=" + path
                    + " state=" + state
                    + " removable=" + removable
                    + " mounted=" + mounted
                    + " primary=" + volume.isPrimary()
                    + " desc=" + volume.getDescription(this));
            if (!mounted) {
                continue;
            }
            if (!removable) {
                continue;
            }
            if (directory == null) {
                appendUsbDebug("findMountedUsbRoot: mounted removable volume has null directory");
                continue;
            }
            appendUsbDebug("findMountedUsbRoot: mounted removable volume readable=" + directory.canRead()
                    + " writable=" + directory.canWrite()
                    + " exists=" + directory.exists());
            String usbRoot = directory.getAbsolutePath();
            appendUsbDebug("findMountedUsbRoot: selected usb root=" + usbRoot);
            return usbRoot;
        }
        appendUsbDebug("findMountedUsbRoot: no suitable removable dir found");
        return null;
    }

    @Override public void onStartHome() {

    }

    @Override public void onStartQuickMenu(String s) {

    }

    @Override public void onRecenter() {

    }

    @Override public void onOtherCommand(int i, int i1, String s) {

    }

    /**
     * 按键事件回调
     *
     * @param keycode
     * @param event
     */
    @Override
    public void onKeyEvent(int keycode, KeyEvent event) {
        Log.d(TAG, "onKeyEvent code:" + keycode + ", action:" + event.getAction());
    }

    private class BatteryInfo {
        private int status = 1;
        private int health = 1;
        private boolean present = false;
        private int level = 1;
        private int scale = 1;
        private int plugged = 0;
        private int voltage = 1;
        private int temperature = 1;
        private String technology;
        private int capacity;
        private int current;

        private String[] mBatteryTitle;
        private String[] mBatteryStatus;
        private String[] mBatteryHealth;
        private String[] mBatteryPlugged;
        private String[] mBatteryLevel;

        public void init(Context context) {
            mBatteryTitle = context.getResources().getStringArray(R.array.case_battery_info);
            mBatteryStatus = context.getResources().getStringArray(R.array.case_battery_status);
            mBatteryHealth = context.getResources().getStringArray(R.array.case_battery_health);
            mBatteryPlugged = context.getResources().getStringArray(R.array.case_battery_plugged);
            mBatteryLevel = context.getResources().getStringArray(R.array.case_battery_level);
        }

        public boolean hasSample() {
            return present || level > 0 || voltage > 0 || temperature > 0;
        }

        public String[] toArrayString() {
            String[] batteryInfo = new String[11];
            batteryInfo[0] = mBatteryTitle[0] + mBatteryStatus[status - 1];
            batteryInfo[1] = mBatteryTitle[5] + mBatteryPlugged[plugged];
            batteryInfo[2] = mBatteryTitle[2] + (present ? "yes" : "no");
            batteryInfo[3] = mBatteryTitle[3] + level;
            batteryInfo[4] = mBatteryTitle[4] + scale;
            batteryInfo[5] = mBatteryTitle[1] + mBatteryHealth[health - 1];
            batteryInfo[6] = mBatteryTitle[6] + (voltage / 1000.0f) + " V";
            if (health == BatteryManager.BATTERY_HEALTH_OVERHEAT
                || health == BatteryManager.BATTERY_HEALTH_COLD) {
                batteryInfo[7] = mBatteryTitle[7] + (temperature / 10.0f) + " ℃";
            } else {
                batteryInfo[7] = mBatteryTitle[7] + (temperature / 10.0f) + " ℃";
            }
            batteryInfo[8] = mBatteryTitle[8] + (capacity / 1000) + "mAh";
            batteryInfo[9] = mBatteryTitle[9] + (current / 1000) + "mAh";
            if (level > 20) {
                batteryInfo[10] = mBatteryTitle[10] + mBatteryLevel[0];
            } else if (level > 5) {
                batteryInfo[10] = mBatteryTitle[10] + mBatteryLevel[1];
            } else {
                batteryInfo[10] = mBatteryTitle[10] + mBatteryLevel[2];
            }
            return batteryInfo;
        }
    }

    private static class PlatformStateSnapshot {
        boolean hasBattery;
        int batteryLevel = -1;
        boolean batteryCharging;
        float batteryVoltage;
        boolean hasBatteryTemperature;
        float batteryTemperature;
        boolean hasWifi;
        boolean wifiConnected;
        String wifiSsid = "";
        int wifiRssi;
        int wifiChannel;
        String wifiIpAddress = "";
        long updateTimeMs;
    }

    private BroadcastReceiver mBroadcastReceiver = new BroadcastReceiver() {

        @Override
        public void onReceive(Context context, Intent intent) {
            if (Intent.ACTION_BATTERY_CHANGED.equals(intent.getAction())) {
                mBatteryInfo.status = intent.getIntExtra("status", 0);
                mBatteryInfo.plugged = intent.getIntExtra("plugged", 0);
                mBatteryInfo.health = intent.getIntExtra("health", 0);
                mBatteryInfo.present = intent.getBooleanExtra("present", false);
                mBatteryInfo.level = intent.getIntExtra("level", 0);
                mBatteryInfo.scale = intent.getIntExtra("scale", 0);
                mBatteryInfo.voltage = intent.getIntExtra("voltage", 0);
                mBatteryInfo.temperature = intent.getIntExtra("temperature", 0);
                mBatteryInfo.capacity =
                    mBatteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CHARGE_COUNTER);
                mBatteryInfo.current =
                    mBatteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
                mBatteryInfo.technology = intent.getStringExtra("technology");
                Log.d(TAG, Arrays.toString(mBatteryInfo.toArrayString()));
                requestPlatformStatePush();
            }
        }
    };

    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.i(TAG, "USB storage broadcast: " + action);
            appendUsbDebug("mUsbReceiver.onReceive action=" + action + " data=" + intent.getDataString());
            refreshExportUsbRoot();
        }
    };
}
