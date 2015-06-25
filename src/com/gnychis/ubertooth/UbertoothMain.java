package com.gnychis.ubertooth;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

import android.app.Activity;
import android.app.ProgressDialog;
import android.content.Intent;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.text.method.ScrollingMovementMethod;
import android.util.Log;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import com.gnychis.ubertooth.Core.USBMon;
import com.gnychis.ubertooth.DeviceHandlers.UbertoothOne;
import com.gnychis.ubertooth.Interfaces.GraphSpectrum;
import com.gnychis.ubertooth.Interfaces.IChart;
import com.stericson.RootTools.RootTools;

/**
 * Central Activity for accessing Ubertooth One and it's helper functions
 * 
 * @author Philipp
 * 
 */
public class UbertoothMain extends Activity implements OnClickListener {

	public UbertoothOne ubertooth;
	protected USBMon usbmon;
	public Button buttonScanSpectrum;
	public Button resetDevice;
	public Button btnScan_start_LAP;
	public Button btnScan_stop_LAP;
	public Button btnScan_start_BTLE;
	public Button btnScan_stop_BTLE;
	public Button btnFile_start_BTLE;
	public Button btnFile_stop_BTLE;
	public EditText edit_filename_BTLE;
	public IChart graphSpectrum;
	static UbertoothMain _this;
	public ArrayList<Integer> _scan_result;
	static TextView tv_results;
	ProgressBar pBar;
	private static final String TAG = "UbertoothMainDev";

	public BlockingQueue<String> toastMessages;
	private ProgressDialog pd;

	// A few message types that are used to pass information between several
	// threads.
	// This, for example, allows a thread handling the Ubertooth device to
	// report a finished
	// or failed scan.
	public enum ThreadMessages {
		UBERTOOTH_CONNECTED, UBERTOOTH_DISCONNECTED, UBERTOOTH_INITIALIZED, UBERTOOTH_FAILED, UBERTOOTH_SCAN_COMPLETE, UBERTOOTH_SCAN_FAILED, UBERTOOTH_RX_LAP_STARTED, UBERTOOTH_RX_LAP_RESULT, UBERTOOTH_RX_LAP_STOPPED, UBERTOOTH_RX_BTLE_STARTED, UBERTOOTH_RX_BTLE_RESULT, UBERTOOTH_RX_BTLE_STOPPED, SHOW_TOAST,
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_ubertooth_main);

		// Install a cross-compiled version of ubertooth-util which we run to
		// retrieve the firmware version on the Ubertooth. This is nothing more
		// than to demonstrate the possibility. This is build using ndk-build
		// and is in jni/ubertooth/Android.mk. You can easily modify it to
		// cross-compile any of the other tools.
		// Note that this Android application does *not* cross-compile and run
		// ubertooth-specan. Instead, it communicates directly with the
		// Ubertooth
		// and uses native code to initiate the scan and get the results. This
		// demonstrates direct access to the device without the need of relying
		// on
		// external applications.
		RootTools.remount("/system/", "rw");
		RootTools.installBinary(this, R.raw.ubertooth_util, "ubertooth_util");
		RootTools.installBinary(this, R.raw.ubertooth_rx, "ubertooth_rx");
		RootTools.installBinary(this, R.raw.ubertooth_btle, "ubertooth_btle");
		RootTools.installBinary(this, R.raw.link_libraries,
				"link_libraries.sh", "755");

		try { // Load several libraries. These are all built in jni/ with
				// ndk-build
			System.loadLibrary("usb");
			System.loadLibrary("usb-compat");
			System.loadLibrary("usbhelper"); // provides native calls to check
												// for USB changes
			System.loadLibrary("btbb"); // need to include the cross-compiled
										// version of btbb
			System.loadLibrary("ubertooth"); // this is a "helper" library to
												// allow native Ubertooth access
		} catch (Exception e) {
			Log.e("UbertoothMain",
					"error trying to load a USB related library", e);
		}
		UbertoothMain
				.runCommand("sh /data/data/com.gnychis.ubertooth/files/link_libraries.sh com.gnychis.ubertooth");

		_this = this; // Save an instance to this class

		toastMessages = new ArrayBlockingQueue<String>(20); // Used for toast
															// messages
		graphSpectrum = new GraphSpectrum(this); // Used to graph the power in
													// the spectrum

		// Setup a button to click which initiates the spectrum scan. Disable it
		// until the Ubertooth is plugged in
		buttonScanSpectrum = (Button) findViewById(R.id.buttonScan);
		buttonScanSpectrum.setOnClickListener(this);
		buttonScanSpectrum.setEnabled(false);

