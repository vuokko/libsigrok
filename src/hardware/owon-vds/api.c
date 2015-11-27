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
#include "protocol.h"

SR_PRIV struct sr_dev_driver owon_vds_driver_info;
/* Device will be opened and closed in scan */
static int dev_open(struct sr_dev_inst *sdi);
static int dev_close(struct sr_dev_inst *sdi);

static const uint32_t drvopts[] = {
};
static const struct owon_vds_model models[] = {
	{VDS2062, "Owon", "VDS2062", "VDS2062"},
	{0, NULL, NULL, NULL,},
};
static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *usb_devices, *node, *devices;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct libusb_device_descriptor des;
	libusb_device * usb_dev;
	int ret, i;
	char model[64];
	const char * conn = USB_VID_PID;


	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	for (node = options; node != NULL; node = node->next) {
		src = node->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	for (node = usb_devices; node != NULL; node = node->next) {
		usb = node->data;
		/* Create sigrok device instance. */
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		devc = g_malloc0(sizeof(struct dev_context));
		if (!sdi) {
			sr_usb_dev_inst_free(usb);
			continue;
		}
		if (!devc) {
			g_free(sdi);
			sr_usb_dev_inst_free(usb);
			continue;
		}
		sdi->status = SR_ST_INACTIVE;
		sdi->driver = di;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;
		sdi->priv = devc;

		/* Get some values from descriptors. */
		if (dev_open(sdi) == SR_OK) {
			usb_dev = libusb_get_device(usb->devhdl);
			if (libusb_get_device_descriptor(usb_dev, &des) == 0) {
				model[0] = '\0';
				if (des.iSerialNumber && (ret = libusb_get_string_descriptor_ascii(
					usb->devhdl, des.iSerialNumber, (unsigned char *) model,
					sizeof(model))) < 0) {
					sr_warn("Failed to get iSerialNumber string descriptor: %s.",
						libusb_error_name(ret));
				}
				sr_spew("Scanned: %s.", model);
				sdi->model = NULL;
				for (i = 0; models[i].id != NULL; i++) {
					if (!strcmp(models[i].id, model)){
						sr_spew("Getting more info of %s", model);
						sdi->model = g_strdup(models[i].name);
						sdi->vendor = g_strdup(models[i].vendor);
						devc->model = &models[i];
						devc->next_target = GET_VERSION;
						owon_send_cmd(devc);
						handle_libusb_events(drvc, 3);
						break;
					}
				}
				if (!sdi->model) {
					sr_err("Unsupported model %s", model);
					dev_close(sdi);
					continue;
				}
			}
			dev_close(sdi);
		} else {
			continue;
		}
		/* Register device instance with driver. */
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}
	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}
static void free_devc(void * priv)
{
	struct dev_context * devc;
	devc = priv;
	g_free(devc->dev_info.board_version);
	g_free(devc->dev_info.serial_number);
}
static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, free_devc);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret, claim_ret;

	drvc = sdi->driver->context;

	if (!drvc) {
		sr_err("Driver was not initialized.");
			return SR_ERR;
		}
	if (sdi->status != SR_ST_INACTIVE) {
		sr_err("Device already open.");
		return SR_ERR;
	}
	usb = sdi->conn;

	if ((ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb)) != SR_OK)
		return ret;
	/* Considering how crappy the USB interface is, let's try to get it
	   to our use. Some bad driver might claim it before us. */
	if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
		if((claim_ret = libusb_detach_kernel_driver(usb->devhdl,
			USB_INTERFACE)) != 0){
			sr_err("Failed to detach kernel driver: %s.",
			libusb_error_name(ret));
		sr_usb_close(usb);
		return SR_ERR;
		}
	}
	/* Do a light reset. Or don't. Why this breaks thigs? */
	/*claim_ret = libusb_set_configuration(usb->devhdl, USB_CONFIG);
	if (claim_ret == 0)
		claim_ret = libusb_set_configuration(usb->devhdl, USB_CONFIG);
	if (claim_ret != 0) {
		sr_err("Failed to set USB configuration: %s.",
			libusb_error_name(ret));
		sr_usb_close(usb);
		return SR_ERR;
	}*/
	/* Claim the interface for our use. */
	if ((claim_ret = libusb_claim_interface(usb->devhdl,
		USB_INTERFACE)) != 0) {
		sr_err("Failed to claim interface: %s",
			libusb_error_name(claim_ret));
		return SR_ERR;
	}
	/* Initialize transfer structures. */
	ret = owon_init_transfers(sdi);
	if (ret == SR_OK)
		sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_usb_dev_inst *usb;
	struct dev_context * devc;

	/* TODO: get handle from sdi->conn and close it. */
	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}
	usb = sdi->conn;
	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_attach_kernel_driver(usb->devhdl, USB_INTERFACE);
	sr_usb_close(usb);
	sdi->status = SR_ST_INACTIVE;
	devc = sdi->priv;
	owon_free_usb(devc);
	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	dev_clear(di);

	/* TODO: free other driver resources, if any. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)data;
	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	(void)sdi;
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc->next_target = STOP;
	/* STOP will be sent when next data reception is ready. */
	return SR_OK;
}

SR_PRIV struct sr_dev_driver owon_vds_driver_info = {
	.name = "owon-vds",
	.longname = "Owon-VDS",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
