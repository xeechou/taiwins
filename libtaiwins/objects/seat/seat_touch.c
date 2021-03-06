/*
 * seat_pointer.c - taiwins server wl_touch implemetation
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

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <wayland-util.h>

static void
clear_focus_no_signal(struct tw_touch *touch)
{
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
	tw_reset_wl_list(&touch->focused_destroy.link);
}

static void
tw_touch_set_focus(struct tw_touch *touch,
                     struct wl_resource *wl_surface,
                     double sx, double sy)
{
	struct tw_seat_client *client;
	struct tw_seat *seat = wl_container_of(touch, seat, touch);

	client = tw_seat_client_find(seat, wl_resource_get_client(wl_surface));
	if (client && !wl_list_empty(&client->touches)) {
		clear_focus_no_signal(touch);

		touch->focused_client = client;
		touch->focused_surface = wl_surface;

		tw_reset_wl_list(&touch->focused_destroy.link);
		wl_resource_add_destroy_listener(wl_surface,
		                                 &touch->focused_destroy);
		wl_signal_emit(&seat->signals.focus, touch);
	}
}

static void
tw_touch_clear_focus(struct tw_touch *touch)
{
	struct tw_seat *seat = wl_container_of(touch, seat, touch);

	clear_focus_no_signal(touch);
	wl_signal_emit(&seat->signals.unfocus, touch);
}

WL_EXPORT void
tw_touch_default_enter(struct tw_seat_touch_grab *grab,
                       struct wl_resource *surface, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	if (surface)
		tw_touch_set_focus(touch, surface, wl_fixed_from_double(sx),
		                   wl_fixed_from_double(sy));
	else
		tw_touch_clear_focus(touch);
}

WL_EXPORT void
tw_touch_default_down(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                      uint32_t touch_id, double sx, double sy)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;
	uint32_t serial;

        if (touch->focused_client) {
		serial = wl_display_get_serial(grab->seat->display);

		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_down(touch_res, serial, time_msec,
			                   touch->focused_surface,
			                   touch_id,
			                   wl_fixed_from_double(sx),
			                   wl_fixed_from_double(sy));
			wl_touch_send_frame(touch_res);

			grab->seat->last_touch_serial = serial;
		}
	}
}

WL_EXPORT void
tw_touch_default_up(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                    uint32_t touch_id)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;
	uint32_t serial;

	if (touch->focused_client) {
		serial = wl_display_get_serial(grab->seat->display);
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_up(touch_res, serial, time_msec,
			                 touch_id);
			wl_touch_send_frame(touch_res);
		}
	}

}

WL_EXPORT void
tw_touch_default_motion(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                        uint32_t touch_id, double sx, double sy)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;

	if (touch->focused_client) {
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches) {
			wl_touch_send_motion(touch_res, time_msec,
			                     touch_id,
			                     wl_fixed_from_double(sx),
			                     wl_fixed_from_double(sy));
			wl_touch_send_frame(touch_res);
		}
	}

}

WL_EXPORT void
tw_touch_default_touch_cancel(struct tw_seat_touch_grab *grab)
{
	struct wl_resource *touch_res;
	struct tw_touch *touch = &grab->seat->touch;

	if (touch->focused_client)
		wl_resource_for_each(touch_res,
		                     &touch->focused_client->touches)
			wl_touch_send_cancel(touch_res);
}

WL_EXPORT void
tw_touch_default_cancel(struct tw_seat_touch_grab *grab)
{
}

static const struct tw_touch_grab_interface default_grab_impl = {
	.enter = tw_touch_default_enter,
	.down = tw_touch_default_down,
	.up = tw_touch_default_up,
	.motion = tw_touch_default_motion,
	.touch_cancel = tw_touch_default_touch_cancel,
	.cancel = tw_touch_default_cancel,
};

static void
notify_focused_disappear(struct wl_listener *listener, void *data)
{
	struct tw_touch *touch =
		wl_container_of(listener, touch, focused_destroy);

	tw_touch_clear_focus(touch);
}

WL_EXPORT struct tw_touch *
tw_seat_new_touch(struct tw_seat *seat)
{
	struct tw_touch *touch = &seat->touch;
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)
		return touch;
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
	touch->default_grab.data = NULL;
	touch->default_grab.impl = &default_grab_impl;
	touch->default_grab.seat = seat;
	touch->grab = &touch->default_grab;

	wl_list_init(&touch->grabs);
	wl_list_init(&touch->focused_destroy.link);
	touch->focused_destroy.notify = notify_focused_disappear;

	seat->capabilities |= WL_SEAT_CAPABILITY_TOUCH;
	tw_seat_send_capabilities(seat);
	return touch;
}

WL_EXPORT void
tw_seat_remove_touch(struct tw_seat *seat)
{
	struct tw_seat_client *client;
	struct wl_resource *resource, *next;
	struct tw_touch *touch = &seat->touch;

	seat->capabilities &= ~WL_SEAT_CAPABILITY_TOUCH;
	tw_seat_send_capabilities(seat);

	//now we remove the link of the resources, the resource itself would get
	//destroyed in release request.
	wl_list_for_each(client, &seat->clients, link)
		wl_resource_for_each_safe(resource, next, &client->touches)
			tw_reset_wl_list(wl_resource_get_link(resource));

	touch->grab = &touch->default_grab;
	touch->focused_client = NULL;
	touch->focused_surface = NULL;
	tw_reset_wl_list(&touch->focused_destroy.link);
}

WL_EXPORT void
tw_touch_start_grab(struct tw_touch *touch, struct tw_seat_touch_grab *grab,
                    uint32_t priority)
{
	struct tw_seat_touch_grab *old = touch->grab;
	struct tw_seat *seat = wl_container_of(touch, seat, touch);

	if (touch->grab != grab &&
	    !tw_find_list_elem(&touch->grabs, &grab->node.link)) {
		struct wl_list *pos =
			tw_seat_grab_node_find_pos(&touch->grabs, priority);
		grab->seat = seat;
		grab->node.priority = priority;
		//swap grab and notify the old grab its replacement
		if (pos == &touch->grabs) {
			if (old != grab && old->impl->grab_action)
				old->impl->grab_action(old, TW_SEAT_GRAB_PUSH);
			touch->grab = grab;
		}
		wl_list_insert(pos, &grab->node.link);
	}
}

WL_EXPORT void
tw_touch_end_grab(struct tw_touch *touch,
                  struct tw_seat_touch_grab *grab)
{
	struct tw_seat *seat = wl_container_of(touch, seat, touch);
	struct tw_seat_touch_grab *old = touch->grab;

	if (tw_find_list_elem(&touch->grabs, &grab->node.link)) {
		if (grab->impl->cancel)
			grab->impl->cancel(grab);
		tw_reset_wl_list(&grab->node.link);
	}
	//finding previous grab from list or default if stack is empty
	if (!wl_list_empty(&touch->grabs))
		grab = wl_container_of(touch->grabs.next, grab, node.link);
	else
		grab = &touch->default_grab;
	touch->grab = grab;
	touch->grab->seat = seat;
	if (grab != old && grab->impl->grab_action)
		grab->impl->grab_action(grab, TW_SEAT_GRAB_POP);
}
