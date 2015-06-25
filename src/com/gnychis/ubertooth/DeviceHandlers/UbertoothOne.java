package com.gnychis.ubertooth.DeviceHandlers;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeoutException;

import android.content.Context;
import android.os.AsyncTask;
import android.os.Message;
import android.util.Log;
import android.widget.Toast;

import com.gnychis.ubertooth.UbertoothMain;
import com.gnychis.ubertooth.UbertoothMain.ThreadMessages;
import com.stericson.RootTools.RootTools;
import com.stericson.RootTools.RootToolsException;

/**
 * 
 This class accesses native JNI methods that are wrappers around some
 * functions in ubertooth.c, ultimately accessing the Ubertooth natively. There
 * is a helper JNI library called 'ubertooth' and built by
 * jni/ubertooth/ubertooth_helper. Others could build additional native JNI
 * access to ubertooth functions by placing them in ubertooth_helper.
 */
public class UbertoothOne {
	private static final String TAG = "UbertoothOneDev";
	private static final boolean VERBOSE = true;

	public static final int BT_LOW_FREQ = 2402;
	public static final int BT_HIGH_FREQ = 2480;

	public static final int UBERTOOTH_CONNECT = 400;
	public static final int UBERTOOTH_DISCONNECT = 401;
	public static final String UBERTOOTH_SCAN_RESULT = "com.gnychis.coexisyst.UBERTOOTH_SCAN_RESULT";
	public static final int SWEEPS_IN_MAX = 200;

	UbertoothMain _mainActivity; // Keep the instance of the main activity
	public String _firmware_version; // Just for asthetics, keep the firmware
										// version

	UbertoothOneScan _scan_thread; // We use a separate thread for scans so they
									// don't block main activity
	UbertoothOne_rxLAP _rx_LAP_thread; // We use a separate thread for scans so
										// they don't block main activity
	UbertoothOne_rxBTLE _rx_Btle_thread; // btle thread
	public boolean _device_connected; // Simple bool to keep track if the
										// Ubertooth is currently connected
	ArrayList<Integer> _scan_result; // Keep track of the last scan result

	public UbertoothOne(UbertoothMain c) {
		_mainActivity = c;
	}

	public boolean isConnected() {
		return _device_connected;
	}

	/**
	 * If we are notified that the device is connected, we execute a thread
	 * which actually initializes the Ubertooth USB device. Again, so we don't
	 * block the main activity. It sends a notification when initialized, or
	 * when the initialization fails.
	 */
	public void connected() {
		_device_connected = true;
		UbertoothOneInit wsi = new UbertoothOneInit();
		wsi.execute(_mainActivity);
	}

	/**
	 * When disconnected, we can disable the scan button.
	 */
	public void disconnected() {
		_mainActivity.buttonScanSpectrum.setEnabled(false);
		_device_connected = false;
	}

	/**
	 * This is a thread that will initialize the Ubertooth device by calling a
	 * native JNI library helper function called startUbertooth(). If the
	 * initialization fails, we will be notified of it.
	 * 
	 * @author Philipp
	 */
	protected class UbertoothOneInit extends
			AsyncTask<Context, Integer, String> {
		Context parent;
		UbertoothMain mainActivity;

		// Used to send messages to the main Activity (UI) thread
		protected void sendMainMessage(UbertoothMain.ThreadMessages t,
				Object obj) {
			Message msg = new Message();
			msg.what = t.ordinal();
			msg.obj = obj;
			mainActivity._handler.sendMessage(msg);
		}

		@Override
		protected String doInBackground(Context... params) {
			parent = params[0];
			mainActivity = (UbertoothMain) params[0];

			// To use the Ubertooth device, we need to give the USB device the
			// application's permissions.
			// Otherwise, it is limited to root and the application cannot
			// natively access the /dev handle.
			UbertoothMain.runCommand("find /dev/bus -exec chown "
					+ mainActivity.getAppUser() + " {} \\;");
			// Get the firmware version for fun and demonstration
			// _firmware_version =
			// UbertoothMain.runCommand("/data/data/com.gnychis.ubertooth/files/ubertooth_util -v").get(0);
			// Log.d(TAG, "Ubertooth firmware version: " + _firmware_version);

			// Get the firmware version for fun and demonstration
			List<String> res = null;
			try {
				res = RootTools
						.sendShell(
								(new String[] {
										"/data/data/com.gnychis.ubertooth/files/ubertooth_util",
										"-v" }), 0, 0);
			} catch (Exception e) {
				// TODO Auto-generated catch block
				Log.e(TAG, "Error executing shell command");
				e.printStackTrace();
			}
			_firmware_version = res.get(0);

			// _firmware_version =
			// UbertoothMain.runCommand("/data/data/com.gnychis.ubertooth/files/ubertooth_util -v").get(0);
			Log.d(TAG, "Ubertooth firmware version: " + _firmware_version);
			_scan_result = new ArrayList<Integer>();

			// Try to initialize the Ubertooth One
			if (startUbertooth() == 1)
				sendMainMessage(ThreadMessages.UBERTOOTH_INITIALIZED, null);
			else
				sendMainMessage(ThreadMessages.UBERTOOTH_FAILED, null);

			return "OK";
		}
	}

	/**
	 * This starts the scan thread, passing the main activity and beginning the
	 * spectrum scan.
	 * 
	 * @return boolean, if scan was started
	 */
	public boolean scanStart() {
		_scan_result.clear();
		_scan_thread = new UbertoothOneScan();
		_scan_thread.execute(_mainActivity);
		return true; // in scanning state, and channel hopping
	}

