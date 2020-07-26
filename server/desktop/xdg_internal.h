/*
 * xdg_internal.h - taiwins desktop internal header
 *
 * Copyright (c) 2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef TW_XDG_INTERNAL_H
#define TW_XDG_INTERNAL_H

#include <pixman.h>
#include <stdint.h>
#include <stdbool.h>
#include <backend/backend.h>
#include <wayland-server-core.h>

#include <objects/seat.h>
#include <objects/desktop.h>

#include "xdg.h"
#include "workspace.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_xdg_grab_interface {
	union {
		struct tw_seat_pointer_grab pointer_grab;
		struct tw_seat_touch_grab touch_grab;
		struct tw_seat_keyboard_grab keyboard_grab;
	};
	/* need this struct to access the workspace */
	struct wl_listener view_destroy_listener;
	struct tw_xdg_view *view;
	struct tw_xdg *xdg;
	double sx, sy; /**< surface motion coordinates by events */
};

struct tw_xdg {
	struct wl_display *display;
	struct tw_shell *shell;
	struct tw_backend *backend;

        struct tw_desktop_manager desktop_manager;

	/* managing current status */
	struct tw_workspace *actived_workspace[2];
	struct tw_workspace workspaces[9];

	struct wl_listener desktop_area_listener;
	struct wl_listener display_destroy_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_destroy_listener;
	struct wl_listener surface_transform_listener;

        /**< statics */
	struct tw_xdg_output outputs[32];
	struct tw_xdg_layout floating_layout;
};

bool
tw_xdg_start_moving_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                         struct tw_seat *seat);

bool
tw_xdg_start_resizing_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                           struct tw_seat *seat);

bool
tw_xdg_start_task_switching_grab(struct tw_xdg *xdg, struct tw_xdg_view *view,
                                 struct tw_seat *seat);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */