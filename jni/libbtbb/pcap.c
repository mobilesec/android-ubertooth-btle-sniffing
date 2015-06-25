/* -*- c -*- */
/*
 * Copyright 2014 Christopher D. Kilgour techie AT whiterocker.com
 *
 * This file is part of libbtbb
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libbtbb; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include "bluetooth_le_packet.h"
#include "bluetooth_packet.h"
#include "pcap-int.h"
#include "btbb.h"
#include "pcap-common.h"
#include <android/log.h>
#include "pcap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef enum {
	PCAP_OK = 0, PCAP_INVALID_HANDLE, PCAP_FILE_NOT_ALLOWED, PCAP_NO_MEMORY,
} PCAP_RESULT;

//Android logging tag
#define LOG_TAG "UbertoothPCAPC" // text for log tag

/*
 * dl_to_linktype definitions
 */
#define LINKTYPE_PFSYNC		246
#define DLT_PKTAP	258
#define LINKTYPE_PKTAP		258
#define DLT_MATCHING_MIN	104 /* lowest value in the "matching" range */
#define DLT_MATCHING_MAX	263	/* highest value in the "matching" range */

/*
 * Standard libpcap format.
 */
#define TCPDUMP_MAGIC		0xa1b2c3d4
/*
 * Normal libpcap format, except for seconds/nanoseconds timestamps,
 * as per a request by Ulf Lamping <ulf.lamping@web.de>
 */
#define NSEC_TCPDUMP_MAGIC	0xa1b23c4d

//#if defined(ENABLE_PCAP)

/* BT BR/EDR support */

typedef struct btbb_pcap_handle {
	pcap_t * pcap;
	pcap_dumper_t * dumper;
} btbb_pcap_handle;

int btbb_pcap_create_file(const char *filename, btbb_pcap_handle ** ph) {
	int retval = 0;
	btbb_pcap_handle * handle = malloc(sizeof(btbb_pcap_handle));
	if (handle) {
		memset(handle, 0, sizeof(*handle));
#ifdef PCAP_TSTAMP_PRECISION_NANO
		handle->pcap = pcap_open_dead_with_tstamp_precision(
				DLT_BLUETOOTH_BREDR_BB, BREDR_MAX_PAYLOAD,
				PCAP_TSTAMP_PRECISION_NANO);
#else
		handle->pcap = pcap_open_dead(DLT_BLUETOOTH_BREDR_BB, BREDR_MAX_PAYLOAD);
#endif
		if (handle->pcap) {
			handle->dumper = pcap_dump_open(handle->pcap, filename);
			if (handle->dumper) {
				*ph = handle;
			} else {
				pcap_perror(handle->pcap, "PCAP error:");
				retval = -PCAP_FILE_NOT_ALLOWED;
				goto fail;
			}
		} else {
			retval = -PCAP_INVALID_HANDLE;
			goto fail;
		}
	} else {
		retval = -PCAP_NO_MEMORY;
		goto fail;
	}
	return retval;
	fail: (void) btbb_pcap_close(handle);
	return retval;
}

typedef struct {
	struct pcap_pkthdr pcap_header;
	pcap_bluetooth_bredr_bb_header bredr_bb_header;
	uint8_t bredr_payload[BREDR_MAX_PAYLOAD];
} pcap_bredr_packet;

