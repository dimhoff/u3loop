/**
 * u3loop.c - Utilities for PassMark USB 3.0 Loopback plug - Loopback tester
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

#include "u3loop_defines.h"

#define VERSION "v0.0.0-20200321"

#define VID 0x0403
#define PID 0xff0b

#define BULK_IN	 0x81
#define BULK_OUT 0x01
#define IFNUM 0
#define ALTIFNUM 1

#define USB_TIMEOUT 2000	//2000 millisecs == 2 seconds 
#define MAX_DEVICE_WAIT 10	// Time in seconds to wait for re-enumration

#define DEFAULT_DISPLAY_IVAL 1

sig_atomic_t running = true;
sig_atomic_t timer_triggered = false;

unsigned int verbose = 0;

// Statistics counters
struct host_errors_t {
	int data_corrupt;

	int tx_stall;
	int tx_timeout;
	int tx_overflow;

	int rx_stall;
	int rx_timeout;
	int rx_overflow;
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


void terminator(__attribute__((unused)) int signum) {
	running = false;
}

void timer_cb(__attribute__((unused)) int signum)
{
	timer_triggered = true;
}

void usage(const char *name)
{
	fprintf(stderr, "Utility for USB 3.0 loopback plug - %s\n", VERSION);
	fprintf(stderr, "Usage: %s [-bvh] [-c CNT] [-i SEC] [-s SERIAL] [-S SPEED] [-t SEC]\n", name);
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, " -b        Identify device by blinking LED's and exiting\n");
	fprintf(stderr, " -c CNT    Report statistics every CNT operations\n");
	fprintf(stderr, " -i SEC    Report statistics every SEC seconds\n");
	fprintf(stderr, " -s SERIAL Use device with this serial number\n");
	fprintf(stderr, " -S SPEED  Force device to work at USB speed\n");
	fprintf(stderr, "             fs = USB 1.x Full Speed, 12 Mbit/s\n");
	fprintf(stderr, "             hs = USB 2.0 High Speed, 480 Mbit/s\n");
	fprintf(stderr, "             ss = USB 3.x Super Speed, 5 Gbit/s\n");
	fprintf(stderr, " -t SEC    Time limit of test in seconds (0=forever)\n");
	fprintf(stderr, " -v        Increase verbosity level. Can be used multiple times\n");
	fprintf(stderr, " -h        This help message\n");
}

void print_dev_phy_errors(struct u3loop_errors *ec) {
	if (ec->phy_errors & U3LOOP_ERR_PHY_DECODE)
		printf("   - U3LOOP_ERR_PHY_DECODE\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_EB_OVR)
		printf("   - U3LOOP_ERR_PHY_EB_OVR\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_EB_UND)
		printf("   - U3LOOP_ERR_PHY_EB_UND\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_DISPARITY)
		printf("   - U3LOOP_ERR_PHY_DISPARITY\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_CRC5)
		printf("   - U3LOOP_ERR_PHY_CRC5\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_CRC16)
		printf("   - U3LOOP_ERR_PHY_CRC16\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_CRC32)
		printf("   - U3LOOP_ERR_PHY_CRC32\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_TRAINING)
		printf("   - U3LOOP_ERR_PHY_TRAINING\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_LOCK_LOSS)
		printf("   - U3LOOP_ERR_PHY_LOCK_LOSS\n");
	if (ec->phy_errors & U3LOOP_ERR_PHY_UNDEFINED)
		printf("   - U3LOOP_ERR_PHY_UNDEFINED\n");
}

void print_dev_ll_errors(struct u3loop_errors *ec) {
	if (ec->ll_errors & U3LOOP_ERR_LL_HP_TIMEOUT_EN)
		printf("   - U3LOOP_ERR_LL_HP_TIMEOUT_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_RX_SEQ_NUM_ERR_EN)
		printf("   - U3LOOP_ERR_LL_RX_SEQ_NUM_ERR_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_RX_HP_FAIL_EN)
		printf("   - U3LOOP_ERR_LL_RX_HP_FAIL_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_MISSING_LGOOD_EN)
		printf("   - U3LOOP_ERR_LL_MISSING_LGOOD_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_MISSING_LCRD_EN)
		printf("   - U3LOOP_ERR_LL_MISSING_LCRD_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_CREDIT_HP_TIMEOUT_EN)
		printf("   - U3LOOP_ERR_LL_CREDIT_HP_TIMEOUT_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_PM_LC_TIMEOUT_EN)
		printf("   - U3LOOP_ERR_LL_PM_LC_TIMEOUT_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_TX_SEQ_NUM_ERR_EN)
		printf("   - U3LOOP_ERR_LL_TX_SEQ_NUM_ERR_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_HDR_ADV_TIMEOUT_EN)
		printf("   - U3LOOP_ERR_LL_HDR_ADV_TIMEOUT_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_HDR_ADV_HP_EN)
		printf("   - U3LOOP_ERR_LL_HDR_ADV_HP_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_HDR_ADV_LCRD_EN)
		printf("   - U3LOOP_ERR_LL_HDR_ADV_LCRD_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_HDR_ADV_LGO_EN)
		printf("   - U3LOOP_ERR_LL_HDR_ADV_LGO_EN\n");
	if (ec->ll_errors & U3LOOP_ERR_LL_UNDEFINED)
		printf("   - U3LOOP_ERR_LL_UNDEFINED\n");
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
	s->cum_host_errors.tx_stall     += s->host_errors.tx_stall;
	s->cum_host_errors.tx_timeout   += s->host_errors.tx_timeout;
	s->cum_host_errors.tx_overflow  += s->host_errors.tx_overflow;
	s->cum_host_errors.rx_stall     += s->host_errors.rx_stall;
	s->cum_host_errors.rx_timeout   += s->host_errors.rx_timeout;
	s->cum_host_errors.rx_overflow  += s->host_errors.rx_overflow;

	// Calculate values
	uint64_t rx_bytes = (s->ctrs.rx_bytes - s->measurement.rx_bytes);
	uint64_t ival_usec = (now.tv_sec - s->measurement_time.tv_sec) * 1000000 +
				(now.tv_nsec - s->measurement_time.tv_nsec) / 1000;

	uint64_t total_time_usec = (now.tv_sec - s->start_time.tv_sec) * 1000000 +
					(now.tv_nsec - s->start_time.tv_nsec) / 1000;

	double rx_mbps = INFINITY;
	if (ival_usec != 0) {
		rx_mbps = (rx_bytes * 8) / ival_usec;
	}
	double avg_rx_mbps = INFINITY;
	if (total_time_usec != 0) {
		avg_rx_mbps = (s->ctrs.rx_bytes * 8) / total_time_usec;
	}

	int host_errors =
		s->host_errors.data_corrupt +
		s->host_errors.tx_stall +
		s->host_errors.tx_timeout +
		s->host_errors.tx_overflow +
		s->host_errors.rx_stall +
		s->host_errors.rx_timeout +
		s->host_errors.rx_overflow;

	printf("% 4ld.0, % 8lld, %7.2f, %7.2f, % 4d, % 4d, 0x%04x, % 4d, 0x%04x\n", total_time_usec / 1000000, s->ops, rx_mbps, avg_rx_mbps, host_errors, s->dev_errors.phy_error_cnt, s->dev_errors.phy_errors, s->dev_errors.ll_error_cnt, s->dev_errors.ll_errors);
	
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

	double avg_rx_mbps;
	if (total_time_usec != 0) {
		avg_rx_mbps = (s->ctrs.rx_bytes * 8) / total_time_usec;
	} else {
		avg_rx_mbps = (s->ctrs.rx_bytes * 8);
	}
	double avg_ops_sec;
	if (total_time_usec > 1000000) { // prevent divide by 0
		avg_ops_sec = s->ops / (total_time_usec / 1000000);
	} else {
		avg_ops_sec = s->ops;
	}

	printf("\nTest Report:\n");
	printf("------------\n");
	printf("Test duration: %lu Sec.\n", total_time_usec / 1000000);
	printf("Total operations: %llu Ops.\n", s->ops);
	printf("\n");
	printf("Bytes send:     % 15ld\n", s->ctrs.tx_bytes);
	printf("Bytes received: % 15ld\n", s->ctrs.rx_bytes);
	printf("Bytes lost:     % 15ld\n", s->ctrs.tx_bytes - s->ctrs.rx_bytes);
	printf("\n");
	printf("Average speed: %7.2f Mbit/s\n", avg_rx_mbps);
	printf("Average rate: %7.2f Ops/s\n", avg_ops_sec);
	printf("\n");
	printf("Host Errors:\n");
	printf(" - data_corrupt: %u\n", s->cum_host_errors.data_corrupt);
	printf(" - tx_stall:     %u\n", s->cum_host_errors.tx_stall);
	printf(" - tx_timeout:   %u\n", s->cum_host_errors.tx_timeout);
	printf(" - tx_overflow:  %u\n", s->cum_host_errors.tx_overflow);
	printf(" - rx_stall:     %u\n", s->cum_host_errors.rx_stall);
	printf(" - rx_timeout:   %u\n", s->cum_host_errors.rx_timeout);
	printf(" - rx_overflow:  %u\n", s->cum_host_errors.rx_overflow);
	printf("\n");
	printf("Device Errors:\n");
	printf(" - Physical layer errors: %u\n", s->cum_dev_errors.phy_error_cnt);
	print_dev_phy_errors(&(s->cum_dev_errors));
	printf(" - Link layer errors: %u\n", s->cum_dev_errors.ll_error_cnt);
	print_dev_ll_errors(&(s->cum_dev_errors));
}

struct libusb_device_handle * open_device(char *serial_number)
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

		if (desc.idVendor != VID ||
		    desc.idProduct != PID)
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

int main(int argc, char *argv[])
{
	struct libusb_device_handle *dev;
	timer_t timer;
	int opt;
	char *endp;
	char *opt_serial_number = NULL;
	bool opt_identify = false;
	time_t opt_time_limit;
	int opt_report_ival = -1;
	long long opt_report_ops = -1;
	int opt_speed = U3LOOP_SPEED_SUPER;
	int retval = EXIT_FAILURE;
	int err;
	ssize_t len;
	int i;
	struct state_t state = { 0 };

	while ((opt = getopt(argc, argv, "bc:i:s:S:t:vh")) != -1) {
		switch (opt) {
		case 'b':
			opt_identify = true;
			break;
		case 'c':
			opt_report_ops = strtoll(optarg, &endp, 10);
			if (*endp != '\0' || opt_report_ops < 0) {
				fprintf(stderr, "Argument to '-c' must be a positive number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'i':
			opt_report_ival = strtol(optarg, &endp, 10);
			if (*endp != '\0' || opt_report_ival < 0) {
				fprintf(stderr, "Argument to '-i' must be a positive number\n");
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
		case 'v':
			verbose++;
			break;
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		default: /* '?' */
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (opt_report_ival == -1 && opt_report_ops == -1) {
		opt_report_ival = DEFAULT_DISPLAY_IVAL;
	} else if (opt_report_ival != -1 && opt_report_ops != -1) {
		fprintf(stderr, "'-i' and '-c' can not be used at a time\n");
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, &terminator);
	signal(SIGINT, &terminator);

	struct sigaction timer_action;
	timer_action.sa_handler = timer_cb;
	sigemptyset(&timer_action.sa_mask);
	timer_action.sa_flags = 0;
	sigaction(SIGALRM, &timer_action, NULL);

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
	dev = open_device(opt_serial_number);
	if (dev == NULL) {
		fprintf(stderr, "Unable to find usable loopback plug\n");
		goto fail1;
	}

	// Identify device
	if (opt_identify) {
		if (verbose) {
			printf("Blinking LEDs\n");
		}

		len = libusb_control_transfer(dev,
				LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_LEDS | U3LOOP_LED_NONE,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Failed to set LEDs: %s\n",
					libusb_error_name(len));
			goto fail2;
		}

		sleep(1);

		len = libusb_control_transfer(dev,
				LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_LEDS | U3LOOP_LED_ALL,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Failed to set LEDs: %s\n",
					libusb_error_name(len));
			goto fail2;
		}

		sleep(1);

		len = libusb_control_transfer(dev,
				LIBUSB_REQUEST_TYPE_VENDOR, 0,
				U3LOOP_CMD_SET_LEDS |
				U3LOOP_LED_PWR | U3LOOP_LED_PWR_AUTO,
				0, NULL, 0, USB_TIMEOUT);
		if (len < LIBUSB_SUCCESS) {
			fprintf(stderr, "Failed to set LEDs: %s\n",
					libusb_error_name(len));
			goto fail2;
		}

		// exit program
		retval = EXIT_SUCCESS;
		goto fail2;
	}

	// Configure device
	struct u3loop_config dev_config = {
		.mode = U3LOOP_MODE_LOOPBACK,
		.ep_type = U3LOOP_EP_TYPE_BULK,
		.ep_in = 0x01,
		.ep_out = 0x01,
		.ss_burst_len = 0x01,
		.polling_interval = 0x01,
		.hs_bulk_nak_interval = 0x00,
		.iso_transactions_per_bus_interval = 0x03,
		.iso_bytes_per_bus_interval = htole16(0xC000),
		.speed = opt_speed,
		.buffer_count = 0x40,
		.buffer_size = htole16(0x0400)
	};
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
		dev = open_device(opt_serial_number);
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

	// Disable LCD display during test
	len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
			U3LOOP_CMD_SET_DISPLAY_MODE | U3LOOP_DISPLAY_DISABLE,
			0, NULL, 0, USB_TIMEOUT);
	if (len < LIBUSB_SUCCESS) {
		fprintf(stderr, "Warning: Failed to set display mode: %s\n",
				libusb_error_name(len));
	}

	// Setup timer
	struct itimerspec alarm_time;
	alarm_time.it_value.tv_sec = 1;
	alarm_time.it_value.tv_nsec = 0;
	alarm_time.it_interval = alarm_time.it_value;
	
	if (timer_create(CLOCK_MONOTONIC, NULL, &timer) == -1) {
		perror("timer_create");
		goto fail2;
	}
	if (timer_settime(timer, 0, &alarm_time, NULL) == -1) {
		perror("timer_settime");
		goto fail2;
	}

	// Get start time
	if (clock_gettime(CLOCK_MONOTONIC, &(state.start_time)) == -1) {
		perror("clock_gettime");
		goto fail3;
	}
	state.measurement_time = state.start_time;

	// Run test
	size_t transfered;

