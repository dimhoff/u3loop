/**
 * u3bench.c - Utilities for PassMark USB 3.0 Loopback plug - Benchmarker
 *
 * Copyright (c) 2020 David Imhoff <dimhoff.devel@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _POSIX_C_SOURCE 200809L
#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include <libusb.h>
#include <math.h>
#include <assert.h>

#include "u3loop_defines.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define VERSION "v0.0.0-20210220"

#define BULK_IN	 (0x01 | LIBUSB_ENDPOINT_IN)
#define BULK_OUT 0x01
#define IFNUM 0
#define ALTIFNUM 1

#define BUFFER_CNT 64      // Amount of transfers to submit to libusb; must be an even number.
#define DEFAULT_TRANSFER_SIZE  (2*1024*1024)  // Amount of bytes to read/write at a time

#define USB_TIMEOUT 2000	//2000 millisecs == 2 seconds 
#define MAX_DEVICE_WAIT 10	// Time in seconds to wait for re-enumration

#define DEFAULT_DISPLAY_IVAL 1

#if (BUFFER_CNT & 1)
# error "BUFFER_CNT must be a multiple of 2"
#endif

int terminate = false;

unsigned int verbose = 0;

// Statistics counters
struct host_errors_t {
	int data_corrupt;

	int error;
	int length;
	int stall;
	int timeout;
	int overflow;
};

struct stat_counters {
	uint64_t tx_bytes;
	uint64_t rx_bytes;
};

// Current statistics state
struct state_t {
	//***** Written by Main *****//
	// Start time
	struct timespec start_time;

	// # of transfers submitted to libusb
	unsigned int active_transfers;

	// operations counter
	unsigned long long ops;

	// counters
	struct stat_counters ctrs;

	// host error counters, since last measurement
	struct host_errors_t host_errors;
	// Device error counters, since last measurement
	struct u3loop_errors dev_errors;

	//***** Written by measurement *****//
	// host error counters, since start
	struct host_errors_t cum_host_errors;
	// Device error counters, since start
	struct u3loop_errors cum_dev_errors;

	// Time of last measurement
	struct timespec measurement_time;
	// Counters at last measurement
	struct stat_counters measurement;
};

struct test_device_type {
	int id;
	char *name;
	uint16_t vid; /**< Default USB Vendor ID **/
	uint16_t pid; /**< Default USB Product ID **/
};


enum test_device_type_ids {
	TEST_DEV_NONE = 0,
	TEST_DEV_PASSMARK,
	TEST_DEV_FX3
};
struct test_device_type test_device_types[] = {
	{ TEST_DEV_PASSMARK, "passmark", 0x0403, 0xff0b },
	{ TEST_DEV_FX3     , "fx3"     , 0x04b4, 0x00f1 },
	{ TEST_DEV_NONE, NULL, 0, 0 }
};

void terminator(__attribute__((unused)) int signum) {
	terminate = true;
}

