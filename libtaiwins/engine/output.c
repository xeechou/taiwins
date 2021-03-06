/*
 * output.c - taiwins engine output implementation
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

#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/output.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/presentation_feedback.h>

#include <taiwins/engine.h>
#include <taiwins/output_device.h>
#include <taiwins/render_context.h>
#include <taiwins/render_output.h>
#include <taiwins/render_surface.h>
#include <wayland-util.h>

#include "output_device.h"
#include "internal.h"

static inline void
emit_output_signal(struct tw_engine_output *o, struct wl_signal *signal)
{
	if (tw_find_list_elem(&o->engine->heads, &o->link))
		wl_signal_emit(signal, o);
}

static void
init_engine_output_state(struct tw_engine_output *o)
{
	pixman_rectangle32_t rect = tw_output_device_geometry(o->device);

	wl_list_init(&o->constrain.link);
	pixman_region32_init_rect(&o->constrain.region,
	                          rect.x, rect.y, rect.width, rect.height);

	wl_list_insert(o->engine->global_cursor.constrains.prev,
	               &o->constrain.link);
}

static void
fini_engine_output_state(struct tw_engine_output *o)
{
	tw_reset_wl_list(&o->constrain.link);
	pixman_region32_fini(&o->constrain.region);

}

static struct wl_resource *
engine_output_get_wl_output(struct tw_engine_output *output,
                            struct wl_resource *resource)
{
	struct wl_resource *wl_output;
	wl_resource_for_each(wl_output, &output->tw_output->resources)
		if (wl_resource_get_client(wl_output) ==
		    wl_resource_get_client(resource))
			return wl_output;
	return NULL;
}

static void
engine_output_send_xdg_info(struct tw_engine_output *output,
                            struct wl_resource *xdg_output)
{
	pixman_rectangle32_t rect =
		tw_output_device_geometry(output->device);
	struct tw_event_xdg_output_info event = {
		.wl_output = tw_xdg_output_get_wl_output(xdg_output),
		.name = output->tw_output->name,
		.x = rect.x,
		.y = rect.y,
		.width = rect.width,
		.height = rect.height
	};
	tw_xdg_output_send_info(xdg_output, &event);
}

static void
engine_output_send_info(struct tw_engine_output *output)
{
	struct wl_resource *xdg_output, *wl_output;
	struct tw_engine *engine = output->engine;

	tw_output_set_name(output->tw_output, output->device->name);
	tw_output_set_scale(output->tw_output, output->device->current.scale);
	tw_output_set_coord(output->tw_output, output->device->current.gx,
	                    output->device->current.gy);
	tw_output_set_mode(output->tw_output, WL_OUTPUT_MODE_CURRENT |
	                   ((output->device->current.current_mode.preferred) ?
	                    WL_OUTPUT_MODE_PREFERRED : 0),
	                   output->device->current.current_mode.w,
	                   output->device->current.current_mode.h,
	                   output->device->current.current_mode.refresh);
	tw_output_set_geometry(output->tw_output,
	                       output->device->phys_width,
	                       output->device->phys_height,
	                       output->device->make,
	                       output->device->model,
	                       output->device->subpixel,
	                       output->device->current.transform);
	tw_output_send_clients(output->tw_output);

	wl_resource_for_each(xdg_output, &engine->output_manager.outputs) {
		wl_output = tw_xdg_output_get_wl_output(xdg_output);
		if (output->tw_output == tw_output_from_resource(wl_output))
			engine_output_send_xdg_info(output, xdg_output);
	}
}

/******************************************************************************
 * listeners
 *****************************************************************************/

static void
notify_output_destroy(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.destroy);
	struct tw_engine *engine = output->engine;
	uint32_t unset = ~(1 << output->id);

	//emit signal only on primary output
	emit_output_signal(output, &engine->signals.output_remove);

	output->id = -1;
	wl_list_remove(&output->link);
	wl_list_remove(&output->listeners.destroy.link);
	wl_list_remove(&output->listeners.present.link);
	wl_list_remove(&output->listeners.set_mode.link);

	tw_output_destroy(output->tw_output);

	fini_engine_output_state(output);
	engine->output_pool &= unset;
}

static void
notify_output_new_mode(struct wl_listener *listener, void *data)
{
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.set_mode);
	struct tw_output_device *device = data;
	struct tw_engine *engine = output->engine;
	pixman_rectangle32_t rect = tw_output_device_geometry(output->device);

	assert(device == output->device);
	pixman_region32_fini(&output->constrain.region);
	pixman_region32_init_rect(&output->constrain.region,
	                          rect.x, rect.y, rect.width, rect.width);
	//move output from primary to secondary and vice verse
	//accordingly. secondary output does not emit any signals
	if (tw_find_list_elem(&engine->pending_heads, &output->link) &&
	    device->current.primary) {
		wl_list_remove(&output->link);
		wl_list_insert(engine->heads.prev, &output->link);
		emit_output_signal(output, &engine->signals.output_created);

	} else if (tw_find_list_elem(&engine->heads, &output->link) &&
	           !device->current.primary) {
		emit_output_signal(output, &engine->signals.output_remove);
		wl_list_remove(&output->link);
		wl_list_insert(engine->pending_heads.prev, &output->link);

	} else if (tw_find_list_elem(&engine->heads, &output->link)) {
		wl_signal_emit(&engine->signals.output_resized, output);
	}
	engine_output_send_info(output);
}

