package com.gnychis.ubertooth.Core;

import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

import android.os.Handler;
import android.os.Message;
import android.util.Log;

import com.gnychis.ubertooth.UbertoothMain;
import com.gnychis.ubertooth.UbertoothMain.ThreadMessages;
import com.gnychis.ubertooth.DeviceHandlers.UbertoothOne;
import com.stericson.RootTools.RootTools;

/**
 * This class accesses native libusb and libusb-compat functions with the help
 * of native JNI functions in jni/usbhelper. The main purpose is to periodically
 * poll for a list of USB devices connected, and check if any of the devices are
 * an Ubertooth (specifically, only support for Ubertooth One).
 * 
 * @author Philipp
 * 
 */
public class USBMon {
	private static boolean VERBOSE = false;

	UbertoothMain _mainActivity;
	private Handler _handler;
	private static int USB_POLL_TIME = 1000; // in milliseconds, poll time

	private Timer _scan_timer;

	public USBMon(UbertoothMain c, Handler h) {
		_mainActivity = c;
		_handler = h;
		_scan_timer = null;
		_mainActivity.sendToastMessage(_handler, initUSB());
		startUSBMon();
	}

	private void debugOut(String msg) {
		if (VERBOSE)
			Log.d("USBMon", msg);
	}

	/**
	 * Schedule a periodic timer with an interval of USB_POLL_TIME and when it
	 * fires we poll with the list of connected USB devices.
	 * 
	 * @return boolean, true if timer was started successfully, false else
	 */
	public boolean startUSBMon() {
		if (_scan_timer != null)
			return false;
		_scan_timer = new Timer();
		_scan_timer.schedule(new TimerTask() {
			@Override
			public void run() {
				usbPoll();
			}

		}, 0, USB_POLL_TIME);
		return true;
	}

	/**
	 * Stops USB polling
	 * @return
	 */
	public boolean stopUSBMon() {
		if (_scan_timer == null)
			return false;

		_scan_timer.cancel();
		_scan_timer = null;
		return true;
	}

	/**
	 * Perform usb polling for a device
	 */
	public void usbPoll() {
		int ubertooth_in_devlist = USBcheckForDevice(0x1D50, 0x6002);
		// int ubertooth_in_devlist = USBcheckForDevice(0xffff, 0x0004);
		// String msg = "usbPoll returned " + ubertooth_in_devlist;
		// Log.d("USBMon", msg);
		// USBList();

		// Check if the Ubertooth device has just been connected, or
		// disconnected.
		if (ubertooth_in_devlist == 1
				&& _mainActivity.ubertooth._device_connected == false)
			updateState(UbertoothOne.UBERTOOTH_CONNECT);
		else if (ubertooth_in_devlist == 0
				&& _mainActivity.ubertooth._device_connected == true)
			updateState(UbertoothOne.UBERTOOTH_DISCONNECT);
	}

	/**
	 * Method for detecting if a change in state happens, notifies the main
	 * activity which can perform an action
	 * 
	 * @param event
	 */
	protected void updateState(int event) {
		Message msg = new Message();
		if (event == UbertoothOne.UBERTOOTH_CONNECT) {
			msg.what = ThreadMessages.UBERTOOTH_CONNECTED.ordinal();
			debugOut("got update that Ubertooth device was connected");
		} else if (event == UbertoothOne.UBERTOOTH_DISCONNECT) {
			msg.what = ThreadMessages.UBERTOOTH_DISCONNECTED.ordinal();
			debugOut("Ubertooth device now disconnected");
		}
		_mainActivity._handler.sendMessage(msg);
	}

	public native void USBList();

	public native int USBcheckForDevice(int vid, int pid);

	public native String initUSB();
}
