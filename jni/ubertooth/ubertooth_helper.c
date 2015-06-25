/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>
#include <jni.h>
#include <errno.h>
#include <stdbool.h>
#include <android/log.h>
#include "ubertooth.h"
#include "ubertooth_control.h"
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <stdlib.h>

#define LOG_TAG "UbertoothHelper" // text for log tag
extern char Quiet;
extern char Ubertooth_Device;
char ubertooth_device = -1;

const int RSSI_OFFSET = -54;

struct libusb_device_handle *devh = NULL;

static JavaVM* gJavaVM = NULL;
static jobject gJavaActivityClass;
static jobject gJavaActivityClassBtle;
static jmethodID gJMethodID;
static jmethodID gJMethodIDBtle;
static jobject gJavaObject;
static volatile bool rx_LAP_running = false;
static volatile bool rx_BTLE_running = false;

//LAP-variables
static uint32_t start_clk100ns = 0;
static uint64_t last_clk100ns = 0;
static uint64_t clk100ns_upper = 0;
static uint64_t abs_start_ns;

static usb_pkt_rx packets[NUM_BANKS];
static char symbols[NUM_BANKS][BANK_LEN];
static u8 *empty_buf = NULL;
static u8 *full_buf = NULL;
static volatile u8 really_full = 0;
static struct libusb_transfer *rx_xfer = NULL;
static int max_ac_errors = 1;
static uint32_t systime;

#define MAX(a,b) ((a)>(b) ? (a) : (b))
#define NUM_BREDR_CHANNELS 79

int main(int argc, char *argv[]) {
	return 1;
}

jint JNI_OnLoad(JavaVM* aVm, void* aReserved) {
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "JNI_OnLoad() called");

	// cache java VM
	gJavaVM = aVm;

	JNIEnv* env;
	if ((*aVm)->GetEnv(aVm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"Failed to get the environment");
		return -1;
	}
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "environment = %d",
			(int) *env);

	jclass activityClass =
			(*env)->FindClass(env,
					"com/gnychis/ubertooth/DeviceHandlers/UbertoothOne$UbertoothOne_rxLAP");
	if (!activityClass) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"failed to get class reference");
		return -1;
	}
	jclass activityClassBtle =
			(*env)->FindClass(env,
					"com/gnychis/ubertooth/DeviceHandlers/UbertoothOne$UbertoothOne_rxBTLE");
	if (!activityClassBtle) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"failed to get class reference");
		return -1;
	}

	gJavaActivityClass = (*env)->NewGlobalRef(env, activityClass);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "OK: class reference is %d",
			(int) gJavaActivityClass);

	//btle class reference
	gJavaActivityClassBtle = (*env)->NewGlobalRef(env, activityClassBtle);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "OK: class reference is %d",
			(int) gJavaActivityClassBtle);

	gJMethodID = (*env)->GetMethodID(env, gJavaActivityClass,
			"messageLAPResult", "(Ljava/lang/String;)V");
	//btle gJMethod
	gJMethodIDBtle = (*env)->GetMethodID(env, gJavaActivityClassBtle,
			"messageBtleResult", "(Ljava/lang/String;)V");
	//gJMethodID = (*env)->GetMethodID(env, gJavaActivityClass, "messageLAPResult2", "()V");
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"LAPResult(): local_method = %d", (int) gJMethodID);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"BTLEResult(): local_method = %d", (int) gJMethodIDBtle);
	if (gJMethodID == NULL) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"Can't find Java method void messageLAPResult()");
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"JNI_OnLoad() done with error");
		return JNI_ERR;
	}
	if (gJMethodIDBtle == NULL) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"Can't find Java method void messageBTLEResult()");
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"JNI_OnLoad() done with error");
		return JNI_ERR;
	}

	/*
	 * nog opruimen
	 (*env)->DeleteLocalRef(env, clazz);
	 */

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "JNI_OnLoad() done");
	return JNI_VERSION_1_6;
}

