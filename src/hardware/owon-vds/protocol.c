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

#include <config.h>
#include <string.h>
#include "protocol.h"

/* Languages or counttries */
static const char * owon_areas[16] = {
	"China", "Taiwan", "English", "French",
	"Spain", "Russia", "Germany", "Poland",
	"Brazil", "Italy", "Japan", "Korea"
};
/** Parses version reply from scope.
 @return negative if couldn't parse, 0 if no more packets
 coming for version reply and positive value is next expected
 packet size. */

static int parse_version(struct dev_context * devc){
	char * buf, property;
	int len, header_len, i, p;
	const char header[] = ":SDSLVERSPB";
	char * substr;

	buf = (char *)devc->in_buf;
	len = devc->xfer_in->actual_length;
	header_len = strlen(header);
	if (len < header_len) return -1;
	if (strncmp(buf, header, header_len) == 0){
		sr_dbg("Received header for version reply");
		if (strncmp(buf + header_len, devc->model->name,
			strlen(devc->model->name)) == 0){
			sr_dbg("Recieved model name: %s", devc->model->name);

			len = 0;
			for(i = 0; i < 4; i++){
				len |= (buf[header_len +
					strlen(devc->model->name) + i] << ((3 - i) * 8));
			}
			len++; /* This is safequard for adding null in second packet.*/
		}
	} else {
		/* We have the second packet. Hopefully this doesn't come during
		acquisition. */
		buf[len] = 0; /* End the reply string.*/
		/* Simple check */
		if (buf[0] != '1' || buf[len -1] != ';') return -1;

		property = buf[0];
		p = 0;
		do {
			for (i = p; i < len; i++){
				if (buf[i] == ';'){
					break;
				}
			}
			/* Test that we hit semicolon before end.
			 * If not, this isn't second version message. */
			if (i == len - 1 && property != '8') return -1;

			switch (property)
			{
				case '1':/* Board version */
				case '2':/* Serial number */
					substr = g_malloc0(i - p);
					strncpy(substr, buf + p + 1, /* Skip property. */
					 i - p - 2 ); /* 2 is for property and semicolon. */
					if (property == '1')
						devc->dev_info.board_version = substr;
					else
						devc->dev_info.serial_number = substr;
					break;
				case '3': /* Areas supported. */
					p++; /* Skip property. */
					while (p < i){
						if (buf[p + 1])
							devc->dev_info.areas |= (1 << (buf[p]&0x0f));
						else
							devc->dev_info.areas &= ~(1 << (buf[p]&0x0f));
						p += 2;
					}
					break;
				case '4': /* Neutral. Don't know yet what this is. */
					devc->dev_info.neutral = buf[i - 1] ? TRUE: FALSE;
					break;
				case '5':
					devc->dev_info.pluggable = buf[i - 1] ? TRUE: FALSE;
					break;
				case '6':
					devc->dev_info.encrypted = buf[i - 1] ? TRUE: FALSE;
					break;
				case '7':
					devc->dev_info.single_triggered = buf[i - 1] ? TRUE: FALSE;
					break;
				case '8':
					devc->dev_info.unifyclock = buf[i - 1] ? TRUE: FALSE;
					break;
				default:
					;
			}
			p = i + 1;
			property = buf[p];
		}while(property <= '8' && property > '0');
		sr_spew("Board version: %s", devc->dev_info.board_version);
		sr_spew("Serial number: %s", devc->dev_info.serial_number);
		sr_spew("Languages supported by device:");
		i = 0;
		while (owon_areas[i] && i < 16){
			if (devc->dev_info.areas & (1 << i))
				sr_spew("    %s",  owon_areas[i]);
			i++;
		}
		sr_spew("Device %s neutral.", devc->dev_info.neutral ? "is" : "isn't");
		sr_spew("Device %s plugsupport.", devc->dev_info.pluggable ? "has" :
			"hasn't");
		sr_spew("Device %s encrypted firmware.", devc->dev_info.encrypted ?
			"has" : "doesn't have");
		sr_spew("Device %s new single trigger.",
			devc->dev_info.single_triggered ? "has" : "doesn't have");
		sr_spew("Device %s unified clock.", devc->dev_info.unifyclock ?
			"has" : "doesn't have");
		len = 0;
	}
	return len;
}
SR_PRIV void LIBUSB_CALL owon_transmit_cb(struct libusb_transfer *transfer)
{
	struct dev_context * devc;

	devc = transfer->user_data;
	devc->current_target = devc->next_target;
	devc->next_target = NONE;
	sr_spew("Transmit transfer called status: %d", transfer->status);
}
SR_PRIV void LIBUSB_CALL owon_receive_cb(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context * devc;
	int ret, next_size;

	sdi = transfer->user_data;
	devc = sdi->priv;
	next_size = 0;
	if(transfer->status != LIBUSB_TRANSFER_COMPLETED){
		/* Stop everything. Something went wrong. */
		devc->current_target = NONE;
		devc->next_target = NONE;
		devc->repeat_target = NONE;
		sr_warn("USB reply failed: %d", transfer->status);
	}
	switch (devc->current_target){
		case NONE:
		break;
		case STOP:
		break;
		case GET_VERSION:
			next_size = parse_version(devc);
		break;
	}
	if (next_size > devc->in_size){
		devc->in_size = next_size;
		owon_alloc_usb(devc);
	}
	if (next_size > 0){
		if ((ret = libusb_submit_transfer(devc->xfer_in)) != 0){
			sr_err("Submitting reply to Libusb failed: %s",
				libusb_error_name(ret));
		}
	}
}
/** Helper function to handle libusb events.
 During acquisition these are handled by sigrok in reception function.
 @param How many times events are handled. (Sum of packets to send and receive)
 @internal */
