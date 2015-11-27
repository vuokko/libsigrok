/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_OWON_VDS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_OWON_VDS_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "owon-vds"
#define USB_VID_PID "5345.1234"
#define USB_CONFIG      1
#define USB_INTERFACE   0
#define USB_TIMEOUT_MS  300 /* Check this value */
#define USB_IN_ENDPOINT  0x81
#define USB_OUT_ENDPOINT 3

/* Way too low for data aqusition. Buffers are
   increased when more data starts to flow. */
#define DEFAULT_USB_IN_SIZE 100
#define DEFAULT_USB_OUT_SIZE 15

enum {
	VDS2062,
};
/* Targets to control the scope. */
enum {
	NONE, STOP, GET_VERSION,
};
struct owon_vds_model {
	int model_id;       /**< Model info */
	const char *vendor; /**< Vendor name */
	const char *name;   /**< Model name */
	const char *id;     /**< USB iSerialNumber */
};
struct owon_device {
	char * board_version;
	char * serial_number;
	guint16 areas;
	gboolean neutral;
	gboolean pluggable;
	gboolean encrypted;
	gboolean single_triggered;
	gboolean unifyclock;
};
struct owon_vds_config {
	/** The samplerate selected by the user. */
	uint64_t samplerate;

	/** The maximum sampling duration, in milliseconds. */
	uint64_t limit_msec;

	/** The maximum number of samples to acquire. */
	uint64_t limit_samples;
};
/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct owon_vds_model * model;
	/* Acquisition settings */
	struct owon_vds_config conf;
	/* Operational state */
	struct owon_vds_config status;
	struct owon_device dev_info;
	int next_target;      /**< Do next this target. */
	int current_target;   /**< Working on this target. */
	int repeat_target;    /**< Repeatedly done target. */
	/* Temporary state across callbacks */
	struct libusb_transfer *xfer_in;
	struct libusb_transfer *xfer_out;
	unsigned char * in_buf;
	int in_size;
	unsigned char * out_buf;
	int out_size;
};

/* Callback functions for libusb. */
SR_PRIV void LIBUSB_CALL owon_receive_cb(struct libusb_transfer *transfer);
SR_PRIV void LIBUSB_CALL owon_transmit_cb(struct libusb_transfer *transfer);

SR_PRIV void handle_libusb_events(struct drv_context *drvc, int count);
SR_PRIV int owon_send_cmd(struct dev_context * devc);
SR_PRIV int owon_init_transfers(struct sr_dev_inst *sdi);
SR_PRIV int owon_alloc_usb(struct dev_context * devc);
SR_PRIV void owon_free_usb(struct dev_context * devc);
SR_PRIV int owon_vds_receive_data(int fd, int revents, void *cb_data);

#endif