void usage()
{
	fprintf(stderr, "Benchmark test for USB 3.0 loopback plug - %s\n", VERSION);
	fprintf(stderr, "Usage: u3bench [-vh] [-i SEC] [-I VID:PID] [-l SIZE] [-m MODE]\n"
			"               [-s SERIAL] [-S SPEED] [-t SEC] [-T TYPE]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, " -i SEC     Report statistics every SEC seconds\n");
	fprintf(stderr, " -I VID:PID Use specific device by USB vendor and product ID\n");
	fprintf(stderr, " -l SIZE    Set transfer size\n");
	fprintf(stderr, " -m MODE    Test mode\n");
	fprintf(stderr, "              rw = Read and write (Default)\n");
	fprintf(stderr, "              r  = Read\n");
	fprintf(stderr, "              w  = Write\n");
	fprintf(stderr, " -s SERIAL  Use device with this serial number\n");
	fprintf(stderr, " -S SPEED   Force device to work at USB speed\n");
	fprintf(stderr, "              fs = USB 1.x Full Speed, 12 Mbit/s\n");
	fprintf(stderr, "              hs = USB 2.0 High Speed, 480 Mbit/s\n");
	fprintf(stderr, "              ss = USB 3.x Super Speed, 5 Gbit/s\n");
	fprintf(stderr, " -t SEC     Time limit of test in seconds (0=forever)\n");
	fprintf(stderr, " -T TYPE    Test device type(use 'list' for available options)\n");
	fprintf(stderr, " -v         Increase verbosity level. Can be used multiple times\n");
	fprintf(stderr, " -h         This help message\n");
}

void usage_device_types()
{
	fprintf(stderr, "Supported device types:\n");
	fprintf(stderr, "  passmark - Passmark USB 3.0 loopback tester\n");
	fprintf(stderr, "  fx3 - Cypress FX3/CX3 with cyfxbulksrcsink example firmware\n");
}

void print_measurement(struct state_t *s)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
		perror("clock_gettime");
		return;
	}

	// Update cumulative counters
	s->cum_dev_errors.phy_error_cnt += s->dev_errors.phy_error_cnt;
	s->cum_dev_errors.phy_errors    |= s->dev_errors.phy_errors;
	s->cum_dev_errors.ll_error_cnt  += s->dev_errors.ll_error_cnt;
	s->cum_dev_errors.ll_errors     |= s->dev_errors.ll_errors;

	s->cum_host_errors.data_corrupt += s->host_errors.data_corrupt;
	s->cum_host_errors.error     += s->host_errors.error;
	s->cum_host_errors.length    += s->host_errors.length;
	s->cum_host_errors.stall     += s->host_errors.stall;
	s->cum_host_errors.timeout   += s->host_errors.timeout;
	s->cum_host_errors.overflow  += s->host_errors.overflow;

	// Calculate values
	uint64_t tx_bytes = (s->ctrs.tx_bytes - s->measurement.tx_bytes);
	uint64_t rx_bytes = (s->ctrs.rx_bytes - s->measurement.rx_bytes);
	uint64_t ival_usec = (now.tv_sec - s->measurement_time.tv_sec) * 1000000 +
				(now.tv_nsec - s->measurement_time.tv_nsec) / 1000;

	uint64_t total_time_usec = (now.tv_sec - s->start_time.tv_sec) * 1000000 +
					(now.tv_nsec - s->start_time.tv_nsec) / 1000;

	double mbps = INFINITY;
	double tx_mbps = INFINITY;
	double rx_mbps = INFINITY;
	if (ival_usec != 0) {
		mbps = (tx_bytes + rx_bytes) * 8 / ival_usec;
		tx_mbps = tx_bytes * 8 / ival_usec;
		rx_mbps = rx_bytes * 8 / ival_usec;
	}
	double avg_mbps = INFINITY;
	double tx_avg_mbps = INFINITY;
	double rx_avg_mbps = INFINITY;
	if (total_time_usec != 0) {
		avg_mbps = (s->ctrs.rx_bytes + s->ctrs.tx_bytes) * 8 / total_time_usec;
		tx_avg_mbps = s->ctrs.tx_bytes * 8 / total_time_usec;
		rx_avg_mbps = s->ctrs.rx_bytes * 8 / total_time_usec;
	}

	int host_errors =
		s->host_errors.data_corrupt +
		s->host_errors.error +
		s->host_errors.length +
		s->host_errors.stall +
		s->host_errors.timeout +
		s->host_errors.overflow;

	printf("% 4ld.0, % 8lld, %7.2f, %7.2f, "
		"%7.2f, %7.2f, %7.2f, %7.2f, "
		"% 4d\n",
		total_time_usec / 1000000, s->ops, mbps, avg_mbps,
		tx_mbps, tx_avg_mbps, rx_mbps, rx_avg_mbps,
		host_errors);
	
	// Clear non cumulative error counters
	memset(&s->dev_errors, 0, sizeof(s->dev_errors));
	memset(&s->host_errors, 0, sizeof(s->host_errors));

	s->measurement_time = now;
	s->measurement = s->ctrs;
}