		buttonScanSpectrum = (Button) findViewById(R.id.button_resetDevice);
		buttonScanSpectrum.setOnClickListener(this);
		buttonScanSpectrum.setEnabled(false);

		btnScan_start_LAP = (Button) findViewById(R.id.button_start_rxLAP);
		btnScan_start_LAP.setOnClickListener(this);
		btnScan_start_LAP.setEnabled(false);

		btnScan_stop_LAP = (Button) findViewById(R.id.button_stop_rxLAP);
		btnScan_stop_LAP.setOnClickListener(this);
		btnScan_stop_LAP.setEnabled(false);

		btnScan_start_BTLE = (Button) findViewById(R.id.button_start_btle);
		btnScan_start_BTLE.setOnClickListener(this);
		btnScan_start_BTLE.setEnabled(false);

		btnScan_stop_BTLE = (Button) findViewById(R.id.button_stop_btle);
		btnScan_stop_BTLE.setOnClickListener(this);
		btnScan_stop_BTLE.setEnabled(false);

		btnFile_start_BTLE = (Button) findViewById(R.id.button_file_btle_start);
		btnFile_start_BTLE.setOnClickListener(this);
		btnFile_start_BTLE.setEnabled(false);

		btnFile_stop_BTLE = (Button) findViewById(R.id.button_file_btle_stop);
		btnFile_stop_BTLE.setOnClickListener(this);
		btnFile_stop_BTLE.setEnabled(false);

		edit_filename_BTLE = (EditText) findViewById(R.id.editText_filename);
		tv_results = (TextView) findViewById(R.id.tv_result);
		tv_results.setMovementMethod(new ScrollingMovementMethod());
		pBar = (ProgressBar) findViewById(R.id.progressBar1);