	/**
	 * Starts the scan thread, passing the main activity and begin receiving
	 * LAP's
	 * 
	 * @return boolean
	 */
	public boolean rxLAP_Start() {
		_rx_LAP_thread = new UbertoothOne_rxLAP();
		// save object in C-side as global object
		SaveGlobalObject(_rx_LAP_thread);
		_rx_LAP_thread.execute(_mainActivity);
		return true;
	}

	/**
	 * Stops the rxLAP process
	 * 
	 * @return boolean
	 */
	public boolean rxLAP_Stop() {
		StopRxLAP();
		return true;
	}

	// This starts the scan thread, passing the main activity and begin
	// receiving Btle packets
	public boolean rxBTLE_Start(String filename) {
		_rx_Btle_thread = new UbertoothOne_rxBTLE(filename);
		// save object in C-side as global object
		SaveGlobalObject(_rx_Btle_thread);
		_rx_Btle_thread.execute(_mainActivity);
		return true;
	}

	// This signals the scan-thread to stop
	public boolean rxBTLE_Stop() {
		StopRxBTLE();
		return true;
	}

	/**
	 * This is a thread to perform the actual scan (blocking and waiting for
	 * it), rather than blocking the main activity. When it is complete, it
	 * sends the results to the main activity.
	 * 
	 * @author Philipp
	 * 
	 */
	protected class UbertoothOneScan extends
			AsyncTask<Context, Integer, String> {
		Context parent;
		UbertoothMain mainActivity;

		// Used to send messages to the main Activity (UI) thread
		protected void sendMainMessage(UbertoothMain.ThreadMessages t,
				Object obj) {
			Message msg = new Message();
			msg.what = t.ordinal();
			msg.obj = obj;
			mainActivity._handler.sendMessage(msg);
		}

		@Override
		protected String doInBackground(Context... params) {
			parent = params[0];
			mainActivity = (UbertoothMain) params[0];

			// Perform the scan, specify the low and high freqs as well as
			// the number of sweeps to perform (this is a "max hold").
			int[] scan_res = scanSpectrum(BT_LOW_FREQ, BT_HIGH_FREQ,
					SWEEPS_IN_MAX);

			if (scan_res == null) {
				sendMainMessage(ThreadMessages.UBERTOOTH_SCAN_FAILED, null);
				return "NOPE";
			}

			_scan_result = new ArrayList<Integer>();
			for (int i = 0; i < scan_res.length; i++)
				_scan_result.add(scan_res[i]);

			sendMainMessage(ThreadMessages.UBERTOOTH_SCAN_COMPLETE,
					_scan_result);

			return "PASS";
		}

	}

	/**
	 * This is a thread to perform the actual scan (blocking and waiting for
	 * it), rather than blocking the main activity. When it is complete, it
	 * sends the results to the main activity.
	 * 
	 * @author Philipp
	 * 
	 */
	protected class UbertoothOne_rxLAP extends
			AsyncTask<Context, Integer, String> {
		Context parent;
		UbertoothMain mainActivity;

		// Used to send messages to the main Activity (UI) thread
		protected void sendMainMessage(UbertoothMain.ThreadMessages t,
				Object obj) {
			Message msg = new Message();
			msg.what = t.ordinal();
			msg.obj = obj;
			mainActivity._handler.sendMessage(msg);
		}

		@Override
		protected String doInBackground(Context... params) {
			parent = params[0];
			mainActivity = (UbertoothMain) params[0];

			sendMainMessage(ThreadMessages.UBERTOOTH_RX_LAP_STARTED, null);
			StartRxLAP();
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_LAP_STOPPED, null);
			// stopUbertooth();
			return "PASS";
		}

		public void messageLAPResult(String msg) {
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_LAP_RESULT, msg);
			return;
		}

		public void messageLAPResult2() {
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_LAP_RESULT, "test void");
			return;
		}

	}

	/**
	 * This is a thread to perform the actual scan (blocking and waiting for
	 * it), rather than blocking the main activity. When it is complete, it
	 * sends the results to the main activity.
	 * 
	 * @author Philipp
	 * 
	 */
	protected class UbertoothOne_rxBTLE extends
			AsyncTask<Context, Integer, String> {
		Context parent;
		UbertoothMain mainActivity;
		String filename;

		public UbertoothOne_rxBTLE(String _filename) {
			this.filename = _filename;
		}

		// Used to send messages to the main Activity (UI) thread
		protected void sendMainMessage(UbertoothMain.ThreadMessages t,
				Object obj) {
			Message msg = new Message();
			msg.what = t.ordinal();
			msg.obj = obj;
			mainActivity._handler.sendMessage(msg);
		}

		@Override
		protected String doInBackground(Context... params) {
			parent = params[0];
			mainActivity = (UbertoothMain) params[0];

			sendMainMessage(ThreadMessages.UBERTOOTH_RX_BTLE_STARTED, null);
			StartRxBTLE(filename);
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_BTLE_STOPPED, null);
			// stopUbertooth();
			return "PASS";
		}

		public void messageBtleResult(String msg) {
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_BTLE_RESULT, msg);
			return;
		}

		public void messageBtleResult2() {
			sendMainMessage(ThreadMessages.UBERTOOTH_RX_BTLE_RESULT,
					"test void");
			return;
		}

	}

	public native int startUbertooth();

	public native int stopUbertooth();

	public native int resetUbertooth();

	public native int SaveGlobalObject(Object obj);

	public native int StartRxLAP();

	public native int StopRxLAP();

	public native int btleToFile();

	public native int StartRxBTLE(String filePath);

	public native int StopRxBTLE();

	public native int[] scanSpectrum(int low_freq, int high_freq, int sweeps);
}