SR_PRIV void handle_libusb_events(struct drv_context *drvc, int count)
{
	struct timeval tv;
	int ret;

	while(count > 0){
		tv.tv_sec = 2 * USB_TIMEOUT_MS / 1000;
		tv.tv_usec = 2* USB_TIMEOUT_MS % 1000 * 1000;
		if ((ret = libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx,
			&tv)) != 0){
			sr_warn("unable handle reply: %s",
				libusb_error_name(ret));
			break;
		}
		count--;
	}
}
/** Sends command to USB based on status of devc.
 @internal */
SR_PRIV int owon_send_cmd(struct dev_context * devc)
{
	int ret;

	if(devc->current_target != NONE) {
		sr_warn("Pending USB message.");
		return SR_ERR;
	}
	switch (devc->next_target){
		case STOP:
		break;
		case GET_VERSION:
			strncpy((char *) devc->out_buf, ":SDSLVER#", devc->out_size);
			devc->xfer_out->length = strnlen(devc->out_buf, devc->out_size);
		break;
		case NONE:
		default:
		return SR_OK;
	}
	if ((ret = libusb_submit_transfer(devc->xfer_out)) != 0){
		sr_err("Submitting command to Libusb failed: %s",
			libusb_error_name(ret));
		return SR_ERR;
	}
	if ((ret = libusb_submit_transfer(devc->xfer_in)) != 0){
		sr_err("Submitting reply to Libusb failed: %s",
				libusb_error_name(ret));
	}
	return SR_OK;
}
/** Initializes USB transfers.
 Allocate and nitialize usb transfer structures and buffers.
 @internal */
SR_PRIV int owon_init_transfers(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct dev_context * devc;
	int ret;

	drvc = sdi->driver->context;
	devc = sdi->priv;
	usb = sdi->conn;
	if (!drvc || !devc || !usb) {
		sr_err("Driver not initializer or device not open.");
		return SR_ERR;
	}
	if ((ret = owon_alloc_usb(devc)) != SR_OK) {
		sr_err("Failed to allocate memory for messaging buffers.");
		return ret;
	}
	libusb_fill_bulk_transfer(devc->xfer_in, usb->devhdl,
		USB_IN_ENDPOINT, devc->in_buf, devc->in_size,
		owon_receive_cb, sdi, USB_TIMEOUT_MS);
	libusb_fill_bulk_transfer(devc->xfer_out, usb->devhdl,
		USB_OUT_ENDPOINT, devc->out_buf, devc->out_size,
		owon_transmit_cb, devc, USB_TIMEOUT_MS);
	return SR_OK;
}
/** Allocate transfer buffers and libusb transfers
 @param devc must not be NULL
 @return SR_OK if success, SR_ERR_DATA if sizes are
 negative and SR_ERR_MALLOC if allocation fails.
 @internal */
SR_PRIV int owon_alloc_usb(struct dev_context * devc)
{
	if (devc->in_size < 0 || devc->out_size < 0)
		return SR_ERR_DATA;
	/* Allocate transfers if needed. */
	if (!devc->xfer_in)
		devc->xfer_in = libusb_alloc_transfer(0);
	if (!devc->xfer_out)
		devc->xfer_out = libusb_alloc_transfer(0);
	if (!devc->xfer_in || !devc->xfer_out){
		/* Clean up. */
		owon_free_usb(devc);
		return SR_ERR_MALLOC;
	}
	/* Allocate buffers in default size if not set */
	if (!devc->in_size){
		devc->in_size = DEFAULT_USB_IN_SIZE;
		devc->in_buf = g_malloc0(devc->in_size);
	} else {
		devc->in_buf = g_realloc(devc->in_buf, devc->in_size);
	}
	if (!devc->out_size){
		devc->out_size = DEFAULT_USB_OUT_SIZE;
		devc->out_buf = g_malloc0(devc->out_size);
	} else {
		devc->out_buf = g_realloc(devc->out_buf, devc->out_size);
	}
	if (!devc->in_buf || !devc->out_buf){
		/* Clean up. */
		owon_free_usb(devc);
		return SR_ERR_MALLOC;
	}
	devc->xfer_in->buffer = devc->in_buf;
	devc->xfer_in->length = devc->in_size;
	devc->xfer_out->buffer = devc->out_buf;
	devc->xfer_out->length = devc->out_size;
	return SR_OK;
}
/** Free transfer buffers and libusb transfers
 and set devc fields to zero or NULL.
 @internal */
SR_PRIV void owon_free_usb(struct dev_context * devc){

	libusb_free_transfer(devc->xfer_in);
	libusb_free_transfer(devc->xfer_out);
	g_free(devc->in_buf);
	g_free(devc->out_buf);
	devc->xfer_in = NULL;
	devc->xfer_out = NULL;
	devc->in_buf = NULL;
	devc->out_buf = NULL;
	devc->in_size = 0;
	devc->out_size = 0;
}
SR_PRIV int owon_vds_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct timeval tv;
	int usb_ret;

	(void)fd;
	sdi = cb_data;
	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* When G_IO_IN would come? */
	}

	usb_ret = libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	return TRUE;
}