static void cb_xfer(struct libusb_transfer *xfer) {
	int r;
	uint8_t *tmp;

	if (xfer->status != LIBUSB_TRANSFER_COMPLETED) {
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "rx_xfer status: %d\n",
				xfer->status);
		libusb_free_transfer(xfer);
		rx_xfer = NULL;
		return;
	}

	while (really_full) {
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
				"uh oh, full_buf not emptied\n");
	}
	//__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "buffer is full !\n");

	tmp = full_buf;
	full_buf = empty_buf;
	empty_buf = tmp;
	really_full = 1;

	rx_xfer->buffer = empty_buf;

	while (1) {
		r = libusb_submit_transfer(rx_xfer);
		if (r < 0) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
					"rx_xfer submission from callback: %d\n", r);
		} else {
			break;
		}
	}
}

static void unpack_symbols(uint8_t* buf, char* unpacked) {
	int i, j;

	for (i = 0; i < SYM_LEN; i++) {
		/* output one byte for each received symbol (0x00 or 0x01) */
		for (j = 0; j < 8; j++) {
			unpacked[i * 8 + j] = (buf[i] & 0x80) >> 7;
			buf[i] <<= 1;
		}
	}
}

#define NUM_CHANNELS 79
#define RSSI_HISTORY_LEN NUM_BANKS
#define RSSI_BASE (-54)       /* CC2400 constant ... do not change */

/* Ignore packets with a SNR lower than this in order to reduce
 * processor load.  TODO: this should be a command line parameter. */

static char rssi_history[NUM_CHANNELS][RSSI_HISTORY_LEN] = { { INT8_MIN } };

#define NUM_BREDR_CHANNELS 79
#define RSSI_HISTORY_LEN NUM_BANKS

static int8_t cc2400_rssi_to_dbm(const int8_t rssi) {
	/* models the cc2400 datasheet fig 22 for 1M as piece-wise linear */
	if (rssi < -48) {
		return -120;
	} else if (rssi <= -45) {
		return 6 * (rssi + 28);
	} else if (rssi <= 30) {
		return (int8_t)((99 * ((int) rssi - 62)) / 110);
	} else if (rssi <= 35) {
		return (int8_t)((60 * ((int) rssi - 35)) / 11);
	} else {
		return 0;
	}
}

/* Ignore packets with a SNR lower than this in order to reduce
 * processor load.  TODO: this should be a command line parameter. */

//static int8_t rssi_history[NUM_BREDR_CHANNELS][RSSI_HISTORY_LEN] = {{INT8_MIN}};
static void determine_signal_and_noise(usb_pkt_rx *rx, int8_t * sig,
		int8_t * noise) {
	int8_t * channel_rssi_history = rssi_history[rx->channel];
	int8_t rssi;
	int i;

	/* Shift rssi max history and append current max */
	memmove(channel_rssi_history, channel_rssi_history + 1,
			RSSI_HISTORY_LEN - 1);
	channel_rssi_history[RSSI_HISTORY_LEN - 1] = rx->rssi_max;

#if 0
	/* Signal starts in oldest bank, but may cross into second
	 * oldest bank.  Take the max or the 2 maxs. */
	rssi = MAX(channel_rssi_history[0], channel_rssi_history[1]);
#else
	/* Alternatively, use all banks in history. */
	rssi = channel_rssi_history[0];
	for (i = 1; i < RSSI_HISTORY_LEN; i++)
		rssi = MAX(rssi, channel_rssi_history[i]);
#endif
	*sig = cc2400_rssi_to_dbm(rssi);

	/* Noise is an IIR of averages */
	/* FIXME: currently bogus */
	*noise = cc2400_rssi_to_dbm(rx->rssi_avg);
}

static uint64_t now_ns(void) {
	/* As per Apple QA1398 */
#if defined( __APPLE__ )
	static mach_timebase_info_data_t sTimebaseInfo;
	uint64_t ts = mach_absolute_time( );
	if (sTimebaseInfo.denom == 0) {
		(void) mach_timebase_info(&sTimebaseInfo);
	}
	return (ts*sTimebaseInfo.numer/sTimebaseInfo.denom);
#else
	struct timespec ts = { 0, 0 };
	(void) clock_gettime(CLOCK_REALTIME, &ts);
	return (1000000000ull * (uint64_t) ts.tv_sec) + (uint64_t) ts.tv_nsec;
#endif
}