		// Instantiate the Ubertooth and the USB monitor (which checks for plugs
		// of the device)
		ubertooth = new UbertoothOne(this); // Instantiate the UbertoothOne
		usbmon = new USBMon(this, _handler); // Start the USB handler
	}

	@Override
	public void onDestroy() {
		try {
			usbmon.stopUSBMon();
			ubertooth.stopUbertooth();
		} catch (Exception e) {

		}

		super.onDestroy();
		return;
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		getMenuInflater().inflate(R.menu.activity_ubertooth_main, menu);
		return true;
	}

	/**
	 * Handler Declaration
	 */
	public Handler _handler = new Handler() {
		@Override
		public void handleMessage(Message msg) {

			// /////////////////////////////////////////////////////////////////////
			// A set of messages referring to scans goes to the scan class
			// if(msg.obj == ThreadMessages.NETWORK_SCANS_COMPLETE)
			// networkScansComplete();

			// /////////////////////////////////////////////////////////////////////
			// A set of messages that that deal with hardware connections
			if (msg.what == ThreadMessages.UBERTOOTH_CONNECTED.ordinal()) {
				ubertoothSettling();
			}

			// The main activity has received a message that the ubertooth has
			// been initialited.
			// We can initiate the scan button.
			if (msg.what == ThreadMessages.UBERTOOTH_INITIALIZED.ordinal()) {
				pd.dismiss();
				Toast.makeText(
						getApplicationContext(),
						"Successfully initialized Ubertooth One device ("
								+ ubertooth._firmware_version + ")",
						Toast.LENGTH_LONG).show();
				usbmon.startUSBMon();
				buttonScanSpectrum.setEnabled(true);
				btnScan_start_LAP.setEnabled(true);
				btnScan_start_BTLE.setEnabled(true);
				btnFile_start_BTLE.setEnabled(true);
			}

			// Failed to initialize the ubertooth device
			if (msg.what == ThreadMessages.UBERTOOTH_FAILED.ordinal()) {
				pd.dismiss();
				usbmon.startUSBMon();
				Toast.makeText(getApplicationContext(),
						"Failed to initialize Ubertooth One device",
						Toast.LENGTH_LONG).show();
			}

			// The scan failed (never had this happen, but...)
			if (msg.what == ThreadMessages.UBERTOOTH_SCAN_FAILED.ordinal()) {
				pd.dismiss();
				usbmon.startUSBMon();
				Toast.makeText(getApplicationContext(),
						"Failed to initialize scan on the Ubertooth",
						Toast.LENGTH_LONG).show();
			}

			// A message that specifies the scan was complete. We save the scan
			// result and start
			// the activity to graph the result.
			if (msg.what == ThreadMessages.UBERTOOTH_SCAN_COMPLETE.ordinal()) {
				usbmon.startUSBMon();
				_scan_result = (ArrayList<Integer>) msg.obj;
				Intent i = graphSpectrum.execute(_this);
				startActivity(i);
				pd.dismiss();
			}
			if (msg.what == ThreadMessages.UBERTOOTH_RX_LAP_STARTED.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_LAP_STARTED received");
				// clear screen
				pBar.setProgress(0);
				tv_results.setText("");

				pd.dismiss(); // remove progress dialog
			}

			if (msg.what == ThreadMessages.UBERTOOTH_RX_BTLE_STARTED.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_BTLE_STARTED received");
				// clear screen
				pBar.setProgress(0);
				tv_results.setText("");

				pd.dismiss(); // remove progress dialog
			}

			if (msg.what == ThreadMessages.UBERTOOTH_RX_LAP_RESULT.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_LAP_RESULT received");
				String new_LAP = (String) msg.obj + "\n";
				tv_results.append(new_LAP);

				int index = new_LAP.indexOf("s=-");
				if (index > 0) {
					String substring = new_LAP.substring(index + 3);

					substring = substring.replaceAll("[^0-9]+", "");
					/*
					 * substring = substring.replaceAll( "[^\\d]", "" );
					 * substring = substring.replaceAll( "\\\\n", "" );
					 * substring = substring.replaceAll( "\n", "" ); substring =
					 * substring.replaceAll( "\\n", "" );
					 */int i = Integer.parseInt(substring);
					pBar.setProgress(i);
				}

				if (tv_results.getLineCount() > 256)
					tv_results.setText("");

				// Erase excessive lines: cost too much time...
				/*
				 * int excessLineNumber = tv.getLineCount() - 50; if
				 * (excessLineNumber > 0) { int eolIndex = -1; CharSequence
				 * charSequence = tv.getText(); for(int i=0; i<excessLineNumber;
				 * i++) { do { eolIndex++; } while(eolIndex <
				 * charSequence.length() && charSequence.charAt(eolIndex) !=
				 * '\n'); } if (eolIndex < charSequence.length()) {
				 * tv.getEditableText().delete(0, eolIndex+1); } else {
				 * tv.setText(""); } }
				 */
			}

			if (msg.what == ThreadMessages.UBERTOOTH_RX_BTLE_RESULT.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_BTLE_RESULT received");
				String new_BTLE = (String) msg.obj + "\n";
				tv_results.append(new_BTLE);

				int index = new_BTLE.indexOf("s=-");
				if (index > 0) {
					String substring = new_BTLE.substring(index + 3);

					substring = substring.replaceAll("[^0-9]+", "");
					/*
					 * substring = substring.replaceAll( "[^\\d]", "" );
					 * substring = substring.replaceAll( "\\\\n", "" );
					 * substring = substring.replaceAll( "\n", "" ); substring =
					 * substring.replaceAll( "\\n", "" );
					 */int i = Integer.parseInt(substring);
					pBar.setProgress(i);
				}

				if (tv_results.getLineCount() > 256)
					tv_results.setText("");
			}

			if (msg.what == ThreadMessages.UBERTOOTH_RX_LAP_STOPPED.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_LAP_STOPPED received");
				usbmon.startUSBMon();
				btnScan_start_LAP.setEnabled(true);
				buttonScanSpectrum.setEnabled(true);
				btnScan_start_BTLE.setEnabled(true);
				// _scan_result = (ArrayList<Integer>)msg.obj;
				// Intent i = graphSpectrum.execute(_this);
				// startActivity(i);
				pd.dismiss();
			}

			if (msg.what == ThreadMessages.UBERTOOTH_RX_BTLE_STOPPED.ordinal()) {
				Log.d("UbertoothMain", "UBERTOOTH_RX_BTLE_STOPPED received");
				usbmon.startUSBMon();
				btnScan_start_LAP.setEnabled(true);
				buttonScanSpectrum.setEnabled(true);
				btnScan_start_BTLE.setEnabled(true);
				pd.dismiss();
			}

			// The Ubertooth device has been disconnected. We call
			// .disconnected() which also disables
			// the scan button.
			if (msg.what == ThreadMessages.UBERTOOTH_DISCONNECTED.ordinal()) {
				Toast.makeText(getApplicationContext(),
						"Ubertooth device has been disconnected",
						Toast.LENGTH_LONG).show();
				ubertooth.disconnected();
			}

			// /////////////////////////////////////////////////////////////////////
			// A set of messages that that deal with hardware connections
			if (msg.what == ThreadMessages.SHOW_TOAST.ordinal()) {
				try {
					String m = toastMessages.remove();
					Toast.makeText(getApplicationContext(), m,
							Toast.LENGTH_LONG).show();
				} catch (Exception e) {
				}
			}
		}
	};

	/**
	 * When we get a click to scan the spectrum, we disable the USB monitor (so
	 * it doesn't do a periodic poll while we're scanning), and starts the
	 * actual spectrum scan.
	 */
	public void onClick(View view) {
		if (view.getId() == R.id.buttonScan) {
			pd = new ProgressDialog(this);
			pd.setCancelable(false);
			pd.setMessage("Scanning spectrum with Ubertooth...");
			pd.show();
			usbmon.stopUSBMon();
			ubertooth.scanStart();
			buttonScanSpectrum.setEnabled(false);

		} else if (view.getId() == R.id.button_resetDevice) {
			// String result = UbertoothMain.runCommand(
			// "/data/data/com.gnychis.ubertooth/files/ubertooth_util -r")
			// .get(0);
			// Log.d(TAG, "Reset-device result: " + result);
			ubertooth.resetUbertooth();
			// Toast.makeText(getApplicationContext(), result,
			// Toast.LENGTH_LONG)
			// .show();

		} else if (view.getId() == R.id.button_start_rxLAP) {
			pd = new ProgressDialog(this);
			pd.setCancelable(false);
			pd.setMessage("Scanning LAP's with Ubertooth...");
			pd.show();
			usbmon.stopUSBMon();
			ubertooth.rxLAP_Start();
			buttonScanSpectrum.setEnabled(false);
			btnScan_start_LAP.setEnabled(false);
			btnScan_stop_LAP.setEnabled(true);

		} else if (view.getId() == R.id.button_stop_rxLAP) {
			pd = new ProgressDialog(this);
			pd.setCancelable(false);
			pd.setMessage("Stopping scanning...");
			pd.show();
			ubertooth.rxLAP_Stop();
			btnScan_stop_LAP.setEnabled(false);

		} else if (view.getId() == R.id.button_start_btle) {
			pd = new ProgressDialog(this);
			pd.setCancelable(true);
			pd.setMessage("Scanning Btle Packets with Ubertooth...");
			pd.show();
			usbmon.stopUSBMon();
			// TODO: fix this wrong filename
			ubertooth.rxBTLE_Start("test");
			buttonScanSpectrum.setEnabled(false);
			btnScan_start_BTLE.setEnabled(false);
			btnScan_stop_BTLE.setEnabled(true);

		} else if (view.getId() == R.id.button_stop_btle) {
			pd = new ProgressDialog(this);
			pd.setCancelable(false);
			pd.setMessage("Stopping Btle scanning...");
			pd.show();
			ubertooth.rxBTLE_Stop();
			btnScan_stop_BTLE.setEnabled(false);
		} else if (view.getId() == R.id.button_file_btle_start) {
			String filename = "";
			Calendar c = Calendar.getInstance();
			SimpleDateFormat sdf = new SimpleDateFormat("yyyyMMddHHmm");
			String strDate = sdf.format(c.getTime());
			btnFile_stop_BTLE.setEnabled(true);
			btnFile_start_BTLE.setEnabled(false);
			if (edit_filename_BTLE.getText().equals("Insert filename")
					|| edit_filename_BTLE.getText().equals("")) {
				filename = strDate;
			} else {
				filename = edit_filename_BTLE.getText().toString();
			}
			if (filename.equals("")) {
				filename = strDate;
			}
			String storagePath = Environment.getExternalStorageDirectory()
					.getAbsolutePath();
			// Log.i(TAG, "File path: " + storagePath + "/" + filename +
			// ".pcap");
			ubertooth.rxBTLE_Start("/sdcard/" + filename + ".pcap");

			// try {
			// //List<String> res = RootTools.sendShell((new
			// String[]{"/data/data/com.gnychis.ubertooth/files/ubertooth_btle",
			// "-f", "-c" + filename + ".pcap"}),0,0);
			// UbertoothMain.runCommand(
			// //"/data/data/com.gnychis.ubertooth/files/ubertooth_btle -f -c "
			// + filename + ".pcap");
			// "/data/data/com.gnychis.ubertooth/files/ubertooth_rx");
			// } catch (Exception e) {
			// // TODO Auto-generated catch block
			// Log.e(TAG, "ERROR: Send BTLE file command went wrong");
			// e.printStackTrace();
			// }
			// Log.d(TAG, "Start Btle file result: " + result);
			// Toast.makeText(getApplicationContext(), result,
			// Toast.LENGTH_LONG)
			// .show();
		} else if (view.getId() == R.id.button_file_btle_stop) {
			btnFile_start_BTLE.setEnabled(true);
			btnFile_stop_BTLE.setEnabled(false);

			ubertooth.rxBTLE_Stop();
			// try {
			// RootTools.sendShell(
			// "^C", 0);
			// } catch (Exception e) {
			// // TODO Auto-generated catch block
			// Log.e(TAG, "ERROR: Send BTLE file command went wrong");
			// e.printStackTrace();
			// }

		}

	}

	/**
	 * Method for setting Ubertooth
	 */
	public void ubertoothSettling() {
		pd = ProgressDialog.show(this, "",
				"Initializing Ubertooth One device...", true, false);
		usbmon.stopUSBMon();
		ubertooth.connected();
	}

	/*
	 * //static call from JNI to JAVA....skip AsyncTask and other
	 * class-instances. public static void SendLapResult(String result){ Message
	 * msg = new Message(); msg.what =
	 * ThreadMessages.UBERTOOTH_RX_LAP_RESULT.ordinal(); msg.obj = result;
	 * _this._handler.handleMessage(msg); }
	 */

	/**
	 * Display some toast messages if needed
	 * 
	 * @param h
	 *            , handler
	 * @param msg
	 *            , the message to display
	 */
	public void sendToastMessage(Handler h, String msg) {
		try {
			toastMessages.put(msg);
			Message m = new Message();
			m.what = ThreadMessages.SHOW_TOAST.ordinal();
			h.sendMessage(m);
		} catch (Exception e) {
			Log.e("UbertoothMain",
					"Exception trying to put toast msg in queue:", e);
		}
	}

	/**
	 * This method is used to get the Android application's username (e.g.,
	 * app_115). and to chown the Ubertooth device in /dev/ so that the
	 * application can natively access the device. Otherwise, it will get an
	 * access denied error (the device is default root).
	 * 
	 * @return
	 */
	public String getAppUser() {
		try {
			List<String> res = RootTools.sendShell(
					"ls -l /data/data | grep com.gnychis.ubertooth", 0);
			return res.get(0).split(" ")[1];
		} catch (Exception e) {
			return "FAIL";
		}
	}

	/**
	 * This is a helper function I wrote to run a command as root and get the
	 * resulting output from the shell. Mainly provided by RootTools.
	 * 
	 * @param c
	 *            string, command to execute
	 * @return the result as an array list
	 */
	public static ArrayList<String> runCommand(String c) {
		ArrayList<String> res = new ArrayList<String>();
		try {
			// First, run the command push the result to an ArrayList
			List<String> res_list = RootTools.sendShell(c, 0);
			Iterator<String> it = res_list.iterator();
			while (it.hasNext())
				res.add((String) it.next());
			res.remove(res.size() - 1);

			// Trim the ArrayList of an extra blank lines at the end
			while (true) {
				int index = res.size() - 1;
				if (index >= 0 && res.get(index).length() == 0)
					res.remove(index);
				else
					break;
			}

			// log result
			if (res != null && res.size() > 0) {
				for (String t : res) {
					if (t != null) {
						Log.i("LOG_OUTPUT_COMMAND_SHELL", t);
					}
				}
			}

			return res;

		} catch (Exception e) {
			Log.e("WifiDev", "error writing to RootTools the command: " + c, e);
			return null;
		}
	}
	//
	// // This is a helper function to run a command as root and get the
	// // resulting
	// // output from the shell. Mainly provided by RootTools.
	// public static ArrayList<String> runCommand(String c) {
	// ArrayList<String> res = new ArrayList<String>();
	// try {
	// // First, run the command push the result to an ArrayList
	// List<String> res_list = RootTools.sendShell(c, 0);
	// Iterator<String> it = res_list.iterator();
	// String temp = "";
	// int counter = 0;
	// while (it.hasNext() && counter < 300){
	// temp = (String) it.next();
	// res.add(temp);
	// }
	// res.remove(res.size() - 1);
	//
	// Log.i("LOG_OUTPUT_COUNTER", String.valueOf(counter));
	//
	// // Trim the ArrayList of an extra blank lines at the end
	// while (true) {
	// int index = res.size() - 1;
	// if (index >= 0 && res.get(index).length() == 0)
	// res.remove(index);
	// else
	// break;
	// }
	//
	// for(String t : res){
	// if(!t.equals("")){
	// Log.i("LOG_OUTPUT_ROOT", t);
	// }
	// }
	//
	// return res;
	//
	// } catch (Exception e) {
	// Log.e("WifiDev", "error writing to RootTools the command: " + c, e);
	// return null;
	// }
	// }
}
