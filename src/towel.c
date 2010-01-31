#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo.h>
#include <cairo-xcb.h>

#define WM_STATE "_NET_WM_STATE"
#define WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"

struct towel_window_t
{
  cairo_surface_t *cs;
  cairo_t *cr;
  xcb_window_t id;
  xcb_connection_t *conn;
  xcb_screen_t *screen;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
};
typedef struct towel_window_t towel_window_t;

static void
towel_window_init_cairo(towel_window_t *win)
{
  xcb_visualtype_t *vt;

  xcb_get_geometry_cookie_t cookie = xcb_get_geometry_unchecked(win->conn, win->id);
  xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(win->conn, cookie, NULL);

  vt = xcb_aux_find_visual_by_id(win->screen, win->screen->root_visual);
  win->cs = cairo_xcb_surface_create(win->conn, win->id, vt, geo->width, geo->height);
  win->cr = cairo_create(win->cs);

  free(geo);
}

static towel_window_t*
towel_create_window(xcb_connection_t *conn)
{
  /* Get screen setup and root window */
  towel_window_t *win = malloc(sizeof(towel_window_t));
  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom;
  xcb_atom_t wm_state;
  xcb_atom_t wm_state_fullscreen;
  xcb_get_geometry_cookie_t geo_cookie;
  xcb_get_geometry_reply_t *geo;

  win->conn = conn;
  win->screen = iter.data;

  geo_cookie = xcb_get_geometry_unchecked(conn, win->screen->root);
  geo = xcb_get_geometry_reply(win->conn, geo_cookie, NULL);


  win->x = 0;
  win->y = 0;
  win->width = geo->width;
  win->height = geo->height;
  free(geo);

  win->id = xcb_generate_id(conn);
  xcb_create_window(conn,
                    XCB_COPY_FROM_PARENT,
                    win->id,
                    win->screen->root,
                    win->x, win->y,
                    win->width, win->height,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    win->screen->root_visual,
                    0, NULL);
  xcb_map_window(conn, win->id);

  atom_cookie = xcb_intern_atom_unchecked(conn, 0, strlen(WM_STATE), WM_STATE);
  atom = xcb_intern_atom_reply(conn, atom_cookie, NULL);
  wm_state = atom->atom;
  free(atom);
  atom_cookie = xcb_intern_atom_unchecked(conn, 0,
                                          strlen(WM_STATE_FULLSCREEN), WM_STATE_FULLSCREEN);
  atom = xcb_intern_atom_reply(conn, atom_cookie, NULL);
  wm_state_fullscreen = atom->atom;
  free(atom);

  xcb_client_message_event_t ev = {
    .response_type = XCB_CLIENT_MESSAGE,
    .format = 32,
    .window = win->id,
    .type = wm_state,
  };

  ev.data.data32[0] = 1;
  ev.data.data32[1] = wm_state_fullscreen;
  ev.data.data32[2] = 0;
  ev.data.data32[3] = 1;

  /* From ICCCM "Changing Window State" */
  xcb_send_event (conn, 0, win->screen->root,
                  XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                  (const char *)&ev);

  xcb_flush(conn);

  towel_window_init_cairo(win);
  return win;
}

static void
towel_window_destroy(towel_window_t *win)
{
  cairo_surface_destroy(win->cs);
  cairo_destroy(win->cr);
  free(win);
}

static void
towel_window_set_background_color(towel_window_t *win)
{
  cairo_t *cr = win->cr;
  cairo_pattern_t* pat = cairo_pattern_create_linear(win->width/2, 0,
                                                     win->width/2, win->height);
  cairo_pattern_add_color_stop_rgb(pat, 0, .1, .1, .1);
  cairo_pattern_add_color_stop_rgb(pat, 1, .2, .2, .2);
  cairo_set_source(cr, pat);
  cairo_rectangle(cr, 0, 0, win->width, win->height);
  cairo_fill(cr);
  cairo_surface_flush(win->cs);
}

static void
towel_window_hide_cursor(towel_window_t *win)
{
  xcb_cursor_t cur = xcb_generate_id (win->conn);
  xcb_pixmap_t pix = xcb_generate_id (win->conn);
  xcb_create_pixmap(win->conn, 1, pix, win->screen->root, 1, 1);
  xcb_create_cursor(win->conn, cur, pix, pix, 0, 0, 0, 0, 0, 0, 0, 0);
  xcb_change_window_attributes(win->conn, win->id,
                               XCB_CW_CURSOR, &(uint32_t){cur});
  xcb_free_pixmap(win->conn, pix);
}

static void
towel_window_render_time(towel_window_t *win, int timer)
{
  cairo_t *cr = win->cr;
  cairo_text_extents_t extents;
  int min = timer / 60;
  int sec = timer % 60;
  char time_str[6];
  snprintf(time_str, sizeof(time_str), "%02d:%02d", min, sec);
  cairo_select_font_face(cr, "Serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 120);
  cairo_text_extents(cr, "00:00", &extents);
  cairo_set_source_rgb(cr, .1, .1, .1);
  cairo_move_to(cr,
                (win->width - extents.width)/2,
                (win->height - extents.height)/2+6);
  cairo_show_text(cr, time_str);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr,
                (win->width - extents.width)/2,
                (win->height - extents.height)/2);
  cairo_show_text(cr, time_str);
  cairo_surface_flush(win->cs);
}

int
main(int argc, char *argv[])
{
  /* TODO: Accept DISPLAY environment or --display arguments */
  xcb_connection_t *conn = xcb_connect(NULL, NULL);
  towel_window_t *win = towel_create_window(conn);
  uint32_t mask = XCB_CW_EVENT_MASK;
  uint32_t values[] = {XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_POINTER_MOTION};
  int xfd = xcb_get_file_descriptor(conn);
  struct pollfd ufd = { .fd = xfd, .events = POLLIN, };
  int retval;
  int timer = 5 * 60;

  towel_window_hide_cursor(win);
  xcb_change_window_attributes(conn, win->id, mask, values);
  xcb_flush(conn);

  xcb_generic_event_t *event;
  int done = 0;
  while (!done) {
    retval = poll (&ufd, 1, 1000);
    if (retval == -1)
      perror("select()");
    else if (retval) {
      event = xcb_poll_for_event(conn);
      if (event) switch (event->response_type & ~0x80) {
      case XCB_EXPOSE:
        towel_window_set_background_color(win);
        towel_window_render_time(win, timer);
        xcb_flush(conn);
        break;
      case XCB_MOTION_NOTIFY:
        done = 1;
        break;
      default:
        break;
      }
      free(event);
    }
    else {
      /* TODO: Only redraw clipped region */
      timer--;
      towel_window_set_background_color(win);
      towel_window_render_time(win, timer);
      xcb_flush(conn);
    }
  }

  towel_window_destroy(win);
  xcb_disconnect(conn);
  return EXIT_SUCCESS;
}
