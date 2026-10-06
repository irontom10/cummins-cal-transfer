package com.irontom10.calibrationtransfer;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

public final class TransferGuardService extends Service {
    private static final String CHANNEL_ID = "calibration_transfer";
    private static final int NOTIFICATION_ID = 4101;

    @Override
    public void onCreate() {
        super.onCreate();

        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Calibration transfer",
                    NotificationManager.IMPORTANCE_LOW);
            channel.setDescription(
                    "Keeps an active ECM calibration transfer running in the foreground.");

            NotificationManager manager =
                    (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
            if (manager != null)
                manager.createNotificationChannel(channel);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Notification.Builder builder;

        if (Build.VERSION.SDK_INT >= 26)
            builder = new Notification.Builder(this, CHANNEL_ID);
        else
            builder = new Notification.Builder(this);

        builder.setSmallIcon(R.drawable.caltool_icon)
                .setContentTitle("Calibration transfer in progress")
                .setContentText("Do not disconnect the adapter or close the transfer.")
                .setOngoing(true)
                .setCategory(Notification.CATEGORY_SERVICE);

        startForeground(NOTIFICATION_ID, builder.build());
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
