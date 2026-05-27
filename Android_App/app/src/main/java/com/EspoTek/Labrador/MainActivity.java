package com.EspoTek.Labrador;

import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.hardware.usb.UsbDeviceConnection;
import android.content.Context;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import android.os.Build;
import android.os.Environment;
import android.content.res.Configuration;

import org.libsdl.app.SDLActivity;

import android.content.DialogInterface;
import android.app.AlertDialog;

import java.io.BufferedOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import java.util.HashMap;
import java.util.Iterator;
import java.lang.String;
import java.io.File;
// import androidx.documentfile.provider.DocumentFile;
import android.media.MediaScannerConnection;

import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.ViewCompat;
import android.content.pm.ActivityInfo;
// import android.graphics.Insets;

// TODO: handle edge cases related to user unplugging board in non-bootloader state, replugging it back in in bootloader state
public class MainActivity extends SDLActivity {
    private static final String ACTION_USB_PERMISSION = "com.EspoTek.Labrador.USB_PERMISSION";
    private static final String usbStateChangeAction = "android.hardware.usb.action.USB_STATE";
    private static final String TAG = "com.EspoTek.Labrador";
    public AssetManager mgr;
    private boolean usb_permission_request_allowed = true;

    private UsbDeviceConnection connection = null;
    public boolean bootloader_mode_allowed = false; // modified by usbcallhandler
    private native void nativeRespondToStartupOrUsbStateChange(boolean is_plugged_in, int file_descriptor, boolean bootloader_mode); // fn in librador.cpp.  **this fn initiates librador**
    private native void nativeInitiateFirmwareFlash();

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL3", "labrador-imgui-android"};
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override 
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
//     https://stackoverflow.com/questions/10941802/cant-access-aassetmanager-in-native-code-passed-from-java-in-wallpaperservice/11617834#11617834
        mgr = getResources().getAssets();
    }

// https://stackoverflow.com/questions/6981736/android-3-1-usb-host-broadcastreceiver-does-not-receive-usb-device-attached/9814826#9814826
// https://stackoverflow.com/questions/8619883/onnewintent-lifecycle-and-registered-listeners
// called when the Labrador board is plugged in or restarted when the app is already running
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        Log.d(TAG, "on new intent");
        setIntent(intent); // set intent for subsequent call to onResume()
    }

// called on startup and on usb stage changes
    @Override
    protected void onResume() {
        super.onResume();

        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        // don't need to add the ACTION_USB_DEVICE_ATTACHED intent because it is already declared in the manifest and accordingly gets forwarded to this app's onNewIntent() by android software.
        registerReceiver(myUsbDetachBroadcastReceiver, filter);

        Intent intent = getIntent();
        Log.d(TAG, "new intent: " + intent.getAction());
        UsbDevice device;
        HashMap<String,Integer> device_info = new HashMap<String,Integer>();
        // look for the device
        UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);  //Handle to system USB service?
        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        if(!deviceIterator.hasNext()){
            Log.d(TAG, "no device found");
        }
        while(device_info.isEmpty() && deviceIterator.hasNext()) {
            Log.d(TAG, "device list has device");
            device = deviceIterator.next();
            device_info = processUsbDevice(device);
        }
        if(device_info.isEmpty()) {
            nativeRespondToStartupOrUsbStateChange(false, -1, false);
        } else {
            int file_descriptor = device_info.get("file_descriptor");
            boolean bootloader_mode = device_info.get("bootloader_mode") == 1 ? true : false;
            if(!bootloader_mode_allowed && bootloader_mode) {
                AlertDialog alert = new AlertDialog.Builder(MainActivity.this)
                     .setMessage("Board found in bootloader mode, which is intended for firmware updates.  Please unplug the board, disconnect Digital Out 1 from GND, plug the board back in, and then unplug and replug the board a second time.  If a firmware update is needed, it will be performed automatically.")
                     .setPositiveButton("OK", new DialogInterface.OnClickListener()
                                {
                                    @Override
                                    public void onClick(DialogInterface dialog, int id)
                                    {
                                    }
                                } 
                                )
                     .setCancelable(false)
                     .create();
                alert.show();
                nativeRespondToStartupOrUsbStateChange(false, file_descriptor, bootloader_mode);
                return;
            }
            nativeRespondToStartupOrUsbStateChange(true, file_descriptor, bootloader_mode);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterReceiver(myUsbDetachBroadcastReceiver);
    }

    @Override
    protected void onStop() {
        super.onStop();
        disconnectIfConnected();
    }

    private HashMap<String,Integer> processUsbDevice(UsbDevice device) {
        HashMap<String,Integer> device_info = new HashMap<String,Integer>();
        PendingIntent mPermissionIntent;
        Intent intent = new Intent(MainActivity.ACTION_USB_PERMISSION);
        intent.setPackage(MainActivity.getContext().getPackageName());

        mPermissionIntent = PendingIntent.getBroadcast(this, 0, intent, PendingIntent.FLAG_IMMUTABLE);

        UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);  //Handle to system USB service?

        // handle edge case: Labrador board is connected -> user puts phone to sleep -> user unplugs Labrador board -> user wakes phone back up (at which point onResume() gets called with intent= the most recent intent received by the app, i.e., the original intent signaling that the board was connected)
        HashMap<String, UsbDevice> dl = manager.getDeviceList();
        if (!manager.getDeviceList().containsValue(device)) {
            return device_info;
        }

        int DeviceID = device.getDeviceId();
        int VID = device.getVendorId();
        int PID = device.getProductId();

        // Block below: for the rare case that the app finds the board and android software hasn't handled the usb permissions, so they need to be handled by the app.  E.g., if the user connects the board for the first time when their phone is asleep
        if(!manager.hasPermission(device)) {
            // block below: open a dialog window that prompts the user for usb connection permission.  meanwhile, this function returns an empty device_info, which gets handled cleanly.  After the user responds to the dialog window, onResume() gets called, which leads back to this fn.  In the case of rejection by the user, the usb_permission_request_allowed boolean prevents the request from being repeated.  This bool gets reset to true if the user disconnects the board.
            if(usb_permission_request_allowed) {
                manager.requestPermission(device, mPermissionIntent);
                usb_permission_request_allowed = false;
            }
            return device_info;
        }

        connection = manager.openDevice(device);
        int file_descriptor;
        if(connection==null) {
            Log.d(TAG, "device connection fail");
            return device_info;
        } else {
            file_descriptor = connection.getFileDescriptor();
        }
        Log.d(TAG, "fd = " + file_descriptor);
        int bootloader_mode;
        if((VID==0x03eb) && (PID==0x2fe4)) {
            bootloader_mode = 1;
        } else {
            bootloader_mode = 0;
        }
        device_info.put("file_descriptor", file_descriptor);
        device_info.put("bootloader_mode", bootloader_mode);

        Log.d(TAG, "Returning...");
        return device_info;
    }

