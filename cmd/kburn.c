// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Eddie Cai <eddie.cai.linux@gmail.com>
 */

#include <command.h>
#include <console.h>
#include <g_dnl.h>
#include <usb.h>

#include "kburn.h"

static int do_kburn(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	int ret, controller_index;
	char *usb_controller;

	if (argc < 1)
		return CMD_RET_USAGE;

	usb_controller = argv[1];
	controller_index = simple_strtoul(usb_controller, NULL, 0);

	ret = usb_gadget_initialize(controller_index);
	if (ret) {
		printf("USB init failed: %d\n", ret);
		return CMD_RET_FAILURE;
	}

	g_dnl_clear_detach();
	ret = g_dnl_register("usb_dnl_kburn");
	if (ret)
		return CMD_RET_FAILURE;

	if (!g_dnl_board_usb_cable_connected()) {
		puts("\rUSB cable not detected, Command exit.\n");
		ret = CMD_RET_FAILURE;
		goto exit;
	}

	while (1) {
		if (g_dnl_detach())
			break;
		if (ctrlc())
			break;
		usb_gadget_handle_interrupts(controller_index);
	}
	ret = CMD_RET_SUCCESS;

exit:
	g_dnl_unregister();
	g_dnl_clear_detach();
	usb_gadget_release(controller_index);

	return ret;
}

U_BOOT_CMD(kburn, 2, 1, do_kburn,
	   "Canaan usb burner protocol",
	   "<USB_controller> e.g. kburn 0\n"
);