static void track_clk100ns(const usb_pkt_rx *rx) {
	/* track clk100ns */
	if (!start_clk100ns) {
		last_clk100ns = start_clk100ns = rx->clk100ns;
		abs_start_ns = now_ns();
	}
	/* detect clk100ns roll-over */
	if (rx->clk100ns < last_clk100ns) {
		clk100ns_upper += 1;
	}
	last_clk100ns = rx->clk100ns;
}

static uint64_t now_ns_from_clk100ns(const usb_pkt_rx *rx) {
	track_clk100ns(rx);
	return abs_start_ns
			+ 100ull * (uint64_t)((rx->clk100ns - start_clk100ns) & 0xffffffff)
			+ ((100ull * clk100ns_upper) << 32);
}

/* Sniff for LAPs. If a piconet is provided, use the given LAP to
 * search for UAP.
 */
//static void cb_lap(JNIEnv* env, jobject thiz, void* args, usb_pkt_rx *rx, int bank){
static void cb_lap(JNIEnv* env, jobject thiz, void* args, usb_pkt_rx *rx,
		int bank) {
	usb_pkt_rx usb_packets[NUM_BANKS];
	char br_symbols[NUM_BANKS][BANK_LEN];
	FILE *infile = NULL;
	btbb_packet *pkt = NULL;
	btbb_piconet *pn = (btbb_piconet *) args;
	char syms[BANK_LEN * NUM_BANKS];
	int i;
	int8_t signal_level;
	int8_t noise_level;
	int8_t snr;
	int offset;
	uint32_t clkn;
	uint32_t lap = LAP_ANY;
	uint8_t uap = UAP_ANY;

	/* Sanity check */
	if (rx->channel > (NUM_BREDR_CHANNELS - 1))
		return;

	/* Copy packet (for dump) */
	memcpy(&usb_packets[bank], rx, sizeof(usb_pkt_rx));

	unpack_symbols(rx->data, br_symbols[bank]);

	/* Do analysis based on oldest packet */
	rx = &usb_packets[(bank + 1) % NUM_BANKS];
	uint64_t nowns = now_ns_from_clk100ns(rx);

	determine_signal_and_noise(rx, &signal_level, &noise_level);
	snr = signal_level - noise_level;

	/* WC4: use vm circbuf if target allows. This gets rid of this
	 * wrapped copy step. */

	/* Copy 2 oldest banks of symbols for analysis. Packet may
	 * cross a bank boundary. */
	for (i = 0; i < 2; i++)
		memcpy(syms + i * BANK_LEN, br_symbols[(i + 1 + bank) % NUM_BANKS],
				BANK_LEN);

	/* Look for packets with specified LAP, if given. Otherwise
	 * search for any packet.  Also determine if UAP is known. */
	if (pn) {
		lap = btbb_piconet_get_flag(pn, BTBB_LAP_VALID) ?
				btbb_piconet_get_lap(pn) : LAP_ANY;
		uap = btbb_piconet_get_flag(pn, BTBB_UAP_VALID) ?
				btbb_piconet_get_uap(pn) : UAP_ANY;
	}

	/* Pass packet-pointer-pointer so that
	 * packet can be created in libbtbb. */
	offset = btbb_find_ac(syms, BANK_LEN, lap, max_ac_errors, &pkt);
	if (offset < 0)
		return;

	/* Copy out remaining banks of symbols for full analysis. */
	for (i = 1; i < NUM_BANKS; i++)
		memcpy(syms + i * BANK_LEN, br_symbols[(i + 1 + bank) % NUM_BANKS],
				BANK_LEN);

	/* Once offset is known for a valid packet, copy in symbols
	 * and other rx data. CLKN here is the 312.5us CLK27-0. The
	 * btbb library can shift it be CLK1 if needed. */
	clkn = (rx->clkn_high << 20)
			+ (letoh32(rx->clk100ns) + offset + 1562) / 3125;
	btbb_packet_set_data(pkt, syms + offset, NUM_BANKS * BANK_LEN - offset,
			rx->channel, clkn);

	/* Dump to PCAP/PCAPNG if specified */
#if defined(USE_PCAP)
	if (h_pcap_bredr) {
		btbb_pcap_append_packet(h_pcap_bredr, nowns,
				signal_level, noise_level,
				lap, uap, pkt);
	}
#endif
	if (h_pcapng_bredr) {
		btbb_pcapng_append_packet(h_pcapng_bredr, nowns, signal_level,
				noise_level, lap, uap, pkt);
	}

	/* When reading from file, caller will read
	 * systime before calling this routine, so do
	 * not overwrite. Otherwise, get current time. */
	if (infile == NULL)
		systime = time(NULL);

	/* If dumpfile is specified, write out all banks to the
	 * file. There could be duplicate data in the dump if more
	 * than one LAP is found within the span of NUM_BANKS. */
//	if (dumpfile) {
//		for(i = 0; i < NUM_BANKS; i++) {
//			uint32_t systime_be = htobe32(systime);
//			if (fwrite(&systime_be,
//				   sizeof(systime_be), 1,
//				   dumpfile)
//			    != 1) {;}
//			if (fwrite(&usb_packets[(i + 1 + bank) % NUM_BANKS],
//				   sizeof(usb_pkt_rx), 1, dumpfile)
//			    != 1) {;}
//		}
//	}
	char LAP_event[64];

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"systime=%u ch=%2d LAP=%06x err=%u clk100ns=%u clk1=%u s=%d n=%d snr=%d\n",
			(int) systime, btbb_packet_get_channel(pkt),
			btbb_packet_get_lap(pkt), btbb_packet_get_ac_errors(pkt),
			rx->clk100ns, btbb_packet_get_clkn(pkt), signal_level, noise_level,
			snr);

	sprintf(LAP_event, "time=%u LAP=%06x s=%d", (int) systime,
			btbb_packet_get_lap(pkt), signal_level);
	Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_LAPResult(env, thiz,
			LAP_event);
}
//	i = btbb_process_packet(pkt, pn);
//	if(i < 0) {
//		follow_pn = pn;
//		stop_ubertooth = 1;
//	}
//
//out:
//	if (pkt)
//		btbb_packet_unref(pkt)