#define BLOCK_SIZE  0x10000 // TODO: make variable. Depends on link speed???
	uint8_t txbuf[BLOCK_SIZE];
	uint8_t rxbuf[BLOCK_SIZE];
	memset(txbuf, 0xC5, BLOCK_SIZE);

	unsigned long long ops_since_last_measurement = 0;
	bool take_measurement = false;

	printf("Time, Ops, Speed(mbps), Avg. Speed(mbps), Host Error count, Phy. Error Count, Phy Error Mask, Link Error Count, Link Error Mask\n");
	while (true) {
		// TX Data
		transfered = 0;
		err = libusb_bulk_transfer(dev, BULK_OUT,
				txbuf, BLOCK_SIZE, (int *) &transfered,
				USB_TIMEOUT);
		if (err != LIBUSB_SUCCESS) {
			if (err == LIBUSB_ERROR_TIMEOUT) {
				state.host_errors.tx_timeout++;
			} else if (err == LIBUSB_ERROR_PIPE) {
				state.host_errors.tx_stall++;
			} else if (err == LIBUSB_ERROR_OVERFLOW) {
				state.host_errors.tx_overflow++;
			} else {
				fprintf(stderr, "Failed to send data to device: %s\n",
						libusb_error_name(err));
				goto fail3;
			}
		}
		state.ctrs.tx_bytes += transfered;

		// RX Data
		transfered = 0;
		err = libusb_bulk_transfer(dev, BULK_IN,
				rxbuf, BLOCK_SIZE, (int *) &transfered,
				USB_TIMEOUT);
		if (err != LIBUSB_SUCCESS) {
			if (err == LIBUSB_ERROR_TIMEOUT) {
				state.host_errors.rx_timeout++;
			} else if (err == LIBUSB_ERROR_PIPE) {
				state.host_errors.rx_stall++;
			} else if (err == LIBUSB_ERROR_OVERFLOW) {
				state.host_errors.rx_overflow++;
			} else {
				fprintf(stderr, "Failed to receive data from device: "
						"%s\n", libusb_error_name(err));
				goto fail3;
			}
		}
		state.ctrs.rx_bytes += transfered;

		if (memcmp(txbuf, rxbuf, BLOCK_SIZE) != 0) {
			state.host_errors.data_corrupt++;
		}

		// Count operations
		state.ops++;
		if (opt_report_ops > 0 &&
		    ++ops_since_last_measurement >= (unsigned long long) opt_report_ops)
		{
			take_measurement = true;
			ops_since_last_measurement = 0;
		}

		// Service periodic things, every second
		if (timer_triggered) {
			timer_triggered = false;

			struct timespec now;
			if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
				perror("clock_gettime");
				goto fail3;
			}
			time_t time_running =  now.tv_sec - state.start_time.tv_sec;

			if (opt_time_limit > 0 && time_running >= opt_time_limit) {
				running = false;
			}

			if (opt_report_ival > 0 && time_running % opt_report_ival == 0) {
				take_measurement = true;
			}
		}

		// Take Measurement
		if (take_measurement || !running) {
			take_measurement = false;

			// Update device error counters
			len = libusb_control_transfer(dev,
					LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN, 0,
					U3LOOP_CMD_GET_ERROR_COUNTERS, 0,
					(unsigned char *) &state.dev_errors, sizeof(state.dev_errors),
					USB_TIMEOUT);
			// FIXME: add support for big endian platforms
			if (len < 0) {
				fprintf(stderr, "Warning: Unable to obtain error counters: "
						"%s\n", libusb_error_name(len));
			} else if (len != sizeof(state.dev_errors)) {
				fprintf(stderr, "Warning: Unable to obtain error counters: "
						"incorrect size data returned\n");
/* NOT needed? GET_ERROR_COUNTERS also resets counters
			} else {
				// Reset device counters
				len = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
						U3LOOP_CMD_RESET_ERROR_COUNTERS,
						0, NULL, 0, USB_TIMEOUT);
				if (len < LIBUSB_SUCCESS) {
					fprintf(stderr, "Warning: Unable to reset error counters: "
							"%s\n", libusb_error_name(len));
				}
*/
			}

			// Print measurement
			print_measurement(&state);

			// Exit loop if not running.
			// NOTE: Using 'while (running)' would be more logical,
			// but leaves a race where the TERM signal is delivered
			// just after the enclosing if and just before checking
			// the while argument.
			if (!running) {
				break;
			}
		}
	};

	// Cumulative error report
	print_report(&state);

	retval = EXIT_SUCCESS;

fail3:
	if (timer_delete(timer) == -1) {
		perror("timer_delete");
		exit(-1);
	}
fail2:
	// Enable LCD display again
	libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
			U3LOOP_CMD_SET_DISPLAY_MODE | U3LOOP_DISPLAY_ENABLE,
			0, NULL, 0, USB_TIMEOUT);

	// Enable Link Power Management
	libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_VENDOR, 0,
			U3LOOP_CMD_CONF_LPM | U3LOOP_LPM_ENTRY_ENABLE,
			0, NULL, 0, USB_TIMEOUT);

	libusb_release_interface(dev, IFNUM);
	libusb_close(dev);
fail1:
	libusb_exit(NULL);
fail0:
	return retval;
}
	