static void assemble_pcapng_bredr_packet(pcap_bredr_packet * pkt,
		const uint32_t interface_id, const uint64_t ns, const uint32_t caplen,
		const uint8_t rf_channel, const int8_t signal_power,
		const int8_t noise_power, const uint8_t access_code_offenses,
		const uint8_t payload_transport, const uint8_t payload_rate,
		const uint8_t corrected_header_bits,
		const int16_t corrected_payload_bits, const uint32_t lap,
		const uint32_t ref_lap, const uint8_t ref_uap, const uint32_t bt_header,
		const uint16_t flags, const uint8_t * payload) {
	uint32_t pcap_caplen = sizeof(pcap_bluetooth_bredr_bb_header) + caplen;
	uint32_t reflapuap = (ref_lap & 0xffffff) | (ref_uap << 24);

	pkt->pcap_header.ts.tv_sec = ns / 1000000000ull;
	pkt->pcap_header.ts.tv_usec = ns % 1000000000ull;
	pkt->pcap_header.caplen = pcap_caplen;
	pkt->pcap_header.len = pcap_caplen;

	pkt->bredr_bb_header.rf_channel = rf_channel;
	pkt->bredr_bb_header.signal_power = signal_power;
	pkt->bredr_bb_header.noise_power = noise_power;
	pkt->bredr_bb_header.access_code_offenses = access_code_offenses;
	pkt->bredr_bb_header.payload_transport_rate = (payload_transport << 4)
			| payload_rate;
	pkt->bredr_bb_header.corrected_header_bits = corrected_header_bits;
	pkt->bredr_bb_header.corrected_payload_bits = htole16(
			corrected_payload_bits);
	pkt->bredr_bb_header.lap = htole32(lap);
	pkt->bredr_bb_header.ref_lap_uap = htole32(reflapuap);
	pkt->bredr_bb_header.bt_header = htole16(bt_header);
	pkt->bredr_bb_header.flags = htole16(flags);
	if (caplen) {
		(void) memcpy(&pkt->bredr_payload[0], payload, caplen);
	} else {
		pkt->bredr_bb_header.flags &= htole16(~BREDR_PAYLOAD_PRESENT);
	}
}

int btbb_pcap_append_packet(btbb_pcap_handle * h, const uint64_t ns,
		const int8_t sigdbm, const int8_t noisedbm, const uint32_t reflap,
		const uint8_t refuap, const btbb_packet *pkt) {
	if (h && h->dumper) {
		uint16_t flags = BREDR_DEWHITENED | BREDR_SIGPOWER_VALID
				| ((noisedbm < sigdbm) ? BREDR_NOISEPOWER_VALID : 0)
				| ((reflap != LAP_ANY) ? BREDR_REFLAP_VALID : 0)
				| ((refuap != UAP_ANY) ? BREDR_REFUAP_VALID : 0);
		uint32_t caplen = (uint32_t) btbb_packet_get_payload_length(pkt);
		uint8_t payload_bytes[caplen];
		btbb_get_payload_packed(pkt, (char *) &payload_bytes[0]);
		caplen = MIN(BREDR_MAX_PAYLOAD, caplen);
		pcap_bredr_packet pcap_pkt;
		assemble_pcapng_bredr_packet(&pcap_pkt, 0, ns, caplen,
				btbb_packet_get_channel(pkt), sigdbm, noisedbm,
				btbb_packet_get_ac_errors(pkt), btbb_packet_get_transport(pkt),
				btbb_packet_get_modulation(pkt), 0, /* TODO: corrected header bits */
				0, /* TODO: corrected payload bits */
				btbb_packet_get_lap(pkt), reflap, refuap,
				btbb_packet_get_header_packed(pkt), flags, payload_bytes);
		pcap_dump((u_char *) h->dumper, &pcap_pkt.pcap_header,
				(u_char *) &pcap_pkt.bredr_bb_header);
		return 0;
	}
	return -PCAP_INVALID_HANDLE;
}

int btbb_pcap_close(btbb_pcap_handle * h) {
	if (h && h->dumper) {
		pcap_dump_close(h->dumper);
	}
	if (h && h->pcap) {
		pcap_close(h->pcap);
	}
	if (h) {
		free(h);
		return 0;
	}
	return -PCAP_INVALID_HANDLE;
}

/* BTLE support */

typedef struct lell_pcap_handle {
	pcap_t * pcap;
	pcap_dumper_t * dumper;
	int dlt;
	uint8_t btle_ppi_version;
} lell_pcap_handle;

static int lell_pcap_create_file_dlt(const char *filename, int dlt,
		lell_pcap_handle ** ph) {
	int retval = 0;
	lell_pcap_handle * handle = malloc(sizeof(lell_pcap_handle));
	if (handle) {
		memset(handle, 0, sizeof(*handle));
#ifdef PCAP_TSTAMP_PRECISION_NANO
		handle->pcap = pcap_open_dead_with_tstamp_precision(dlt,
				BREDR_MAX_PAYLOAD, PCAP_TSTAMP_PRECISION_NANO);
#else
		handle->pcap = pcap_open_dead(dlt, BREDR_MAX_PAYLOAD);
#endif
		if (handle->pcap) {
			handle->dumper = pcap_dump_open(handle->pcap, filename);
			if (handle->dumper) {
				handle->dlt = dlt;
				*ph = handle;
			} else {
				retval = -PCAP_FILE_NOT_ALLOWED;
				goto fail;
			}
		} else {
			retval = -PCAP_INVALID_HANDLE;
			goto fail;
		}
	} else {
		retval = -PCAP_NO_MEMORY;
		goto fail;
	}
	return retval;
	fail: (void) lell_pcap_close(handle);
	return retval;
}