void print_report(struct state_t *s)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
		perror("clock_gettime");
		return;
	}

	uint64_t total_time_usec = (now.tv_sec - s->start_time.tv_sec) * 1000000 +
					(now.tv_nsec - s->start_time.tv_nsec) / 1000;

	double avg_mbps;
	double tx_avg_mbps;
	double rx_avg_mbps;
	if (total_time_usec != 0) {
		avg_mbps = (s->ctrs.tx_bytes + s->ctrs.rx_bytes) * 8 / total_time_usec;
		tx_avg_mbps = s->ctrs.tx_bytes * 8 / total_time_usec;
		rx_avg_mbps = s->ctrs.rx_bytes * 8 / total_time_usec;
	} else {
		avg_mbps = (s->ctrs.tx_bytes + s->ctrs.rx_bytes) * 8;
		tx_avg_mbps = s->ctrs.tx_bytes * 8;
		rx_avg_mbps = s->ctrs.rx_bytes * 8;
	}

	printf("\nTest Report:\n");
	printf("------------\n");
	printf("Test duration: %lu Sec.\n", total_time_usec / 1000000);
	printf("Total operations: %llu Ops.\n", s->ops);
	printf("\n");
	printf("Bytes written: % 15ld\n", s->ctrs.tx_bytes);
	printf("Bytes read:    % 15ld\n", s->ctrs.rx_bytes);
	printf("\n");
	printf("Average speed:       %7.2f Mbit/s\n", avg_mbps);
	printf("Average write speed: %7.2f Mbit/s\n", tx_avg_mbps);
	printf("Average read speed:  %7.2f Mbit/s\n", rx_avg_mbps);
	printf("\n");
	printf("Host Errors:\n");
	printf(" - data_corrupt: %u\n", s->cum_host_errors.data_corrupt);
	printf(" - generic:   %u\n", s->cum_host_errors.error);
	printf(" - length:    %u\n", s->cum_host_errors.length);
	printf(" - stall:     %u\n", s->cum_host_errors.stall);
	printf(" - timeout:   %u\n", s->cum_host_errors.timeout);
	printf(" - overflow:  %u\n", s->cum_host_errors.overflow);
}

struct libusb_device_handle * open_device(uint16_t vid, uint16_t pid, char *serial_number)
{
	struct libusb_device_handle *dev;
	libusb_device **devs;
	ssize_t cnt;
	int i;
	int err;

	cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0) {
		fprintf(stderr, "Failed to get USB device list: %s\n",
				libusb_error_name(cnt));
		return NULL;
	}

	bool found = false;
	for (i=0; i < cnt && !found; i++) {
		struct libusb_device_descriptor desc;
		err = libusb_get_device_descriptor(devs[i], &desc);
		if (err != LIBUSB_SUCCESS) {
			continue;
		}

		if (desc.idVendor != vid ||
		    desc.idProduct != pid)
		{
			continue;
		}

		err = libusb_open(devs[i], &dev);
		if (err != LIBUSB_SUCCESS) {
			if (verbose) {
				fprintf(stderr, "Unable to open device: %s\n", libusb_error_name(err));
			}
			continue;
		}

		char serial_str[256];
		err = libusb_get_string_descriptor_ascii(dev,
					desc.iSerialNumber,
					(unsigned char *)serial_str,
					sizeof(serial_str));
		if (err == LIBUSB_SUCCESS) {
			if (verbose) {
				fprintf(stderr, "Unable to get serial number: %s\n", libusb_error_name(err));
			}
			libusb_close(dev);
			continue;
		}

		if (serial_number == NULL ||
				strcmp(serial_str, serial_number) == 0)
		{
			found = true;
			if (verbose) {
				printf("Found Device @ bus: %u, device: %u, s/n: %s\n",
						       libusb_get_bus_number(devs[i]),
						       libusb_get_device_address(devs[i]),
						       serial_str);
			}
		} else {
			libusb_close(dev);
		}
	}
	libusb_free_device_list(devs, 1);

	if (!found) {
		return NULL;
	}

	err = libusb_claim_interface(dev, IFNUM);
	if (err != LIBUSB_SUCCESS) {
		fprintf(stderr, "Failed to claim device interface: %s\n",
				libusb_error_name(err));
		libusb_close(dev);
		return NULL;
	}

	return dev;
}

void transfer_cb(struct libusb_transfer *transfer)
{
	struct state_t *state = (struct state_t *) transfer->user_data;
	assert(state != NULL);
	bool is_tx = ((transfer->endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT);

	state->active_transfers--;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		state->ops++;

		// TODO: verify data ??
		if (transfer->length != transfer->actual_length) {
			state->host_errors.length++;
		}

		if (is_tx) {
			state->ctrs.tx_bytes += transfer->actual_length;
		} else {
			state->ctrs.rx_bytes += transfer->actual_length;
		}
		break;
	case LIBUSB_TRANSFER_ERROR:
		state->host_errors.error++;
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		state->host_errors.timeout++;
		break;
	case LIBUSB_TRANSFER_STALL:
		state->host_errors.stall++;
		break;
	case LIBUSB_TRANSFER_OVERFLOW:
		state->host_errors.overflow++;
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
		fprintf(stderr, "Device disconnected\n");
		terminate = true;
		break;
	case LIBUSB_TRANSFER_CANCELLED:
		return; // Stop on cancellation of transfer
	default:
		assert(false);
	}

	if (!terminate) {
		if (libusb_submit_transfer(transfer) == LIBUSB_SUCCESS) {
			state->active_transfers++;
		}
	}
}

