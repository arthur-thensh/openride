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

import org.libsdl.app.SDLActivity;

public class OpenRideActivity extends SDLActivity implements LocationListener {
    private static final int LOCATION_PERMISSION_REQUEST = 1901;
    private LocationManager locationManager;
    private volatile Location latestLocation;
    private volatile boolean locationRequested;
    private boolean updatesActive;

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

    @Override
    public void onLocationChanged(Location location) {
        if (location != null) latestLocation = location;
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
        stopLocationUpdatesOnUiThread();
        super.onDestroy();
    }
}