int lell_pcap_create_file(const char *filename, lell_pcap_handle ** ph) {
	return lell_pcap_create_file_dlt(filename, DLT_BLUETOOTH_LE_LL_WITH_PHDR,
			ph);
}

int lell_pcap_ppi_create_file(const char *filename, int btle_ppi_version,
		lell_pcap_handle ** ph) {
	int retval = lell_pcap_create_file_dlt(filename, DLT_PPI, ph);
	if (!retval) {
		(*ph)->btle_ppi_version = btle_ppi_version;
	}
	return retval;
}

typedef struct {
	struct pcap_pkthdr pcap_header;
	pcap_bluetooth_le_ll_header le_ll_header;
	uint8_t le_packet[LE_MAX_PAYLOAD];
} pcap_le_packet;

static void assemble_pcapng_le_packet(pcap_le_packet * pkt,
		const uint32_t interface_id, const uint64_t ns, const uint32_t caplen,
		const uint8_t rf_channel, const int8_t signal_power,
		const int8_t noise_power, const uint8_t access_address_offenses,
		const uint32_t ref_access_address, const uint16_t flags,
		const uint8_t * lepkt) {
	uint32_t pcap_caplen = sizeof(pcap_bluetooth_le_ll_header) + caplen;

	pkt->pcap_header.ts.tv_sec = ns / 1000000000ull;
	pkt->pcap_header.ts.tv_usec = ns % 1000000000ull;
	pkt->pcap_header.caplen = pcap_caplen;
	pkt->pcap_header.len = pcap_caplen;

	pkt->le_ll_header.rf_channel = rf_channel;
	pkt->le_ll_header.signal_power = signal_power;
	pkt->le_ll_header.noise_power = noise_power;
	pkt->le_ll_header.access_address_offenses = access_address_offenses;
	pkt->le_ll_header.ref_access_address = htole32(ref_access_address);
	pkt->le_ll_header.flags = htole16(flags);
	(void) memcpy(&pkt->le_packet[0], lepkt, caplen);
}

int lell_pcap_append_packet(lell_pcap_handle * h, const uint64_t ns,
		const int8_t sigdbm, const int8_t noisedbm, const uint32_t refAA,
		const lell_packet *pkt) {
	if (h && h->dumper && (h->dlt == DLT_BLUETOOTH_LE_LL_WITH_PHDR)) {
		uint16_t flags = LE_DEWHITENED | LE_AA_OFFENSES_VALID
				| LE_SIGPOWER_VALID
				| ((noisedbm < sigdbm) ? LE_NOISEPOWER_VALID : 0)
				| (lell_packet_is_data(pkt) ? 0 : LE_REF_AA_VALID);
		pcap_le_packet pcap_pkt;
		assemble_pcapng_le_packet(&pcap_pkt, 0, ns, 9 + pkt->length,
				pkt->channel_k, sigdbm, noisedbm, pkt->access_address_offenses,
				refAA, flags, &pkt->symbols[0]);
		pcap_dump((u_char *) h->dumper, &pcap_pkt.pcap_header,
				(u_char *) &pcap_pkt.le_ll_header);
		return 0;
	}
	return -PCAP_INVALID_HANDLE;
}

#define PPI_BTLE 30006