int main(int argc, char *argv[])
{
	struct libusb_device_handle *dev;
	int opt;
	char *endp;
	char *opt_serial_number = NULL;
	time_t opt_time_limit;
	int opt_report_ival = DEFAULT_DISPLAY_IVAL;
	int opt_speed = U3LOOP_SPEED_SUPER;
	int opt_mode = U3LOOP_MODE_READ_WRITE;
	size_t opt_transfer_size = DEFAULT_TRANSFER_SIZE;
	uint16_t opt_vid = 0;
	uint16_t opt_pid = 0;
	struct test_device_type *opt_test_device = &(test_device_types[0]);
	int retval = EXIT_FAILURE;
	int err;
	ssize_t len;
	int i;
	struct state_t state = { 0 };

	while ((opt = getopt(argc, argv, "i:I:d:l:m:s:S:t:T:vh")) != -1) {
		switch (opt) {
		case 'i':
			opt_report_ival = strtol(optarg, &endp, 10);
			if (*endp != '\0' || opt_report_ival < 0) {
				fprintf(stderr, "Argument to '-i' must be a positive number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'I':
			if (strlen(optarg) != 9 || optarg[4] != ':') {
				fprintf(stderr, "Illegal VID PID comibantion. Use format: VVVV:PPPP\n");
				exit(EXIT_FAILURE);
			}
			opt_vid = strtoul(optarg, NULL, 16);
			opt_pid = strtoul(&optarg[5], NULL, 16);
			break;
		case 'l':
			opt_transfer_size = strtoul(optarg, &endp, 10);
			if (*endp != '\0') {
				fprintf(stderr, "Argument to '-l' must be numeric\n");
				exit(EXIT_FAILURE);
			}
			if (opt_transfer_size % 1024) {
				// NOTE: cyfxbulksrcsink firmware 'hangs' if reading partial packets, default packet size is 1024
				fprintf(stderr, "WARNING: transfer size not a multiple of 1024, this might not work\n");
			}
			break;
		case 'm':
			if (strcasecmp(optarg, "r") == 0) {
				opt_mode = U3LOOP_MODE_READ;
			} else if (strcasecmp(optarg, "w") == 0) {
				opt_mode = U3LOOP_MODE_WRITE;
			} else if (strcasecmp(optarg, "rw") == 0) {
				opt_mode = U3LOOP_MODE_READ_WRITE;
			} else {
				fprintf(stderr, "Invalid argument for '-m' option\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			opt_serial_number = optarg;
			break;
		case 'S':
			if (strcasecmp(optarg, "fs") == 0) {
				opt_speed = U3LOOP_SPEED_FULL;
			} else if (strcasecmp(optarg, "hs") == 0) {
				opt_speed = U3LOOP_SPEED_HIGH;
			} else if (strcasecmp(optarg, "ss") == 0) {
				opt_speed = U3LOOP_SPEED_SUPER;
			} else {
				fprintf(stderr, "Invalid argument for '-S' option\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			opt_time_limit = strtoul(optarg, &endp, 10);
			if (*endp != '\0') {
				fprintf(stderr, "Argument to '-t' must be numeric\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'T':
			if (strcasecmp(optarg, "list") == 0) {
				usage_device_types();
				exit(EXIT_SUCCESS);
			}
			struct test_device_type *tdt_p = test_device_types;
			opt_test_device = NULL;
			while (tdt_p->id != TEST_DEV_NONE) {
				if (strcasecmp(optarg, tdt_p->name) == 0) {
					opt_test_device = tdt_p;
					break;
				}
				tdt_p++;
			}
			if (opt_test_device == NULL) {
				fprintf(stderr, "Unknown device type\n");
				usage_device_types();
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		default: /* '?' */
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (opt_vid == 0) {
		opt_vid = opt_test_device->vid;
		opt_pid = opt_test_device->pid;
	}

	signal(SIGTERM, &terminator);
	signal(SIGINT, &terminator);

	// Init LibUSB
	err = libusb_init(NULL);
	if (err != LIBUSB_SUCCESS) {
		fprintf(stderr, "Failed to initialize LibUSB: %s\n",
				libusb_error_name(err));
		goto fail0;
	}
#if (LIBUSB_API_VERSION >= 0x01000106)
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, verbose);
#else
	libusb_set_debug(NULL, verbose);
#endif


	// Find device and open it
	if (verbose >= 2) {
		printf("Looking for device of type '%s', id: %04x:%04x, sn: %s\n",
				opt_test_device->name,
				opt_vid, opt_pid,
				(opt_serial_number != NULL) ?
					opt_serial_number : "*");
	}
	dev = open_device(opt_vid, opt_pid, opt_serial_number);
	if (dev == NULL) {
		fprintf(stderr, "Unable to find usable loopback plug\n");
		goto fail1;
	}

	if (opt_test_device->id == TEST_DEV_PASSMARK) {
		// Configure device
		struct u3loop_config dev_config = {
			.mode = opt_mode,
			.ep_type = U3LOOP_EP_TYPE_BULK,
			.ep_in = BULK_IN & LIBUSB_ENDPOINT_ADDRESS_MASK,
			.ep_out = BULK_OUT & LIBUSB_ENDPOINT_ADDRESS_MASK,
			.ss_burst_len = 0x10,
			.polling_interval = 0x01,
			.hs_bulk_nak_interval = 0x00,
			.iso_transactions_per_bus_interval = 0x03,
			.iso_bytes_per_bus_interval = htole16(0xC000), // Depends on burst length
			.speed = opt_speed,
			.buffer_count = 0x02, // from USB3Test
			.buffer_size = htole16(0xc000) // 0xc000 for read or write; 0x6000 for read and write
		};
		if (opt_mode == U3LOOP_MODE_READ_WRITE) {
			dev_config.buffer_size = htole16(0x6000);
		}
		len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_CONFIG, 0,
				(unsigned char *) &dev_config, sizeof(dev_config),
				USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Failed to configure device for test: %s\n",
					libusb_error_name(len));
			goto fail2;
		}
		libusb_release_interface(dev, IFNUM);
		libusb_close(dev);
		dev = NULL;

		if (verbose) {
			printf("Waiting for device to re-enumrate\n");
		}

		for (i=0; dev == NULL && i < MAX_DEVICE_WAIT; i++) {
			sleep(1);
			// FIXME: if multiple adapters are connected; and no
			// opt_serial_number is specified this breaks!!! get serial of
			// previously opened device...
			dev = open_device(opt_vid, opt_pid, opt_serial_number);
		}

		if (dev == NULL) {
			fprintf(stderr, "Timeout waiting for device to re-enumerate\n");
			goto fail1;
		}

		// Disable Link Power Management
		len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_CONF_LPM | U3LOOP_LPM_ENTRY_DISABLE,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Warning: Failed to set LPM entry mode: %s\n",
					libusb_error_name(len));
		}

		/*
		// Enable Error counters
		struct u3loop_error_cfg err_cfg = {
			.phy_err_mask = htole16(0x1ff),
			.ll_err_mask = htole16(0x7fff)
		};
		len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_CONF_ERROR_COUNTERS, 0,
				(unsigned char *) &err_cfg, sizeof(err_cfg),
				USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Warning: Unable to enable error counters:"
					" %s\n", libusb_error_name(len));
		}
		len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_RESET_ERROR_COUNTERS,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Warning: Unable to reset error counters: "
					"%s\n", libusb_error_name(len));
		}
		*/

		// Disable LCD display during test
		len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_DISPLAY_MODE | U3LOOP_DISPLAY_DISABLE,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Warning: Failed to set display mode: %s\n",
					libusb_error_name(len));
		}
	}

	// Get start time
	if (clock_gettime(CLOCK_MONOTONIC, &(state.start_time)) == -1) {
		perror("clock_gettime");
		goto fail2;
	}
	state.measurement_time = state.start_time;

	// Allocate and submit USB transfers
	int use_dev_mem = -1;
	struct libusb_transfer *xfers[BUFFER_CNT];
	for (i=0; (size_t) i < ARRAY_SIZE(xfers); i++) {
		xfers[i] = libusb_alloc_transfer(0);
		if (xfers[i] == NULL) {
			fprintf(stderr, "Failed to allocate transfer\n");
			goto fail3;
		}

		uint8_t *buf = NULL;
#if LIBUSB_API_VERSION >= 0x01000105 && WITH_USE_DEV_MEM
		if (use_dev_mem) {
			buf = (uint8_t *) libusb_dev_mem_alloc(opt_transfer_size);
			if (buf == NULL) {
				if (use_dev_mem == -1) {
					// If first time allocation fails then
					// DMA is probably not supported on
					// this platform. So disable.
					if (verbose) printf("DMA not supported,"
						" using malloc() instead\n");
					use_dev_mem = 0;
				} else {
					fprintf(stderr, "Failed to allocate "
							"DMA buffer\n");
					libusb_free_transfer(xfers[i]);
					xfers[i] = NULL;
					goto fail3;
				}
			} else {
				if (verbose) printf("DMA supported, using "
						"libusb_dev_mem_alloc()\n");
				use_dev_mem = 1;
			}
		}
#else
		(void) use_dev_mem;
#endif // LIBUSB_API_VERSION >= 0x01000105 && WITH_USE_DEV_MEM

		if (buf == NULL) {
			buf = (uint8_t *) malloc(opt_transfer_size);
			if (buf == NULL) {
				perror("malloc()");
				libusb_free_transfer(xfers[i]);
				xfers[i] = NULL;
				goto fail3;
			}
		}

		memset(buf, 0xC5, opt_transfer_size);

		// Determine endpoint
		int ep;
		if (opt_mode == U3LOOP_MODE_READ) {
			ep = BULK_IN;
		} else if (opt_mode == U3LOOP_MODE_WRITE) {
			ep = BULK_OUT;
		} else if (opt_mode == U3LOOP_MODE_READ_WRITE) {
			if (i & 1) {
				ep = BULK_OUT;
			} else {
				ep = BULK_IN;
			}
		}

		libusb_fill_bulk_transfer(xfers[i], dev, ep, buf,
				opt_transfer_size, transfer_cb, &state, USB_TIMEOUT);

		if (libusb_submit_transfer(xfers[i]) == LIBUSB_SUCCESS) {
			state.active_transfers++;
		}
	}


	printf("Time, Ops, "
		"Speed(mbps), Avg. Speed(mbps), "
		"TX Speed(mbps), TX Avg. Speed(mbps), "
		"RX Speed(mbps), RX Avg. Speed(mbps), "
		"Host Error count\n");

	// Main loop
	bool take_measurement = false;
	struct timeval onesec = { 1, 0 };
	time_t last_time_running = 0;
	while (!terminate) {
		err = libusb_handle_events_timeout_completed(NULL, &onesec, &terminate);

		// Service periodic things, every second
		// TODO: use timer to set _take_measurement_. To prevent
		// calling clock_gettime() a lot on platforms that still use a
		// slow library/kernel call for this.
		struct timespec now;
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
			perror("clock_gettime");
			goto fail3;
		}
		time_t time_running =  now.tv_sec - state.start_time.tv_sec;
		if (now.tv_nsec < state.start_time.tv_nsec) {
			time_running -= 1;
		}

		if (time_running != last_time_running) {
			last_time_running = time_running;
			if (opt_time_limit > 0 && time_running >= opt_time_limit) {
				terminate = true;
			}

			if (opt_report_ival > 0 && time_running % opt_report_ival == 0) {
				take_measurement = true;
			}
		}

		// Take Measurement
		if (take_measurement || terminate) {
			take_measurement = false;

			// Print measurement
			print_measurement(&state);
		}
	}

	// Cumulative error report
	print_report(&state);

	retval = EXIT_SUCCESS;

fail3:
	// Cancel all submitted transfers
	for (i=0; (size_t) i < ARRAY_SIZE(xfers); i++) {
		if (xfers[i] != NULL) {
			libusb_cancel_transfer(xfers[i]);
		}
	}
	while (state.active_transfers != 0) {
		libusb_handle_events(NULL);
	}

	// Free transfers
	for (i=0; (size_t) i < ARRAY_SIZE(xfers); i++) {
		if (xfers[i]->buffer == NULL) continue;

#if LIBUSB_API_VERSION >= 0x01000105 && WITH_USE_DEV_MEM
		if (use_dev_mem) {
			libusb_dev_mem_free(xfers[i]->buffer);
		} else
#endif // LIBUSB_API_VERSION >= 0x01000105 && WITH_USE_DEV_MEM
		{
			free(xfers[i]->buffer);
		}

		libusb_free_transfer(xfers[i]);
	}
fail2:
	if (opt_test_device->id == TEST_DEV_PASSMARK) {
		// Enable LCD display again
		libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_DISPLAY_MODE | U3LOOP_DISPLAY_ENABLE,
				0, NULL, 0, USB_TIMEOUT);

		// Enable Link Power Management
		libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_CONF_LPM | U3LOOP_LPM_ENTRY_ENABLE,
				0, NULL, 0, USB_TIMEOUT);
	}

	libusb_release_interface(dev, IFNUM);
	libusb_close(dev);
fail1:
	libusb_exit(NULL);
fail0:
	return retval;
}
	
