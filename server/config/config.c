/*
 * config.c - taiwins config functions
 *
 * Copyright (c) 2019 Xichen Zhou
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-names.h>
#include <linux/input.h>
#include <ctype.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <sequential.h>
#include <os/file.h>
#include <strops.h>
#include <libweston/libweston.h>

#include "config_internal.h"
#include "server/compositor.h"
#include "server/desktop/desktop.h"
#include "server/taiwins.h"

/******************************************************************************
 * API
 *****************************************************************************/

static inline void
tw_config_set_bindings(struct tw_config *config, struct tw_bindings *b)
{
	config->bindings = b;
}

static inline struct tw_bindings*
tw_config_get_bindings(struct tw_config *config)
{
	return config->bindings;
}

static inline void
swap_listener(struct wl_list *dst, struct wl_list *src)
{
	wl_list_init(dst);
	dst->next = src->next;
	dst->prev = src->prev;
	src->next->prev = dst;
	src->prev->next = dst;
	wl_list_init(src);
}

static void
tw_config_apply_default(struct tw_config *c)
{
	//compositor setup
	c->compositor->kb_repeat_delay = -1;
	c->compositor->kb_repeat_rate = -1;
	c->xkb_rules = (struct xkb_rule_names){0};
	//apply bindings
	c->builtin_bindings[TW_QUIT_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_F12, 0}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_QUIT",
	};
	c->builtin_bindings[TW_RELOAD_CONFIG_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_R, MODIFIER_CTRL | MODIFIER_ALT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_RELOAD_CONFIG",
	};
	c->builtin_bindings[TW_OPEN_CONSOLE_BINDING] = (struct tw_binding) {
		.keypress = {{KEY_P, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_OPEN_CONSOLE",
	};
	c->builtin_bindings[TW_ZOOM_AXIS_BINDING] = (struct tw_binding) {
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SUPER},
		.type = TW_BINDING_axis,
		.name = "TW_ZOOM_AXIS",
	};
	c->builtin_bindings[TW_ALPHA_AXIS_BINDING] = (struct tw_binding) {
		.axisaction = {.axis_event = WL_POINTER_AXIS_VERTICAL_SCROLL,
			       .modifier = MODIFIER_CTRL | MODIFIER_SHIFT},
		.type = TW_BINDING_axis,
		.name = "TW_ALPHA_AXIS",
	};
	c->builtin_bindings[TW_MOVE_PRESS_BINDING] = (struct tw_binding) {
		.btnpress = {BTN_LEFT, MODIFIER_SUPER},
		.type = TW_BINDING_btn,
		.name = "TW_MOVE_VIEW_BTN",
	};
	c->builtin_bindings[TW_FOCUS_PRESS_BINDING] = (struct tw_binding) {
		.btnpress = {BTN_LEFT, 0},
		.type = TW_BINDING_btn,
		.name = "TW_FOCUS_VIEW_BTN",
	};
	c->builtin_bindings[TW_SWITCH_WS_LEFT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_LEFT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_LEFT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RIGHT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_CTRL}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RIGHT_WORKSPACE",
	};
	c->builtin_bindings[TW_SWITCH_WS_RECENT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_B, MODIFIER_CTRL}, {KEY_B, MODIFIER_CTRL},
			     {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_MOVE_TO_RECENT_WORKSPACE",
	};
	c->builtin_bindings[TW_TOGGLE_FLOATING_BINDING] = (struct tw_binding){
		.keypress = {{KEY_SPACE, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_FLOATING",
	};
	c->builtin_bindings[TW_TOGGLE_VERTICAL_BINDING] = (struct tw_binding){
		.keypress = {{KEY_SPACE, MODIFIER_ALT | MODIFIER_SHIFT},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_TOGGLE_VERTICAL",
	};
	c->builtin_bindings[TW_VSPLIT_WS_BINDING] = (struct tw_binding){
		.keypress = {{KEY_V, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_VERTICAL",
	};
	c->builtin_bindings[TW_HSPLIT_WS_BINDING] = (struct tw_binding){
		.keypress = {{KEY_H, MODIFIER_SUPER}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_SPLIT_HORIZENTAL",
	};
	c->builtin_bindings[TW_MERGE_BINDING] = (struct tw_binding){
		.keypress = {{KEY_M, MODIFIER_SUPER},
			     {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_MERGE",
	};
	c->builtin_bindings[TW_RESIZE_ON_LEFT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_LEFT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_LEFT",
	};
	c->builtin_bindings[TW_RESIZE_ON_RIGHT_BINDING] = (struct tw_binding){
		.keypress = {{KEY_RIGHT, MODIFIER_ALT}, {0}, {0}, {0}, {0}},
		.type = TW_BINDING_key,
		.name = "TW_VIEW_RESIZE_RIGHT",
	};
	c->builtin_bindings[TW_NEXT_VIEW_BINDING] = (struct tw_binding){
		.keypress = {{KEY_J, MODIFIER_ALT | MODIFIER_SHIFT},{0},{0},{0},{0}},
		.type = TW_BINDING_key,
		.name = "TW_NEXT_VIEW",
	};
}

struct tw_config*
tw_config_create(struct weston_compositor *ec, log_func_t log)
{
	struct tw_config *config =
		zalloc(sizeof(struct tw_config));
	config->err_msg = NULL;
	config->compositor = ec;
	config->print = log;
	config->quit = false;
	wl_list_init(&config->lua_components);
	wl_list_init(&config->apply_bindings);
	vector_init_zero(&config->option_hooks,
			 sizeof(struct tw_option), NULL);
	vector_init_zero(&config->registry,
	                 sizeof(struct tw_config_obj), NULL);

	return config;
}

//release the config to be reused, we don't remove the apply bindings
//here. Since it maybe called again.
static inline void
_tw_config_release(struct tw_config *config)
{
	if (config->xkb_rules.layout)
		free((void *)config->xkb_rules.layout);
	if (config->xkb_rules.model)
		free((void *)config->xkb_rules.model);
	if (config->xkb_rules.options)
		free((void *)config->xkb_rules.options);
	if (config->xkb_rules.variant)
		free((void *)config->xkb_rules.variant);
	if (config->err_msg)
		free(config->err_msg);

	vector_destroy(&config->config_bindings);
}

void
tw_config_destroy(struct tw_config *config)
{
	_tw_config_release(config);
	free(config);
}


/**
 * @brief swap all the config from one to another.
 *
 * at this point we know for sure we can apply the config. This works even if
 * our dst is a fresh new config. The release function will take care of freeing
 * things.
 */
static void
tw_swap_config(struct tw_config *dst, struct tw_config *src)
{
	_tw_config_release(dst);
	//clone everthing.
	dst->L = src->L;
	dst->bindings = src->bindings;
	dst->config_bindings = src->config_bindings;
	dst->xkb_rules = src->xkb_rules;
	swap_listener(&dst->apply_bindings, &src->apply_bindings);
	swap_listener(&dst->lua_components, &src->lua_components);

	free(src);
}

static void
tw_config_try_config(struct tw_config *config)
{
	bool safe = true;

	tw_config_init_luastate(config);
	tw_config_apply_default(config);

	if (is_file_exist(config->path)) {
		safe = safe && !luaL_loadfile(config->L, config->path);
		safe = safe && !lua_pcall(config->L, 0, 0, 0);
	}

	struct tw_bindings *bindings = tw_config_get_bindings(config);
	struct tw_apply_bindings_listener *listener;
	if (safe)
		wl_list_for_each(listener, &config->apply_bindings, link)
			safe = safe && listener->apply(bindings, config, listener);
	if (safe) {
		struct tw_binding *binding;
		vector_for_each(binding, &config->config_bindings) {
			switch (binding->type) {
			case TW_BINDING_key:
				tw_bindings_add_key(bindings, binding->keypress,
						    binding->key_func, 0, binding);
				break;
			case TW_BINDING_btn:
				tw_bindings_add_btn(bindings, &binding->btnpress,
						    binding->btn_func, binding);
				break;
			case TW_BINDING_axis:
				tw_bindings_add_axis(bindings, &binding->axisaction,
						     binding->axis_func, binding);
				break;
			default:
				break;
			}
		}
	}
	config->quit = config->quit || !safe;
}

/**
 * @brief apply options we accumulated in the lua run
 */
static void
tw_config_apply_cached(struct tw_config *config)
{
	if (config->xkb_rules.layout || config->xkb_rules.model ||
	    config->xkb_rules.options || config->xkb_rules.rules ||
	    config->xkb_rules.variant) {
		//this one should have runtime effect if weston finally took my patch
		weston_compositor_set_xkb_rule_names(config->compositor, &config->xkb_rules);
		config->xkb_rules = (struct xkb_rule_names){0};
	}
	if (config->kb_delay > 0 && config->kb_repeat) {
		config->compositor->kb_repeat_rate = config->kb_repeat;
		config->compositor->kb_repeat_delay = config->kb_delay;
		config->kb_delay = (config->kb_repeat = -1);
	}
}

/**
 * @brief run/rerun the configurations.
 *
 * right now we can only run once.
 */
bool
tw_config_run(struct tw_config *config, const char *path)
{
	bool error = false;
	if (path) {
		strop_ncpy(config->path, path, 128);
	}
	//create temporary resource
	struct tw_bindings *bindings = tw_bindings_create(config->compositor);
	struct tw_config *temp_config = tw_config_create(config->compositor, config->print);
	struct tw_config_component_listener *component;
	//setup the temporary config
	temp_config->option_hooks = config->option_hooks;
	strcpy(temp_config->path, config->path);
	swap_listener(&temp_config->apply_bindings, &config->apply_bindings);
	swap_listener(&temp_config->lua_components, &config->lua_components);
	tw_config_set_bindings(temp_config, bindings);
	//now we try the commits
	tw_config_try_config(temp_config);
	error = temp_config->quit;
	//apply all the components
	if (!error) {
		struct tw_option *opt = NULL;
		struct tw_option_listener *listener;
		//clean up the bindings we have right now
		tw_bindings_apply(bindings);
		tw_swap_config(config, temp_config);
		tw_config_apply_cached(config);
		//run all the hooks registered
		vector_for_each(opt, &config->option_hooks)
			wl_list_for_each(listener, &opt->listener_list, link)
				listener->apply(config, listener);
	} else {
		if (config->err_msg)
			free(config->err_msg);
		config->err_msg = strdup(lua_tostring(temp_config->L, -1));
		swap_listener(&config->apply_bindings, &temp_config->apply_bindings);
		swap_listener(&config->lua_components, &temp_config->lua_components);
		tw_config_destroy(temp_config);
	}
	wl_list_for_each(component, &config->lua_components, link)
		if (component->apply)
			component->apply(config, error, component);

	return (!error);
}

const char *
tw_config_retrieve_error(struct tw_config *config)
{
	return config->err_msg;
}

const struct tw_binding *
tw_config_get_builtin_binding(struct tw_config *c,
				   enum tw_builtin_binding_t type)
{
	assert(type < TW_BUILTIN_BINDING_SIZE);
	return &c->builtin_bindings[type];
}

void
tw_config_add_apply_bindings(struct tw_config *c,
				  struct tw_apply_bindings_listener *listener)
{
	wl_list_insert(&c->apply_bindings, &listener->link);
}

void
tw_config_add_component(struct tw_config *c,
			     struct tw_config_component_listener *listener)
{
	wl_list_insert(&c->lua_components, &listener->link);
}

void
tw_config_add_option_listener(struct tw_config *config, const char *key,
                              struct tw_option_listener *listener)
{
	struct tw_option *opt;
	bool new_option = true;
	vector_for_each(opt, &config->option_hooks) {
		if (strncmp(opt->key, key, 32) == 0) {
			new_option = false;
			break;
		}
	}
	if (new_option) {
		opt = vector_newelem(&config->option_hooks);
		strop_ncpy(opt->key, key, 32);
		wl_list_init(&opt->listener_list);
	}
	wl_list_init(&listener->link);
	wl_list_insert(&opt->listener_list, &listener->link);
}

void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj)
{
	struct tw_config_obj object;

	strop_ncpy((char *)object.name, name, 32);
	object.data = obj;
	vector_append(&config->registry, &object);
}

void *
tw_config_request_object(struct tw_config *config,
                         const char *name)
{
	struct tw_config_obj *obj;

	vector_for_each(obj, &config->registry) {
		if (!strcmp(name, obj->name))
			return obj->data;
	}
	return NULL;
}

bool
tw_run_default_config(struct tw_config *config)
{
	struct shell *shell;
	struct console *console;
	struct tw_backend *backend;
	struct tw_bus *bus;
	struct tw_theme *theme;
	struct tw_xwayland *xwayland;
	struct desktop *desktop;

	const char *shell_path;
	const char *console_path;
	struct weston_compositor *ec = config->compositor;

	shell_path =  tw_config_request_object(config, "shell_path");
	console_path = tw_config_request_object(config, "console_path");

	weston_compositor_wake(ec);

	if (!(backend = tw_setup_backend(ec)))
		goto out;
        tw_config_register_object(config, "backend", backend);

        if (!(bus = tw_setup_bus(ec)))
		goto out;
        tw_config_register_object(config, "bus", bus);

	if (!(shell = tw_setup_shell(ec, shell_path)))
		goto out;
        tw_config_register_object(config, "shell", shell);

        if (!(console = tw_setup_console(ec, console_path, config)))
		goto out;
	tw_config_register_object(config, "console", console);

	if (!(desktop = tw_setup_desktop(ec, config)))
		goto out;
	tw_config_register_object(config, "desktop", desktop);

	if (!(theme = tw_setup_theme(ec)))
		goto out;
	tw_config_register_object(config, "theme", theme);

	if (!(xwayland = tw_setup_xwayland(ec)))
		goto out;
        tw_config_register_object(config, "xwayland", xwayland);

        ec->default_pointer_grab = NULL;
	ec->kb_repeat_delay = 500;
	ec->kb_repeat_rate = 20;

out:
	return false;
}

static void
tw_config_table_apply(void *data)
{
	struct tw_config_table *t = data;
	tw_config_table_flush(t);
}

void
tw_config_table_dirty(struct tw_config_table *t, bool dirty)
{
	struct wl_display *display;
	struct wl_event_loop *loop;

	if (t->config->_config_time || !dirty)
		return;
	display = t->config->compositor->wl_display;
	loop = wl_display_get_event_loop(display);

	wl_event_loop_add_idle(loop, tw_config_table_apply, t);
}

/* this function is the only point we apply for configurations, It can may run
 * in the middle of the configuration as well. For example, if lua config is
 * calling compositor.wake(). tw_config_table_apply would run and apply for the
 * configuration first before actually wakening the comositor.
*/
void
tw_config_table_flush(struct tw_config_table *t)
{
	struct tw_config *c = t->config;
	struct weston_compositor *ec = t->config->compositor;
	struct desktop *desktop;
	struct shell *shell;
	struct tw_xwayland *xwayland;
	struct tw_theme *theme;

	desktop = tw_config_request_object(c, "desktop");
	shell  = tw_config_request_object(c, "shell");
	xwayland = tw_config_request_object(c, "xwayland");
	theme = tw_config_request_object(c, "theme");

	for (int i = 0; i < 32; i++) {
		struct weston_output *output =
			 t->outputs[i].output;
		enum wl_output_transform transform =
			t->outputs[i].transform.transform;
		int32_t scale =
			t->outputs[i].scale.val;
		//this does not work for output already enabled actually.
		if (!output)
			continue;
		if (t->outputs[i].transform.valid) {
			weston_output_set_transform(output, transform);
			t->outputs[i].transform.valid = false;
		}
		if (t->outputs[i].scale.valid) {
			weston_output_set_scale(output, scale);
			t->outputs[i].scale.valid = false;
		}
		wl_signal_emit(&ec->output_resized_signal, output);
	}
	for (int i = 0; i < tw_desktop_num_workspaces(desktop); i++) {
		/* if (t->workspaces[i].layout.valid) */
		/*	tw_desktop_set_workspace_layout(c->desktop, i, */
		/*	                                t->workspaces[i].layout.layout); */
	}

	if (t->desktop_igap.valid || t->desktop_ogap.valid) {
		tw_desktop_set_gap(desktop,
		                   t->desktop_igap.val,
		                   t->desktop_ogap.val);
		t->desktop_igap.valid = false;
		t->desktop_ogap.valid = false;
	}

	if (t->xwayland.valid) {
		tw_xwayland_enable(xwayland, t->xwayland.enable);
		t->xwayland.valid = false;
	}

	if (t->background_path.valid) {
		tw_shell_set_wallpaper(shell, t->background_path.path);
		free(t->background_path.path);
		t->background_path.valid = false;
		t->background_path.path = NULL;
	}

        if (t->widgets_path.valid) {
		tw_shell_set_widget_path(shell, t->widgets_path.path);
		free(t->widgets_path.path);
		t->widgets_path.valid = false;
		t->widgets_path.path = NULL;
	}

	if (t->menu.valid) {
		tw_shell_set_menu(shell, &t->menu.vec);
		vector_init_zero(&t->menu.vec, 1, NULL);
		t->menu.valid = false;
	}

	if (t->lock_timer.valid) {
		ec->idle_time = t->lock_timer.val;
		t->lock_timer.valid = false;
	}

	weston_compositor_set_xkb_rule_names(ec, &t->xkb_rules);
	ec->kb_repeat_rate = t->kb_repeat;
	ec->kb_repeat_delay = t->kb_delay;

	if (t->theme.valid) {
		tw_theme_notify(theme);
		t->theme.read = false;
		t->theme.valid = false;
	}

	weston_compositor_schedule_repaint(ec);
}