static void
notify_output_present(struct wl_listener *listener, void *data)
{
	struct tw_presentation_feedback *feedback, *tmp;
	struct tw_engine_output *output =
		wl_container_of(listener, output, listeners.present);
	struct tw_engine *engine = output->engine;
	struct tw_event_output_present *event = data;

	wl_list_for_each_safe(feedback, tmp, &engine->presentation.feedbacks,
	                      link) {
		struct wl_resource *wl_surface =
			feedback->surface->resource;
		struct wl_resource *wl_output =
			engine_output_get_wl_output(output, wl_surface);

		if (event)
			tw_presentation_feeback_sync(feedback, wl_output,
			                             &event->time,
			                             event->seq,
			                             event->refresh,
			                             event->flags);
		else
			tw_presentation_feedback_discard(feedback);
	}
}

/******************************************************************************
 * APIs
 *****************************************************************************/

void
tw_engine_new_xdg_output(struct tw_engine *engine,
                         struct wl_resource *resource)
{
	struct wl_resource *wl_output =
		tw_xdg_output_get_wl_output(resource);
	struct tw_engine_output *output =
		tw_engine_output_from_resource(engine, wl_output);
	engine_output_send_xdg_info(output, resource);
}

bool
tw_engine_new_output(struct tw_engine *engine,
                     struct tw_output_device *device)
{
	struct tw_engine_output *output;
	struct tw_render_output *render_output =
		wl_container_of(device, render_output, device);
	uint32_t id = ffs(~engine->output_pool)-1;

	if (ffs(~engine->output_pool) <= 0)
		tw_logl_level(TW_LOG_ERRO, "too many displays");
	output = &engine->outputs[id];
	output->id = id;
	output->engine = engine;
	output->device = device;
	output->tw_output = tw_output_create(engine->display);
	tw_output_device_set_id(device, id);
	wl_list_init(&output->link);

	if (!output->tw_output) {
		tw_logl_level(TW_LOG_ERRO, "failed to create wl_output");
		return false;
	}

	init_engine_output_state(output);

	tw_signal_setup_listener(&device->signals.destroy,
	                         &output->listeners.destroy,
	                         notify_output_destroy);
	tw_signal_setup_listener(&device->signals.commit_state,
	                         &output->listeners.set_mode,
	                         notify_output_new_mode);
	tw_signal_setup_listener(&render_output->signals.present,
	                         &output->listeners.present,
	                         notify_output_present);
        engine->output_pool |= 1 << id;

        wl_list_insert(engine->pending_heads.prev, &output->link);
        return true;
}

WL_EXPORT struct tw_engine_output *
tw_engine_get_focused_output(struct tw_engine *engine)
{
	struct tw_seat *seat;
	struct wl_resource *wl_surface = NULL;
	struct tw_render_surface *surface = NULL;
	struct tw_engine_seat *engine_seat;

	if (wl_list_length(&engine->heads) == 0)
		return NULL;

	wl_list_for_each(engine_seat, &engine->inputs, link) {
		struct tw_pointer *pointer;
		struct tw_keyboard *keyboard;
		struct tw_touch *touch;

		seat = engine_seat->tw_seat;
		if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) {
			pointer = &seat->pointer;
			wl_surface = pointer->focused_surface;
			surface =  tw_render_surface_from_resource(wl_surface);
			if (surface)
				return &engine->outputs[surface->output];
		}
		else if (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
			keyboard = &seat->keyboard;
			wl_surface = keyboard->focused_surface;
			surface = tw_render_surface_from_resource(wl_surface);
			if (surface)
				return &engine->outputs[surface->output];
		} else if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) {
			touch = &seat->touch;
			wl_surface = touch->focused_surface;
			surface = tw_render_surface_from_resource(wl_surface);
			if (surface)
				return &engine->outputs[surface->output];
		}
	}

	struct tw_engine_output *head;
	wl_list_for_each(head, &engine->heads, link)
		return head;

	return NULL;
}

WL_EXPORT struct tw_engine_output *
tw_engine_output_from_resource(struct tw_engine *engine,
                               struct wl_resource *resource)
{
	struct tw_output *tw_output =
		tw_output_from_resource(resource);
	struct tw_engine_output *output;

	wl_list_for_each(output, &engine->heads, link) {
		if (output->tw_output == tw_output)
			return output;
	}
	wl_list_for_each(output, &engine->pending_heads, link) {
		if (output->tw_output == tw_output)
			return output;
	}
	return NULL;
}

WL_EXPORT struct tw_engine_output *
tw_engine_output_from_device(struct tw_engine *engine,
                             const struct tw_output_device *device)
{
	struct tw_engine_output *output = NULL;
	wl_list_for_each(output, &engine->heads, link)
		if (output->device == device)
			return output;
	wl_list_for_each(output, &engine->pending_heads, link)
		if (output->device == device)
			return output;
	//this may not be a good idea?
	return tw_engine_get_focused_output(engine);
}

WL_EXPORT void
tw_engine_output_notify_surface_enter(struct tw_engine_output *output,
                                      struct tw_surface *surface)
{
	struct wl_resource *wl_output;

	wl_resource_for_each(wl_output, &output->tw_output->resources) {
		if (tw_match_wl_resource_client(wl_output, surface->resource))
			wl_surface_send_enter(surface->resource, wl_output);
	}
}

WL_EXPORT void
tw_engine_output_notify_surface_leave(struct tw_engine_output *output,
                                      struct tw_surface *surface)
{
	struct wl_resource *wl_output;

	wl_resource_for_each(wl_output, &output->tw_output->resources) {
		if (tw_match_wl_resource_client(wl_output, surface->resource))
			wl_surface_send_leave(surface->resource, wl_output);
	}
}
