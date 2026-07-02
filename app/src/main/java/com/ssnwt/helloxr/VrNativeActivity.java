/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

package com.ssnwt.helloxr;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.NativeActivity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.media.MediaRecorder;
import android.net.Uri;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.os.Handler;
import android.os.Message;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
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

    // Intent actions for remote control
    public static final String ACTION_SAVE_IMAGE = "com.ssnwt.helloxr.SAVE_IMAGE";
    public static final String ACTION_START_RECORDING = "com.ssnwt.helloxr.START_RECORDING";
    public static final String ACTION_STOP_RECORDING = "com.ssnwt.helloxr.STOP_RECORDING";
    public static final String ACTION_MEDIA_MOUNTED_CUSTOM = "com.ssnwt.action.MEDIA_MOUNTED";
    public static final String ACTION_MEDIA_EJECT_CUSTOM = "com.ssnwt.action.MEDIA_EJECT";
    private static final String EXPORT_DIR_NAME = "Export";
    private static final String USB_DEBUG_LOG_NAME = "usb_debug.log";
    private static final String USB_PROBE_LOG_NAME = "usb_debug_probe.txt";

    // Native methods for intent control
    public native void nativeRequestSnapshot();
    public native void nativeStartRecording();
    public native void nativeStopRecording();
    public native void nativeStartExporter(String exportPath);
    public native void nativeStopExporter();
    private BatteryManager mBatteryManager;
    private BatteryInfo mBatteryInfo;
    private boolean isRegisterReceiver = false;
    private MediaRecorder mRecorder;
    private boolean isRecording = false;
    private BluetoothAdapter bluetoothAdapter;
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

    @SuppressLint("MissingPermission")
    private void startPairBluetoothKeyboard() {
        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        bluetoothAdapter.startDiscovery();
    }

    @SuppressLint("MissingPermission")
    private void pairDevice(BluetoothDevice device) {
        bluetoothAdapter.cancelDiscovery();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            device.createBond();
        }
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
        if (!isRegisterReceiver) {
            IntentFilter filter = new IntentFilter();
            filter.addAction(Intent.ACTION_BATTERY_CHANGED);
            registerReceiver(mBroadcastReceiver, filter);

            IntentFilter btfilter = new IntentFilter();
            btfilter.addAction(BluetoothDevice.ACTION_FOUND);
            btfilter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
            registerReceiver(bluetoothReceiver, btfilter);

            // Register command intent receiver
            IntentFilter cmdFilter = new IntentFilter();
            cmdFilter.addAction(ACTION_SAVE_IMAGE);
            cmdFilter.addAction(ACTION_START_RECORDING);
            cmdFilter.addAction(ACTION_STOP_RECORDING);
            ContextCompat.registerReceiver(this, mCommandReceiver, cmdFilter, ContextCompat.RECEIVER_EXPORTED);

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
        super.onStop();
        if (isRegisterReceiver) {
            unregisterReceiver(mBroadcastReceiver);
            unregisterReceiver(bluetoothReceiver);
            unregisterReceiver(mCommandReceiver);
            unregisterReceiver(mUsbReceiver);
            isRegisterReceiver = false;
        }
    }

    @Override protected void onDestroy() {
        super.onDestroy();
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

    private String formatUsbDebugLine(String message) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
                .format(new Date()) + " " + message + "\n";
    }

    private void appendUsbProbe(File targetDir, String message) {
        if (targetDir == null) {
            appendUsbDebug("appendUsbProbe: target dir is null for message=" + message);
            return;
        }

        File probeFile = new File(targetDir, USB_PROBE_LOG_NAME);
        FileOutputStream fos = null;
        try {
            fos = new FileOutputStream(probeFile, true);
            fos.write(formatUsbDebugLine(message).getBytes());
            fos.flush();
            appendUsbDebug("appendUsbProbe: wrote " + probeFile.getAbsolutePath());
        } catch (IOException e) {
            Log.w(TAG, "appendUsbProbe failed: " + probeFile.getAbsolutePath(), e);
            appendUsbDebug("appendUsbProbe failed: " + probeFile.getAbsolutePath() + " err=" + e.getMessage());
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
            try {
                nativeStopExporter();
                mActiveExportRoot = null;
                speak("u盘已卸载");
                appendUsbDebug("refreshExportUsbRoot: nativeStopExporter called");
            } catch (UnsatisfiedLinkError e) {
                Log.w(TAG, "nativeStopExporter not implemented yet", e);
                appendUsbDebug("refreshExportUsbRoot: nativeStopExporter missing " + e.getMessage());
            }
            return;
        }

        File usbRootDir = new File(usbRoot);
        appendUsbProbe(usbRootDir, "mounted root=" + usbRoot
                + " exists=" + usbRootDir.exists()
                + " canRead=" + usbRootDir.canRead()
                + " canWrite=" + usbRootDir.canWrite());

        File exportRoot = new File(usbRoot, EXPORT_DIR_NAME);
        String exportRootPath = exportRoot.getAbsolutePath();
        boolean exportRootReady = exportRoot.exists() || exportRoot.mkdirs();
        appendUsbDebug("refreshExportUsbRoot: usbRoot=" + usbRoot
                + " exportRoot=" + exportRootPath
                + " ready=" + exportRootReady
                + " canWrite=" + exportRoot.canWrite());
        appendUsbProbe(usbRootDir, "export_root path=" + exportRootPath
                + " ready=" + exportRootReady
                + " canWrite=" + exportRoot.canWrite());
        if (!exportRootReady) {
            Log.w(TAG, "refreshExportUsbRoot: export root is not ready " + exportRootPath);
            appendUsbDebug("refreshExportUsbRoot: export root is not ready " + exportRootPath);
            return;
        }
        appendUsbProbe(exportRoot, "export_root_ready path=" + exportRootPath
                + " canWrite=" + exportRoot.canWrite());
        if (exportRootPath.equals(mActiveExportRoot)) {
            appendUsbDebug("refreshExportUsbRoot: export root unchanged");
            appendUsbProbe(exportRoot, "export_root_unchanged path=" + exportRootPath);
            return;
        }

        if (mActiveExportRoot != null) {
            try {
                nativeStopExporter();
                appendUsbDebug("refreshExportUsbRoot: switched exporter root, stopped previous exporter");
                appendUsbProbe(exportRoot, "stopped_previous_exporter oldRoot=" + mActiveExportRoot);
            } catch (UnsatisfiedLinkError e) {
                Log.w(TAG, "nativeStopExporter not implemented yet", e);
                appendUsbDebug("refreshExportUsbRoot: nativeStopExporter missing while switching " + e.getMessage());
            }
        }

        Log.i(TAG, "refreshExportUsbRoot: start exporter with " + exportRootPath);
        appendUsbDebug("refreshExportUsbRoot: preparing nativeStartExporter with " + exportRootPath);
        appendUsbProbe(exportRoot, "before_nativeStartExporter exportRoot=" + exportRootPath);
        try {
            nativeStartExporter(exportRootPath);
            mActiveExportRoot = exportRootPath;
            speak("u盘已识别");
            appendUsbDebug("refreshExportUsbRoot: nativeStartExporter called successfully");
            appendUsbProbe(exportRoot, "after_nativeStartExporter exportRoot=" + exportRootPath);
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "nativeStartExporter not implemented yet", e);
            appendUsbDebug("refreshExportUsbRoot: nativeStartExporter missing " + e.getMessage());
        }
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
            appendUsbProbe(directory, "selected_usb_root path=" + directory.getAbsolutePath()
                    + " canRead=" + directory.canRead()
                    + " canWrite=" + directory.canWrite());
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
            }
        }
    };

    private final BroadcastReceiver bluetoothReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                @SuppressLint("MissingPermission") String name = device.getName();
                @SuppressLint("MissingPermission") BluetoothClass btClass = device.getBluetoothClass();
                if (btClass != null) {
                    int major = btClass.getMajorDeviceClass();
                    int deviceClass = btClass.getDeviceClass();
                    if (major == BluetoothClass.Device.Major.PERIPHERAL) {
                        if (deviceClass == 0x0540/*BluetoothClass.Device.PERIPHERAL_KEYBOARD*/ ||
                            deviceClass == 0x05C0/*BluetoothClass.Device.PERIPHERAL_KEYBOARD_POINTING*/) {
                            pairDevice(device);
                        }
                    }
                }
            }

            if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action)) {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                int state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1);
                if (state == BluetoothDevice.BOND_BONDED) {
                }
            }
        }
    };

    private final BroadcastReceiver mCommandReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_SAVE_IMAGE.equals(action)) {
                Log.i(TAG, "Intent: SAVE_IMAGE");
                nativeRequestSnapshot();
            } else if (ACTION_START_RECORDING.equals(action)) {
                Log.i(TAG, "Intent: START_RECORDING");
                nativeStartRecording();
            } else if (ACTION_STOP_RECORDING.equals(action)) {
                Log.i(TAG, "Intent: STOP_RECORDING");
                nativeStopRecording();
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
