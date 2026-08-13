package com.arthurthion.openride;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

import org.libsdl.app.SDLActivity;

public class OpenRideActivity extends SDLActivity implements LocationListener {
    private static final int LOCATION_PERMISSION_REQUEST = 1901;
    private LocationManager locationManager;
    private volatile Location latestLocation;
    private volatile boolean locationRequested;
    private boolean updatesActive;

    private static final int DOWNLOAD_IDLE = 0;
    private static final int DOWNLOAD_RUNNING = 1;
    private static final int DOWNLOAD_COMPLETE = 2;
    private static final int DOWNLOAD_ERROR = 3;
    private static final int DOWNLOAD_CANCELLED = 4;
    private volatile int downloadState = DOWNLOAD_IDLE;
    private volatile long downloadBytes;
    private volatile long downloadTotal;
    private volatile String downloadError = "";
    private volatile boolean downloadCancelRequested;
    private Thread downloadThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        locationManager = (LocationManager)getSystemService(Context.LOCATION_SERVICE);
    }

    private boolean hasLocationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return true;
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
            || checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestLocationPermissionOnUiThread() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !hasLocationPermission()) {
            requestPermissions(new String[] {
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION
            }, LOCATION_PERMISSION_REQUEST);
        }
    }

    private boolean startLocationUpdatesOnUiThread() {
        if (locationManager == null || !hasLocationPermission()) return false;
        if (updatesActive) return true;

        boolean registered = false;
        try {
            if (locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER,
                                                       500L,
                                                       0.5f,
                                                       this);
                Location last = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
                if (last != null) latestLocation = last;
                registered = true;
            }
            if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                locationManager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER,
                                                       1000L,
                                                       1.0f,
                                                       this);
                if (latestLocation == null) {
                    Location last = locationManager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
                    if (last != null) latestLocation = last;
                }
                registered = true;
            }
        } catch (SecurityException ignored) {
            registered = false;
        }
        updatesActive = registered;
        return registered;
    }

    private void stopLocationUpdatesOnUiThread() {
        if (locationManager != null && updatesActive) {
            try {
                locationManager.removeUpdates(this);
            } catch (SecurityException ignored) {
            }
        }
        updatesActive = false;
    }

    /* Called through JNI from OpenRide's SDL/native thread. */
    public boolean openRideStartLocation() {
        if (locationManager == null) return false;
        locationRequested = true;
        runOnUiThread(() -> {
            if (!locationRequested) return;
            if (!hasLocationPermission()) {
                requestLocationPermissionOnUiThread();
            } else {
                startLocationUpdatesOnUiThread();
            }
        });
        return true;
    }

    /* Called through JNI from OpenRide's SDL/native thread. */
    public void openRideStopLocation() {
        locationRequested = false;
        runOnUiThread(this::stopLocationUpdatesOnUiThread);
    }

    public double[] openRideReadLocation() {
        Location location = latestLocation;
        if (location == null) return null;
        return new double[] {
            location.getLatitude(),
            location.getLongitude(),
            location.hasSpeed() ? location.getSpeed() : -1.0,
            location.hasBearing() ? location.getBearing() : -1.0,
            location.hasAccuracy() ? location.getAccuracy() : -1.0,
            location.getElapsedRealtimeNanos() / 1000000000.0
        };
    }

    public synchronized boolean openRideStartDownload(String urlText, String relativePath) {
        if (urlText == null || relativePath == null || downloadState == DOWNLOAD_RUNNING) return false;
        downloadCancelRequested = false;
        downloadBytes = 0L;
        downloadTotal = 0L;
        downloadError = "";
        downloadState = DOWNLOAD_RUNNING;
        downloadThread = new Thread(() -> {
            File destination = new File(getFilesDir(), relativePath);
            File part = new File(destination.getAbsolutePath() + ".part");
            HttpURLConnection connection = null;
            try {
                File parent = destination.getParentFile();
                if (parent != null && !parent.exists() && !parent.mkdirs()) {
                    throw new Exception("Impossible de creer le dossier de telechargement");
                }
                URL url = new URL(urlText);
                connection = (HttpURLConnection)url.openConnection();
                connection.setConnectTimeout(20000);
                connection.setReadTimeout(30000);
                connection.setInstanceFollowRedirects(true);
                connection.setRequestProperty("User-Agent", "OpenRide/0.23 Android");
                connection.connect();
                int code = connection.getResponseCode();
                if (code < 200 || code >= 300) {
                    throw new Exception("HTTP " + code);
                }
                downloadTotal = Math.max(0L, connection.getContentLengthLong());
                try (InputStream input = connection.getInputStream();
                     FileOutputStream output = new FileOutputStream(part, false)) {
                    byte[] buffer = new byte[128 * 1024];
                    int read;
                    while ((read = input.read(buffer)) >= 0) {
                        if (downloadCancelRequested) {
                            downloadState = DOWNLOAD_CANCELLED;
                            return;
                        }
                        if (read == 0) continue;
                        output.write(buffer, 0, read);
                        downloadBytes += read;
                    }
                    output.getFD().sync();
                }
                if (downloadCancelRequested) {
                    downloadState = DOWNLOAD_CANCELLED;
                    return;
                }
                if (destination.exists() && !destination.delete()) {
                    throw new Exception("Impossible de remplacer le fichier existant");
                }
                if (!part.renameTo(destination)) {
                    throw new Exception("Impossible de finaliser le telechargement");
                }
                downloadState = DOWNLOAD_COMPLETE;
            } catch (Exception exception) {
                downloadError = exception.getMessage() != null ? exception.getMessage() : exception.toString();
                downloadState = DOWNLOAD_ERROR;
            } finally {
                if (connection != null) connection.disconnect();
                if (downloadState != DOWNLOAD_COMPLETE && part.exists()) part.delete();
            }
        }, "OpenRide-region-download");
        downloadThread.start();
        return true;
    }

    public void openRideCancelDownload() {
        downloadCancelRequested = true;
    }

    public long[] openRideReadDownload() {
        return new long[] { downloadState, downloadBytes, downloadTotal };
    }

    public String openRideReadDownloadError() {
        return downloadError != null ? downloadError : "";
    }

    @Override
    public void onLocationChanged(Location location) {
        if (location != null) latestLocation = location;
    }

    @Override
    protected void onPause() {
        stopLocationUpdatesOnUiThread();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (locationRequested && hasLocationPermission()) {
            startLocationUpdatesOnUiThread();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == LOCATION_PERMISSION_REQUEST && locationRequested && hasLocationPermission()) {
            startLocationUpdatesOnUiThread();
        }
    }

    @Override
    protected void onDestroy() {
        locationRequested = false;
        downloadCancelRequested = true;
        stopLocationUpdatesOnUiThread();
        super.onDestroy();
    }
}
