#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cairo/cairo.h>

#include <wayland-client.h>
#include <wayland-taiwins-shell-client-protocol.h>
#include "client.h"
#include "ui.h"
#include "nk_wl_egl.h"


/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE TAIWINS_LAUNCHER_CONF_STRIDE
#define NUM_DECISIONS TAIWINS_LAUNCHER_CONF_NUM_DECISIONS

//every decision represents a row in wl_buffer, we need to make it as aligned as possible
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));


struct desktop_launcher {
	struct taiwins_launcher *interface;
	struct wl_globals globals;
	struct app_surface surface;
	struct wl_buffer *decision_buffer;

	off_t cursor;
	char chars[256];
	bool quit;
	//for nuklear
	struct nk_egl_backend *bkend;
	struct egl_env env;
	struct nk_text_edit text_edit;
	const char *previous_tab;
};


static const char *tmp_tab_chars[5] = {
	"aaaaaa",
	"bbbbbb",
	"cccccc",
	"dddddd",
	"eeeeee",
};


/**
 * @brief get the next
 */
static const char *
auto_complete(struct desktop_launcher *launcher)
{
	//we have some shadowed context here
	static int i = 0;
	return tmp_tab_chars[i++ % 5];
}


static void
draw_launcher(struct nk_context *ctx, float width, float height, void *data)
{
	static bool _completing = false;
	struct desktop_launcher *launcher = data;
	nk_layout_row_static(ctx, height, width, 1);
	nk_edit_buffer(ctx, NK_EDIT_FIELD, &launcher->text_edit, nk_filter_default);
	if (nk_egl_get_keyinput(ctx) == XKB_KEY_Tab) {

		_completing = true;
		for (int i = 0; i < strlen(launcher->previous_tab); i++)
			nk_textedit_undo(&launcher->text_edit);
		launcher->previous_tab = auto_complete(launcher);
		nk_textedit_text(&launcher->text_edit,
				 launcher->previous_tab, strlen(launcher->previous_tab));

	} else if (nk_egl_get_keyinput(ctx) == XKB_KEY_Return) {
		launcher->previous_tab = NULL;
		_completing = false;
		//update the buffer.
		//taiwins_launcher_submit(launcher->interface);
		//do fork-exec (you probably don't want to do it here)
	} else {
		launcher->previous_tab = NULL;
		_completing = false;
	}
}

//fuck, I wish that I have c++
static void
update_app_config(void *data,
		  struct taiwins_launcher *taiwins_launcher,
		  const char *app_name,
		  uint32_t floating,
		  wl_fixed_t scale)
{
//we don't nothing here now
}

static void
start_launcher(void *data,
	       struct taiwins_launcher *taiwins_launcher,
	       wl_fixed_t width,
	       wl_fixed_t height,
	       wl_fixed_t scale)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;
	//yeah, generally you will want a buffer from this
	taiwins_launcher_set_launcher(launcher, launcher->surface.wl_surface);
	nk_egl_launch(launcher->bkend,
		      wl_fixed_to_int(width),
		      wl_fixed_to_int(height),
		      wl_fixed_to_double(scale), draw_launcher, launcher);
}


struct taiwins_launcher_listener launcher_impl = {
	.application_configure = update_app_config,
	.start = start_launcher,
};




static void
ready_launcher(struct desktop_launcher *launcher)
{
	struct wl_shm *shm = launcher->globals.shm;
	memset(launcher->chars, 0, sizeof(launcher->chars));
	struct bbox bounding = {
		.x = 0, .y = 0,
		.w = 400, .h = 20
	};
	appsurface_init(&launcher->surface, NULL, APP_WIDGET,
			launcher->globals.compositor, NULL);
	egl_env_init(&launcher->env, launcher->globals.display);
	launcher->bkend = nk_egl_create_backend(&launcher->env,
						launcher->surface.wl_surface);
}


static void
release_launcher(struct desktop_launcher *launcher)
{
	egl_env_end(&launcher->env);
	taiwins_launcher_destroy(launcher->interface);
	wl_globals_release(&launcher->globals);
	launcher->quit = true;
#ifdef __DEBUG
	cairo_debug_reset_static_data();
#endif
}


static
void announce_globals(void *data,
		      struct wl_registry *wl_registry,
		      uint32_t name,
		      const char *interface,
		      uint32_t version)
{
	struct desktop_launcher *launcher = (struct desktop_launcher *)data;

	if (strcmp(interface, taiwins_launcher_interface.name) == 0) {
		fprintf(stderr, "launcher registé\n");
		launcher->interface = (struct taiwins_launcher *)
			wl_registry_bind(wl_registry, name, &taiwins_launcher_interface, version);
		ready_launcher(launcher);
		taiwins_launcher_add_listener(launcher->interface, &launcher_impl, launcher);
	} else
		wl_globals_announce(&launcher->globals, wl_registry, name, interface, version);
}


static void
announce_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name)
{
}

static struct wl_registry_listener registry_listener = {
	.global = announce_globals,
	.global_remove = announce_global_remove
};




int
main(int argc, char *argv[])
{
	struct desktop_launcher tw_launcher;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to display\n");
		return -1;
	}
	wl_globals_init(&tw_launcher.globals, display);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &tw_launcher);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	//okay, now we should create the buffers
	//event loop
	while(wl_display_dispatch(display) != -1 && !tw_launcher.quit);
	release_launcher(&tw_launcher);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return 0;
}