/*
 * Sniff Bluetooth Low Energy packets.
 */
void cb_btle_packet(JNIEnv* env, jobject thiz, void* args, usb_pkt_rx *rx,
		int bank) {
	FILE *infile = NULL;
	FILE *dumpfile = NULL;
	lell_packet * pkt;
	btle_options * opts = (btle_options *) args;
	int i;
	u32 access_address = 0;

	static u32 prev_ts = 0;
	uint32_t refAA;
	int8_t sig, noise;

	UNUSED(bank);

	uint64_t nowns = now_ns_from_clk100ns(rx);

	/* Sanity check */
	if (rx->channel > (NUM_BREDR_CHANNELS - 1))
		return;

	if (infile == NULL)
		systime = time(NULL);

	/* Dump to sumpfile if specified */
	if (dumpfile) {
		uint32_t systime_be = htobe32(systime);
		if (fwrite(&systime_be, sizeof(systime_be), 1, dumpfile) != 1) {
			;
		}
		if (fwrite(rx, sizeof(usb_pkt_rx), 1, dumpfile) != 1) {
			;
		}
	}

	lell_allocate_and_decode(rx->data, rx->channel + 2402, rx->clk100ns, &pkt);

	/* do nothing further if filtered due to bad AA */
	if (opts
			&& (opts->allowed_access_address_errors
					< lell_get_access_address_offenses(pkt))) {
		lell_packet_unref(pkt);
		return;
	}

	/* Dump to PCAP/PCAPNG if specified */
	refAA = lell_packet_is_data(pkt) ? 0 : 0x8e89bed6;
	determine_signal_and_noise(rx, &sig, &noise);
#if defined(USE_PCAP)
	if (h_pcap_le) {
		/* only one of these two will succeed, depending on
		 * whether PCAP was opened with DLT_PPI or not */
		lell_pcap_append_packet(h_pcap_le, nowns,
				sig, noise,
				refAA, pkt);
		lell_pcap_append_ppi_packet(h_pcap_le, nowns,
				rx->clkn_high,
				rx->rssi_min, rx->rssi_max,
				rx->rssi_avg, rx->rssi_count,
				pkt);
	}
#endif
	if (h_pcapng_le) {
		lell_pcapng_append_packet(h_pcapng_le, nowns, sig, noise, refAA, pkt);
	}

	char BTLE_event[64];

	u32 ts_diff = rx->clk100ns - prev_ts;
	prev_ts = rx->clk100ns;
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "systime=%u freq=%d addr=%08x delta_t=%.03f ms\n",
//	       systime, rx->channel + 2402, lell_get_access_address(pkt),
//	       ts_diff / 10000.0);

	sprintf(BTLE_event, "time=%u addr=%08x \n", (int) systime,
			lell_get_access_address(pkt));
	Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_BTLEResult(env, thiz,
			BTLE_event);

//	int len = (rx->data[5] & 0x3f) + 6 + 3;
//	if (len > 50) len = 50;
//
//	for (i = 4; i < len; ++i){
//		printf("%02x ", rx->data[i]);
//		//logging to android log
//		//__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "data=%02x \n", rx->data[i]);
//	}
//	printf("\n");
//
//	lell_print(pkt);
//	printf("\n");
//
//	lell_packet_unref(pkt);
//
//	fflush(stdout);
}

//jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_StartRxBTLE(
//		JNIEnv* env, jobject thiz) {
//	int r;
//	int i;
//	int xfer_blocks;
//	int num_xfers;
//	uint8_t bank = 0;
//	uint8_t rx_buf1[BUFFER_SIZE];
//	uint8_t rx_buf2[BUFFER_SIZE];
//	int xfer_size = PKT_LEN * 2; // XFER_LEN
//	uint16_t num_blocks = 0;
//
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "call to start_rxBTLE()");
//	rx_BTLE_running = true;
//
//	/* INIT from stream_rx_usb */
//	/*
//	 * A block is 64 bytes transferred over USB (includes 50 bytes of rx symbol
//	 * payload).  A transfer consists of one or more blocks.  Consecutive
//	 * blocks should be approximately 400 microseconds apart (timestamps about
//	 * 4000 apart in units of 100 nanoseconds).
//	 */
//
//	if (xfer_size > BUFFER_SIZE)
//		xfer_size = BUFFER_SIZE;
//	xfer_blocks = xfer_size / PKT_LEN;
//	xfer_size = xfer_blocks * PKT_LEN;
//	num_xfers = num_blocks / xfer_blocks;
//	num_blocks = num_xfers * xfer_blocks;
//
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//			"rx %d blocks of 64 bytes in %d byte transfers\n", num_blocks,
//			xfer_size);
//
//	empty_buf = &rx_buf1[0];
//	full_buf = &rx_buf2[0];
//	really_full = 0;
//	rx_xfer = libusb_alloc_transfer(0);
//	libusb_fill_bulk_transfer(rx_xfer, devh, DATA_IN, empty_buf, xfer_size,
//			cb_xfer, NULL, TIMEOUT);
//
//	cmd_rx_syms(devh, num_blocks);
//
//	r = libusb_submit_transfer(rx_xfer);
//	if (r < 0) {
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//				"rx_xfer submission: %d\n", r);
//		return -1;
//	}
//
//	/* end of INIT from stream_rx_usb */
//
//	while (rx_BTLE_running == true) {
//
//		// here's the magic.
//		while (!really_full && (rx_BTLE_running == true)) {	// really_full filled in cb_xfer()
//		/*
//		 will block here !
//		 struct timeval tv;
//		 tv.tv_sec = 2;
//		 tv.tv_usec = 0;
//		 return libusb_handle_events_timeout(ctx, &tv);
//		 */
//			r = libusb_handle_events(NULL);	// timeout : 2 seconds
//			if (r < 0) {
//				__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//						"libusb_handle_events: %d\n", r);
//				return -1;
//			}
//		}
//		//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "transfer completed: blocks %d, bank %d\n", xfer_blocks, bank);
//
//		/* process each received block */
//		for (i = 0; i < xfer_blocks; i++) {
//
//			cb_btle_packet(env, thiz, NULL,
//					(usb_pkt_rx *) (full_buf + PKT_LEN * i), bank);
//
//			bank = (bank + 1) % NUM_BANKS;
//		}
//		really_full = 0;
//	}
//	libusb_free_transfer(rx_xfer);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "start_rxBTLE() done");
//	return 0;
//}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_StartRxLAP(
		JNIEnv* env, jobject thiz) {
	int r;
	int i;
	int xfer_blocks;
	int num_xfers;
	uint8_t bank = 0;
	uint8_t rx_buf1[BUFFER_SIZE];
	uint8_t rx_buf2[BUFFER_SIZE];
	int xfer_size = PKT_LEN * 2; // XFER_LEN
	uint16_t num_blocks = 0;

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "call to start_rxLAP()");
	rx_LAP_running = true;

	/* INIT from stream_rx_usb */
	/*
	 * A block is 64 bytes transferred over USB (includes 50 bytes of rx symbol
	 * payload).  A transfer consists of one or more blocks.  Consecutive
	 * blocks should be approximately 400 microseconds apart (timestamps about
	 * 4000 apart in units of 100 nanoseconds).
	 */

	if (xfer_size > BUFFER_SIZE)
		xfer_size = BUFFER_SIZE;
	xfer_blocks = xfer_size / PKT_LEN;
	xfer_size = xfer_blocks * PKT_LEN;
	num_xfers = num_blocks / xfer_blocks;
	num_blocks = num_xfers * xfer_blocks;

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"rx %d blocks of 64 bytes in %d byte transfers\n", num_blocks,
			xfer_size);

	empty_buf = &rx_buf1[0];
	full_buf = &rx_buf2[0];
	really_full = 0;
	rx_xfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(rx_xfer, devh, DATA_IN, empty_buf, xfer_size,
			cb_xfer, NULL, TIMEOUT);

	cmd_rx_syms(devh, num_blocks);

	r = libusb_submit_transfer(rx_xfer);
	if (r < 0) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"rx_xfer submission: %d\n", r);
		return -1;
	}

	/* end of INIT from stream_rx_usb */

	while (rx_LAP_running == true) {

		// here's the magic.
		while (!really_full && (rx_LAP_running == true)) { // really_full filled in cb_xfer()
			/*
			 will block here !
			 struct timeval tv;
			 tv.tv_sec = 2;
			 tv.tv_usec = 0;
			 return libusb_handle_events_timeout(ctx, &tv);
			 */
			r = libusb_handle_events(NULL);	// timeout : 2 seconds
			if (r < 0) {
				__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
						"libusb_handle_events: %d\n", r);
				return -1;
			}
		}
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "transfer completed: blocks %d, bank %d\n", xfer_blocks, bank);

		/* process each received block */
		for (i = 0; i < xfer_blocks; i++) {

			cb_lap(env, thiz, NULL, (usb_pkt_rx *) (full_buf + PKT_LEN * i),
					bank);

			bank = (bank + 1) % NUM_BANKS;
		}
		really_full = 0;
	}
	libusb_free_transfer(rx_xfer);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "start_rxLAP() done");
	return 0;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_BTLEResult(
		JNIEnv* env, jobject obj, char *stringtowrite) {
	jclass cls;
	jstring BTLEResultString;
	JNIEnv* env2;
	jint rv;
	int status;

//    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "enter LAPResult() %d, running = %d", (int)env, (int)rx_LAP_running);
	BTLEResultString = (*env)->NewStringUTF(env, stringtowrite);// creates a jstring
	char buf[128];
	const char *str = (*env)->GetStringUTFChars(env, BTLEResultString, 0);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "BTLEResult() write %s", str);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Calling JAVA method from NATIVE C/C++ %d, %d, %d", (int)*env, (int)gJavaObject, (int)gJMethodID);
	(*env)->CallVoidMethod(env, gJavaObject, gJMethodID, BTLEResultString);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "DONE!!!");
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "LAPResult(): CallVoidMethod done");
	(*env)->ReleaseStringUTFChars(env, BTLEResultString, str);
	(*env)->DeleteLocalRef(env, BTLEResultString);
	return;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_LAPResult(
		JNIEnv* env, jobject obj, char *stringtowrite) {
	jclass cls;
	jstring LAPResultString;
	JNIEnv* env2;
	jint rv;
	int status;

//    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "enter LAPResult() %d, running = %d", (int)env, (int)rx_LAP_running);
	LAPResultString = (*env)->NewStringUTF(env, stringtowrite);	// creates a jstring
	char buf[128];
	const char *str = (*env)->GetStringUTFChars(env, LAPResultString, 0);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "LAPResult() write %s", str);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Calling JAVA method from NATIVE C/C++ %d, %d, %d", (int)*env, (int)gJavaObject, (int)gJMethodID);
	(*env)->CallVoidMethod(env, gJavaObject, gJMethodID, LAPResultString);
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "DONE!!!");
//	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "LAPResult(): CallVoidMethod done");
	(*env)->ReleaseStringUTFChars(env, LAPResultString, str);
	(*env)->DeleteLocalRef(env, LAPResultString);
	return;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_StopRxLAP(
		JNIEnv* env, jobject thiz) {
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "call to stop_rxLAP()");
	rx_LAP_running = false;
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "stop_rxLAP() done");
	return 0;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_StopRxBTLE(
		JNIEnv* env, jobject thiz) {
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "call to stop_rxBTLE()");
	rx_BTLE_running = false;
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "stop_rxBTLE() done");
	return 0;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_startUbertooth(
		JNIEnv* env, jobject thiz) {
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- StartUbertoothCalled");
	devh = ubertooth_start(ubertooth_device);
	if (devh == NULL)
		return -1;
	else
		return 1;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_resetUbertooth(
		JNIEnv* env, jobject thiz) {
	int r = 0;
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- resetUbertoothCalled");
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"Resetting ubertooth device number %d\n",
			(ubertooth_device >= 0) ? ubertooth_device : 0);
	r = cmd_reset(devh);
	if (r != 0) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- reset failed");
	} else {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- reset ok");
	}
	sleep(3);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- after sleep failed");
	devh = ubertooth_start(ubertooth_device);
	if (devh == NULL) {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "--- reset failed");
		return -1;
	} else {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"--- start after reset ok");
		return 1;
	}
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_StartRxBTLE(
		JNIEnv* env, jobject thiz, jstring filepath) {

	const char * res;

	//convert char
	jboolean isCopy;
	res = (*env)->GetStringUTFChars(env, filepath, NULL);

//FILE Creation Tests

//	FILE* file2 = fopen("/sdcard/helloWorld.txt", "w+");
//
//	if (file2 != NULL) {
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//				"--- created file is not null");
//		fputs("HELLO WORLD!\n", file2);
//		fflush(file2);
//		fclose(file2);
//	} else {
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//				"--- created file is null");
//	}
//	FILE* file3 = fopen(res, "w+");
//
//	if (file3 != NULL) {
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//				"--- created file 2 is not null");
//		fputs("HELLO WORLD!\n", file3);
//	} else {
//		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
//				"--- created file 2 is null");
//	}
// END File creation tests

	rx_BTLE_running = true;
	usb_pkt_rx pkt;
	int do_adv_index = 37;
	btle_options cb_opts = { .allowed_access_address_errors = 32 };

	if (!h_pcap_le) {
		//if (lell_pcap_ppi_create_file("/sdcard/capturing2141.pcap", 0, &h_pcap_le)) {
		if (lell_pcap_ppi_create_file(res, 0, &h_pcap_le)) {
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
					"lell_pcap_ppi_create_file");
			//err(1, "lell_pcap_ppi_create_file: ");
		}
	} else {
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"Ignoring extra capture file");

		//printf("Ignoring extra capture file: %s\n", optarg);
	}

	//release res variable
	(*env)->ReleaseStringUTFChars(env, filepath, res);

	cmd_set_modulation(devh, MOD_BT_LOW_ENERGY);

	//do follow
	u16 channel;
	if (do_adv_index == 37)
		channel = 2402;
	else if (do_adv_index == 38)
		channel = 2426;
	else
		channel = 2480;
	cmd_set_channel(devh, channel);
	cmd_btle_sniffing(devh, 2);

	//poll data
	while (rx_BTLE_running == true) {
		int r = cmd_poll(devh, &pkt);
		if (r < 0) {
			//printf("USB error\n");
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "USB error\n");
			break;
		}
		if (r == sizeof(usb_pkt_rx))
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Packet received");
		cb_btle(&cb_opts, &pkt, 0);
		usleep(500);
	}
	ubertooth_stop(devh);
}

