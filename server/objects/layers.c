/*
 * layers.c - taiwins server layers manager
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

#include <wayland-util.h>
#include "layers.h"

static struct tw_layers_manager s_layers_manager = {0};


struct tw_layers_manager *
tw_layers_manager_create_global(struct wl_display *display)
{
	return &s_layers_manager;
}


void
tw_layer_set_position(struct tw_layer *layer, enum tw_layer_pos pos,
                      struct wl_list *layers)
{
	struct tw_layer *l, *tmp;

	wl_list_remove(&layer->link);
	layer->position = pos;

	//from bottom to top
	wl_list_for_each_reverse_safe(l, tmp, layers, link) {
		if (l->position >= pos) {
			wl_list_insert(&l->link, &layer->link);
			return;
		}
	}
	wl_list_insert(layers, &layer->link);
}

void
tw_layer_unset_position(struct tw_layer *layer)
{
	wl_list_remove(&layer->link);
	wl_list_init(&layer->link);
}