// doesn't work ideally at the moment because iso_polling_thread in
// usbcallhandler.cpp encounters LIBUSB_ERROR_NO_DEVICE before the broadcast
// receiver below shuts down the thread.  Then, usbcallhandler.cpp flushes out
// all remaining iso transfers that it had previously submitted.  After that
// finishes, this broadcast receiver fires and does some more cleanup of the
// iso thread and also deletes the libusb handle on the Labrador board.
    BroadcastReceiver myUsbDetachBroadcastReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            Log.d(TAG,"detach detector reached");
            String action = intent.getAction();
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {                
                UsbDevice device;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                  device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
                } else {
                  device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                }
                int VID = device.getVendorId();
                int PID = device.getProductId();
                if((VID==0x03eb) && (PID==0xba94)){
                    disconnectIfConnected();
                }
                if(connection != null) {
                    connection.close();
                }
            }
        }
    };

    // for APIs 35 and higher, the status bar is included in the main display, so it's necessary to account for it and avoid it.  Otherwise, return 0;
    public int getStatusBarHeight()
    {
        int statusBarHeight;
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(getWindow().getDecorView());
            statusBarHeight = insets.getInsetsIgnoringVisibility(WindowInsetsCompat.Type.systemBars()).top;
        } else {
            statusBarHeight = 0;
        }
        return statusBarHeight;
    }
    // for APIs 35 and higher, the navigation bar is included in the main display, so it's necessary to account for it and avoid it.  Otherwise, return 0;
    public int getNavigationBarHeight()
    {
        int navigationBarHeight;
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(getWindow().getDecorView());
            navigationBarHeight = insets.getInsetsIgnoringVisibility(WindowInsetsCompat.Type.systemBars()).bottom;
        } else {
            navigationBarHeight = 0;
        }
        return navigationBarHeight;
    }

    public int getScreenWidth()
    {
        return getResources().getSystem().getDisplayMetrics().widthPixels;
    }

    public int getScreenHeight()
    {
        return getResources().getSystem().getDisplayMetrics().heightPixels;
    }

    public float getDpi()
    {
        return getResources().getSystem().getDisplayMetrics().xdpi;
    }


    // called by usbcallhandler.cpp
    public void requestFirmwareFlash()
    {
        AlertDialog alert = new AlertDialog.Builder(MainActivity.this)
             .setMessage("A firmware update is required.  This process will produce several USB permission requests.  Please accept them all.")
             .setPositiveButton("OK", new DialogInterface.OnClickListener()
                        {
                            @Override
                            public void onClick(DialogInterface dialog, int id)
                            {
                                nativeInitiateFirmwareFlash();
                            }
                        } 
                        )
             .setCancelable(false)
             .create();
        alert.show();
    }

    // called by usbcallhandler.cpp
    public void confirmFirmwareFlash()
    {
        AlertDialog alert = new AlertDialog.Builder(MainActivity.this)
             .setMessage("New firmware has been successfully uploaded to the Labrador board.")
             .setPositiveButton("OK", new DialogInterface.OnClickListener()
                        {
                            @Override
                            public void onClick(DialogInterface dialog, int id)
                            {
                            }
                        } 
                        )
             .setCancelable(false)
             .create();
        alert.show();
    }

    public void disconnectIfConnected() 
    {
        nativeRespondToStartupOrUsbStateChange(false, -1, false);
        if(connection != null) {
            connection.close();
        }
        usb_permission_request_allowed = true;
    }

//     public int openFile(String filename) {
//         File file = new File(filename);
//         ParcelFileDescriptor filePfd;
// //         DocumentFile file;
//         filePfd = getContentResolver().openAssetFileDescriptor(file.toURI(), "w");
//         int fd = filePfd.getFd();
// 
// 
// 
//     }
// 
    public String initFile(String filename) {
        File file = new File(filename);
        try{
            file.createNewFile();
        }
        catch(IOException e){
            e.printStackTrace();
        }
        file.setReadable(true, false);
        file.setWritable(true, false);
        return file.toURI().toString();
    }

    public String getDocsDir() {
        String docs_dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS) + "/Labrador";
        File docs_dir_struct = new File(docs_dir);
        docs_dir_struct.mkdir();
        return docs_dir;
    }

    public void scanFile(String filepath) {
    MediaScannerConnection
        .scanFile(MainActivity.getContext(), new String[] {filepath},
                  new String[] {"text/plain"}, null);
    }
}