// Returns the "max" of the specified number of sweeps from low_freq to high_freq
jintArray Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_scanSpectrum(
		JNIEnv* env, jobject thiz, int low_freq, int high_freq, int sweeps) {
	int ns, xfer_size = 512, num_blocks = 0xFFFF, curr_sweep = 0, z;
	int nbins = high_freq - low_freq;  // number of 1MHz bins
	jintArray result = (jintArray)(*env)->NewIntArray(env, nbins);
	jint *fill = (int *) malloc(sizeof(int) * nbins);

//  sweeps+=10;

	for (z = 0; z < nbins; z++)
		fill[z] = -255;

	bool done = false;

	//__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "call to scanSpectrum(%d,%d,%d)", low_freq, high_freq, sweeps);

	u8 buffer[BUFFER_SIZE];
	int r;
	int i, j;
	int xfer_blocks;
	int num_xfers;
	int transferred;
	int frequency;
	u32 time; /* in 100 nanosecond units */

	if (xfer_size > BUFFER_SIZE)
		xfer_size = BUFFER_SIZE;
	xfer_blocks = xfer_size / PKT_LEN;
	xfer_size = xfer_blocks * PKT_LEN;
	num_xfers = num_blocks / xfer_blocks;
	num_blocks = num_xfers * xfer_blocks;

	cmd_specan(devh, low_freq, high_freq);

	while (!done) {
		r = libusb_bulk_transfer(devh, DATA_IN, buffer, xfer_size, &transferred,
				TIMEOUT);
		if (r < 0) {
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
					"bulk read returned: %d, failed to read", r);
			return NULL;
		}
		if (transferred != xfer_size) {
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
					"bad data read size (%d)", transferred);
			return NULL;
		}

		/* process each received block */
		for (i = 0; i < xfer_blocks; i++) {
			time = buffer[4 + PKT_LEN * i] | (buffer[5 + PKT_LEN * i] << 8)
					| (buffer[6 + PKT_LEN * i] << 16)
					| (buffer[7 + PKT_LEN * i] << 24);

			for (j = PKT_LEN * i + SYM_OFFSET; j < PKT_LEN * i + 62; j += 3) {
				frequency = (buffer[j] << 8) | buffer[j + 1];
				int8_t val = (int8_t) buffer[j + 2] + RSSI_OFFSET;
				int bin = frequency - low_freq;
				//__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "... freq: %d - val: %d - bin: %d", frequency, val, bin);
				if ((int) val > fill[bin]) { // Do a max across the sweeps
					fill[bin] = (int) val;
					//  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "... freq: %d - val: %d - bin: %d", frequency, val, bin);
				}

				if (frequency == high_freq) {
					curr_sweep++;
					//__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Current sweep complete, now at: %d", curr_sweep);
				}

				if (curr_sweep == sweeps)
					done = true;
			}
		}
	}

	if (result == NULL) {
		result = (jintArray)(*env)->NewIntArray(env, 1);
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
				"not sure why, but the ubertooth scan failed");
	} else {
		(*env)->SetIntArrayRegion(env, (jintArray) result, (jsize) 0,
				(jsize) nbins, fill);
		//__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "ubertooth scan complete");
	}

	return result;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_SaveGlobalObject(
		JNIEnv* env, jobject thiz, jobject obj) {
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"call to SaveGlobalObject(%d)", (int) obj);
	gJavaObject = (*env)->NewGlobalRef(env, obj);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"call to SaveGlobalObject: gJavaObject %d", (int) gJavaObject);
	return 1;
}

jint Java_com_gnychis_ubertooth_DeviceHandlers_UbertoothOne_stopUbertooth(
		JNIEnv* env, jobject thiz) {
	ubertooth_stop(devh);
	return 1;
}
