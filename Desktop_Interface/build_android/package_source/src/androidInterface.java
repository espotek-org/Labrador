package com.EspoTek.Labrador.Java;

import org.qtproject.qt5.android.bindings.QtApplication;
import org.qtproject.qt5.android.bindings.QtActivity;

import android.util.Log;

import android.os.Build;

import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.hardware.usb.UsbDeviceConnection;
import android.content.Context;
import android.app.PendingIntent;
import android.content.Intent;
import java.util.HashMap;
import java.util.Iterator;
import java.lang.String;

public class androidInterface extends QtActivity
{
    private static final String ACTION_USB_PERMISSION = "org.qtproject.example.androidLibusb.USB_PERMISSION";

    public String usbfs_path;
    public int file_descriptor;
    private UsbDeviceConnection connection;

    public androidInterface()
    {
        file_descriptor = -69;
        Log.d(QtApplication.QtTAG, "An androidInterface has been constructed!");
    }

    public static void staticTest()
    {
        Log.d(QtApplication.QtTAG, "staticTest");
    }

    public void nonStaticTest()
    {
        Log.d(QtApplication.QtTAG, "nonStaticTest");
    }

    public void findDevice()
    {
        Log.d(QtApplication.QtTAG, "findDevice");
        PendingIntent mPermissionIntent;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), PendingIntent.FLAG_MUTABLE);  //???
        } else {
            mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);  //???
        }
        UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);  //Handle to system USB service?
        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        if(!deviceIterator.hasNext()){
            Log.d(QtApplication.QtTAG, "NO DEVICE FOUND");
            }

        while(deviceIterator.hasNext()){
            Log.d(QtApplication.QtTAG, "DEVICE FOUND");
            UsbDevice device = deviceIterator.next();

            manager.requestPermission(device, mPermissionIntent);
            //Wait until it gets the permission
            while(!manager.hasPermission(device)){
                ;
                }

            String Model = device.getDeviceName();

            int DeviceID = device.getDeviceId();
            int VID = device.getVendorId();
            int PID = device.getProductId();
            Log.d(QtApplication.QtTAG, String.format("Device ID = %d\nVID=0x%04x\nPID=0x%04x\n", DeviceID, VID, PID));
            if((VID==0x03eb) && (PID==0xba94)){
                if(!manager.hasPermission(device)){
                    Log.d(QtApplication.QtTAG, "permission was not granted to the USB device!!!");
                    return;
                }
                Log.d(QtApplication.QtTAG, "MATCH FOUND!");
                usbfs_path = device.getDeviceName();
                Log.d(QtApplication.QtTAG, "usbfs_path = " + usbfs_path);
                connection = manager.openDevice(device);
                file_descriptor = connection.getFileDescriptor();
                Log.d(QtApplication.QtTAG, "fd = " + file_descriptor);
                Log.d(QtApplication.QtTAG, "Returning...");
                return;
            } else if ((VID==0x03eb) && (PID==0x2fe4)) {
                Log.d(QtApplication.QtTAG, "Device found but it is in bootloader mode!");
                file_descriptor = -65;
                return;
            }
        }
    }
    public void findDevice_bootloader()
    {
        Log.d(QtApplication.QtTAG, "findDevice_bootloader");
        PendingIntent mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);  //???
        UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);  //Handle to system USB service?
        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        if(!deviceIterator.hasNext()){
            Log.d(QtApplication.QtTAG, "NO DEVICE FOUND");
            }

        while(deviceIterator.hasNext()){
            Log.d(QtApplication.QtTAG, "DEVICE FOUND");
            UsbDevice device = deviceIterator.next();

            manager.requestPermission(device, mPermissionIntent);
            //Wait until it gets the permission
            while(!manager.hasPermission(device)){
                ;
                }

            String Model = device.getDeviceName();

            int DeviceID = device.getDeviceId();
            int VID = device.getVendorId();
            int PID = device.getProductId();
            Log.d(QtApplication.QtTAG, String.format("Device ID = %d\nVID=0x%04x\nPID=0x%04x\n", DeviceID, VID, PID));
            if((VID==0x03eb) && (PID==0x2fe4)){
                if(!manager.hasPermission(device)){
                    Log.d(QtApplication.QtTAG, "permission was not granted to the USB device!!!");
                    return;
                    }
                Log.d(QtApplication.QtTAG, "MATCH FOUND!");
                usbfs_path = device.getDeviceName();
                Log.d(QtApplication.QtTAG, "usbfs_path = " + usbfs_path);
                connection = manager.openDevice(device);
                file_descriptor = connection.getFileDescriptor();
                Log.d(QtApplication.QtTAG, "fd = " + file_descriptor);
                Log.d(QtApplication.QtTAG, "Returning...");
                return;
                }
        }
    }
    public void closeDevice()
    {
        file_descriptor = -69;
        Log.d(QtApplication.QtTAG, "androidInterface has been closed!");
    }
}