typedef struct
	__attribute__((packed)) {
		uint8_t pph_version;
		uint8_t pph_flags;
		uint16_t pph_len;
		uint32_t pph_dlt;
	} ppi_packet_header_t;

	typedef struct
		__attribute__((packed)) {
			uint16_t pfh_type;
			uint16_t pfh_datalen;
		} ppi_fieldheader_t;

		typedef struct
			__attribute__((packed)) {
				uint8_t btle_version;
				uint16_t btle_channel;
				uint8_t btle_clkn_high;
				uint32_t btle_clk100ns;
				int8_t rssi_max;
				int8_t rssi_min;
				int8_t rssi_avg;
				uint8_t rssi_count;
			} ppi_btle_t;

			typedef struct
				__attribute__((packed)) {
					struct pcap_pkthdr pcap_header;
					ppi_packet_header_t ppi_packet_header;
					ppi_fieldheader_t ppi_fieldheader;
					ppi_btle_t le_ll_ppi_header;
					uint8_t le_packet[LE_MAX_PAYLOAD];
				} pcap_ppi_le_packet;

				int lell_pcap_append_ppi_packet(lell_pcap_handle * h,
						const uint64_t ns, const uint8_t clkn_high,
						const int8_t rssi_min, const int8_t rssi_max,
						const int8_t rssi_avg, const uint8_t rssi_count,
						const lell_packet *pkt) {
					const ppi_packet_header_sz = sizeof(ppi_packet_header_t);
					const ppi_fieldheader_sz = sizeof(ppi_fieldheader_t);
					const le_ll_ppi_header_sz = sizeof(ppi_btle_t);

					if (h && h->dumper && (h->dlt == DLT_PPI)) {
						pcap_ppi_le_packet pcap_pkt;
						uint32_t pcap_caplen = ppi_packet_header_sz
								+ ppi_fieldheader_sz + le_ll_ppi_header_sz
								+ pkt->length + 9;
						uint16_t MHz = 2402 + 2 * lell_get_channel_k(pkt);

						pcap_pkt.pcap_header.ts.tv_sec = ns / 1000000000ull;
						pcap_pkt.pcap_header.ts.tv_usec = ns % 1000000000ull;
						pcap_pkt.pcap_header.caplen = pcap_caplen;
						pcap_pkt.pcap_header.len = pcap_caplen;

						pcap_pkt.ppi_packet_header.pph_version = 0;
						pcap_pkt.ppi_packet_header.pph_flags = 0;
						pcap_pkt.ppi_packet_header.pph_len = htole16(
								ppi_packet_header_sz + ppi_fieldheader_sz
										+ le_ll_ppi_header_sz);
						pcap_pkt.ppi_packet_header.pph_dlt = htole32(DLT_USER0);

						pcap_pkt.ppi_fieldheader.pfh_type = htole16(PPI_BTLE);
						pcap_pkt.ppi_fieldheader.pfh_datalen = htole16(
								le_ll_ppi_header_sz);

						pcap_pkt.le_ll_ppi_header.btle_version =
								h->btle_ppi_version;
						pcap_pkt.le_ll_ppi_header.btle_channel = htole16(MHz);
						pcap_pkt.le_ll_ppi_header.btle_clkn_high = clkn_high;
						pcap_pkt.le_ll_ppi_header.btle_clk100ns = htole32(
								pkt->clk100ns);
						pcap_pkt.le_ll_ppi_header.rssi_max = rssi_max;
						pcap_pkt.le_ll_ppi_header.rssi_min = rssi_min;
						pcap_pkt.le_ll_ppi_header.rssi_avg = rssi_avg;
						pcap_pkt.le_ll_ppi_header.rssi_count = rssi_count;
						(void) memcpy(&pcap_pkt.le_packet[0], &pkt->symbols[0],
								pkt->length + 9); // FIXME where does the 9 come from?
						pcap_dump((u_char *) h->dumper, &pcap_pkt.pcap_header,
								(u_char *) &pcap_pkt.ppi_packet_header);
						pcap_dump_flush(h->dumper);
						return 0;
					}
					return -PCAP_INVALID_HANDLE;
				}

				static int sf_write_header(pcap_t *p, FILE *fp, int linktype,
						int thiszone, int snaplen) {
					struct pcap_file_header hdr;

					hdr.magic =
							p->opt.tstamp_precision
									== PCAP_TSTAMP_PRECISION_NANO ?
									NSEC_TCPDUMP_MAGIC : TCPDUMP_MAGIC;
					hdr.version_major = PCAP_VERSION_MAJOR;
					hdr.version_minor = PCAP_VERSION_MINOR;

					hdr.thiszone = thiszone;
					hdr.snaplen = snaplen;
					hdr.sigfigs = 0;
					hdr.linktype = linktype;

					if (fwrite((char *) &hdr, sizeof(hdr), 1, fp) != 1)
						return (-1);

					return (0);
				}

				int lell_pcap_close(lell_pcap_handle *h) {
					if (h && h->dumper) {
						pcap_dump_close(h->dumper);
					}
					if (h && h->pcap) {
						pcap_close(h->pcap);
					}
					if (h) {
						free(h);
						return 0;
					}
					return -PCAP_INVALID_HANDLE;
				}

				/*------------------------------------------------------
				 *
				 * Define pcap methods that are missing ----------------
				 *
				 * -----------------------------------------------------*/

				int dlt_to_linktype(int dlt) {
					int i;

					/*
					 * DLTs that, on some platforms, have values in the matching range
					 * but that *don't* have the same value as the corresponding
					 * LINKTYPE because, for some reason, not all OSes have the
					 * same value for that DLT (note that the DLT's value might be
					 * outside the matching range on some of those OSes).
					 */
					if (dlt == DLT_PFSYNC)
						return (LINKTYPE_PFSYNC);
					if (dlt == DLT_PKTAP)
						return (LINKTYPE_PKTAP);

					/*
					 * For all other values in the matching range, the DLT
					 * value is the same as the LINKTYPE value.
					 */
					if (dlt >= DLT_MATCHING_MIN && dlt <= DLT_MATCHING_MAX)
						return (dlt);

					//TODO:outcommented map for linktype_dl
					/*
					 * Map the values outside that range.
					 */
//					for (i = 0; map[i].dlt != -1; i++) {
//						if (map[i].dlt == dlt)
//							return (map[i].linktype);
//					}
					/*
					 * If we don't have a mapping for this DLT, return an
					 * error; that means that this is a value with no corresponding
					 * LINKTYPE, and we need to assign one.
					 */
					return (-1);
				}

				/*
				 * Not all systems have strerror().
				 */
				const char *
				pcap_strerror(int errnum) {
//#ifdef HAVE_STRERROR
					return (strerror(errnum));
//#else
//	extern int sys_nerr;
//	//extern const char *const sys_errlist[];
//	static char ebuf[15+10+1];
//
//	if ((unsigned int)errnum < sys_nerr)
//		return ((char *)sys_errlist[errnum]);
//	(void)snprintf(ebuf, sizeof ebuf, "Unknown error: %d", errnum);
//	return(ebuf);
//#endif
				}

				void pcap_perror(pcap_t *p, char *prefix) {
					fprintf(stderr, "%s: %s\n", prefix, p->errbuf);
				}

				pcap_t *
				pcap_open_dead(int linktype, int snaplen) {
					return (pcap_open_dead_with_tstamp_precision(linktype,
							snaplen, PCAP_TSTAMP_PRECISION_MICRO));
				}

				int pcap_stats(pcap_t *p, struct pcap_stat *ps) {
					return (p->stats_op(p, ps));
				}

				static int pcap_stats_dead(pcap_t *p, struct pcap_stat *ps) {
//					snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
//							"Statistics aren't available from a pcap_open_dead pcap_t %s %s:",
//							pcap_strerror(errno), pcap_strerror(errno));
					__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
														"Statistics aren't available from a pcap_open_dead pcap_t");
					printf("Statistics aren't available from a pcap_open_dead pcap_t");
					return (-1);
				}

				static void pcap_cleanup_dead(pcap_t *p) {
					/* Nothing to do. */
				}

				static pcap_dumper_t *
				pcap_setup_dump(pcap_t *p, int linktype, FILE *f,
						const char *fname) {

//#if defined(WIN32) || defined(MSDOS)
//					/*
//					 * If we're writing to the standard output, put it in binary
//					 * mode, as savefiles are binary files.
//					 *
//					 * Otherwise, we turn off buffering.
//					 * XXX - why?  And why not on the standard output?
//					 */
//					if (f == stdout)
//					SET_BINMODE(f);
//					else
//					setbuf(f, NULL);
//#endif
					if (sf_write_header(p, f, linktype, p->tzoff, p->snapshot)
							== -1) {
//						snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
//								"Can't write to %s: %s", fname,
//								pcap_strerror(errno));
						printf("Can't write to file, pcap_setup_dump");
						__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Can't write to file, pcap_setup_dump");
						if (f != stdout)
							(void) fclose(f);
						return (NULL);
					}
					return ((pcap_dumper_t *) f);
				}

				/*
				 * Initialize so that sf_write() will output to the file named 'fname'.
				 */
				pcap_dumper_t *
				pcap_dump_open(pcap_t *p, const char *fname) {
					FILE *f;
					int linktype;

					/*
					 * If this pcap_t hasn't been activated, it doesn't have a
					 * link-layer type, so we can't use it.
					 */
					if (!p->activated) {
//						snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
//								"not-yet-activated pcap_t passed to pcap_dump_open %s:",
//								fname);
						__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "not-yet-activated pcap_t passed to pcap_dump_open");
						printf("not-yet-activated pcap_t passed to pcap_dump_open");
						return (NULL);
					}
					linktype = dlt_to_linktype(p->linktype);
					if (linktype == -1) {
//						snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
//								"%s: link-layer type %d isn't supported in savefiles",
//								fname, p->linktype);
						__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "link-layer type isn't supported in savefiles");
						printf("link-layer type isn't supported in savefiles");
						return (NULL);
					}
					linktype |= p->linktype_ext;

					if (fname[0] == '-' && fname[1] == '\0') {
						f = stdout;
						fname = "standard output";
					} else {
#if !defined(WIN32) && !defined(MSDOS)
						f = fopen(fname, "w");
#else
						//f = fopen(fname, "wb");
						f = fopen(fname, "wb")
#endif
						if (f == NULL) {
//							snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "%s: %s",
//									fname, pcap_strerror(errno));
							__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
									"lell_pcap_ppi_created file is null");
							printf("Error: file is null, pcap_open");
							return (NULL);
						}
						__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
															"lell_pcap_ppi_created file is OK, yeah");
					}
					return (pcap_setup_dump(p, linktype, f, fname));
				}

				/*
				 * Output a packet to the initialized dump file.
				 */
				void pcap_dump(u_char *user, const struct pcap_pkthdr *h,
						const u_char *sp) {
					register FILE *f;
					struct pcap_sf_pkthdr sf_hdr;

					f = (FILE *) user;
					sf_hdr.ts.tv_sec = h->ts.tv_sec;
					sf_hdr.ts.tv_usec = h->ts.tv_usec;
					sf_hdr.caplen = h->caplen;
					sf_hdr.len = h->len;
					/* XXX we should check the return status */
					(void) fwrite(&sf_hdr, sizeof(sf_hdr), 1, f);
					(void) fwrite(sp, h->caplen, 1, f);
				}

				void pcap_close(pcap_t *p) {
					if (p->opt.source != NULL)
						free(p->opt.source);
					p->cleanup_op(p);
					free(p);
				}

				void pcap_dump_close(pcap_dumper_t *p) {

#ifdef notyet
					if (ferror((FILE *)p))
					return-an-error;
					/* XXX should check return from fclose() too */
#endif
					(void) fclose((FILE *) p);
				}

				int pcap_dump_flush(pcap_dumper_t *p) {

					if (fflush((FILE *) p) == EOF)
						return (-1);
					else
						return (0);
				}

				pcap_t *
				pcap_open_dead_with_tstamp_precision(int linktype, int snaplen,
						u_int precision) {
					pcap_t *p;

					switch (precision) {

					case PCAP_TSTAMP_PRECISION_MICRO:
					case PCAP_TSTAMP_PRECISION_NANO:
						break;

					default:
						return NULL;
					}
					p = malloc(sizeof(*p));
					if (p == NULL)
						return NULL;
					memset(p, 0, sizeof(*p));
					p->snapshot = snaplen;
					p->linktype = linktype;
					p->opt.tstamp_precision = precision;
					p->stats_op = pcap_stats_dead;
#ifdef WIN32
					p->setbuff_op = pcap_setbuff_dead;
					p->setmode_op = pcap_setmode_dead;
					p->setmintocopy_op = pcap_setmintocopy_dead;
#endif
					p->cleanup_op = pcap_cleanup_dead;

					/*
					 * A "dead" pcap_t never requires special BPF code generation.
					 */
					p->bpf_codegen_flags = 0;

					p->activated = 1;
					return (p);
				}
//#endif /* ENABLE_PCAP */
