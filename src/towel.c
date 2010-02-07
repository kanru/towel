#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/screensaver.h>

#include <cairo.h>
#include <cairo-xcb.h>

#define DEBUG 0

#define WM_STATE "_NET_WM_STATE"
#define WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"

/* time in seconds */
#if DEBUG
#define REST_TIME (60)
#define CHECK_PERIOD (REST_TIME / 2)
#define WORKING_TIME (60)
#else
#define REST_TIME (60*5)
#define CHECK_PERIOD (REST_TIME / 2)
#define WORKING_TIME (60*50)
#endif

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
  uint16_t working_time;
  uint16_t idle_time;
};
typedef struct towel_window_t towel_window_t;

static int
ms2sec(int ms)
{
  return ms/1000;
}

static void
towel_window_init_cairo(towel_window_t *win)
{
  xcb_visualtype_t *vt;

  xcb_get_geometry_cookie_t cookie = xcb_get_geometry_unchecked(win->conn,
                                                                win->id);
  xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(win->conn, cookie, NULL);

  vt = xcb_aux_find_visual_by_id(win->screen, win->screen->root_visual);
  win->cs = cairo_xcb_surface_create(win->conn, win->id,
                                     vt, geo->width, geo->height);
  win->cr = cairo_create(win->cs);

  free(geo);
}

static towel_window_t*
towel_create_window(xcb_connection_t *conn)
{
  /* Get screen setup and root window */
  towel_window_t *win = calloc(1, sizeof(towel_window_t));
  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_get_geometry_cookie_t geo_cookie;
  xcb_get_geometry_reply_t *geo;
  uint32_t mask = XCB_CW_EVENT_MASK;
  uint32_t values[] = {XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_POINTER_MOTION};

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
                    mask, values);

  xcb_flush(conn);

  towel_window_init_cairo(win);
  return win;
}

static xcb_atom_t
towel_window_get_atom(towel_window_t *win, const char *atom)
{
  xcb_intern_atom_cookie_t cookie;
  xcb_intern_atom_reply_t *reply;
  xcb_atom_t ret;
  cookie = xcb_intern_atom_unchecked(win->conn, 0, strlen(atom), atom);
  reply = xcb_intern_atom_reply(win->conn, cookie, NULL);
  ret = reply->atom;
  free(reply);
  return ret;
}

static void
towel_window_map(towel_window_t *win)
{
  xcb_atom_t wm_state = towel_window_get_atom(win, WM_STATE);
  xcb_atom_t wm_state_fullscreen = towel_window_get_atom(win, WM_STATE_FULLSCREEN);

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

  xcb_map_window(win->conn, win->id);

  /* From ICCCM "Changing Window State" */
  xcb_send_event(win->conn, 0, win->screen->root,
                 XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                 XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                 (const char *)&ev);
}

static void
towel_window_unmap(towel_window_t *win)
{
  xcb_unmap_window(win->conn, win->id);
}

static void
towel_window_destroy(towel_window_t *win)
{
  cairo_surface_destroy(win->cs);
  cairo_destroy(win->cr);
  xcb_destroy_window(win->conn, win->id);
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
  int x, y, w, h;
  snprintf(time_str, sizeof(time_str), "%02d:%02d", min, sec);
  cairo_select_font_face(cr, "Serif",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 120);
  cairo_text_extents(cr, "00:00", &extents);
  x = (win->width - extents.width)/2;
  y = (win->height - extents.height)/2+6;
  w = extents.width;
  h = extents.height;
  cairo_set_source_rgb(cr, .1, .1, .1);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, time_str);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, time_str);
  cairo_surface_flush(win->cs);
}

static void
towel_window_update_working_time(towel_window_t *win, int period)
{
  xcb_screensaver_query_info_cookie_t cookie;
  xcb_screensaver_query_info_reply_t *reply;
  cookie = xcb_screensaver_query_info_unchecked(win->conn, win->screen->root);
  reply = xcb_screensaver_query_info_reply(win->conn, cookie, NULL);
#if DEBUG
  fprintf(stderr, "idle: %d sec\n", ms2sec(reply->ms_since_user_input));
#endif
  win->idle_time = ms2sec(reply->ms_since_user_input);
  if (win->idle_time < period)
    win->working_time += period;
}

static void
towel_window_grab_input(towel_window_t *win)
{
  xcb_grab_pointer_cookie_t pointer_cookie;
  xcb_grab_keyboard_cookie_t keyboard_cookie;
  xcb_grab_pointer_reply_t *pointer_reply;
  xcb_grab_keyboard_reply_t *keyboard_reply;

  pointer_cookie = xcb_grab_pointer_unchecked(win->conn, 0, win->id,
                                              XCB_EVENT_MASK_NO_EVENT,
                                              XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                                              win->id,
                                              XCB_CURSOR_NONE, XCB_TIME_CURRENT_TIME);
  keyboard_cookie = xcb_grab_keyboard_unchecked(win->conn, 0, win->id,
                                                XCB_TIME_CURRENT_TIME,
                                                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  pointer_reply = xcb_grab_pointer_reply(win->conn, pointer_cookie, NULL);
  keyboard_reply = xcb_grab_keyboard_reply(win->conn, keyboard_cookie, NULL);
#if DEBUG
  fprintf(stderr, "p: %d, k: %d\n", pointer_reply->status, keyboard_reply->status);
#endif
  free(pointer_reply);
  free(keyboard_reply);
}

int
main(int argc, char *argv[])
{
  /* TODO: Accept DISPLAY environment or --display arguments */
  xcb_connection_t *conn = xcb_connect(NULL, NULL);
  towel_window_t *win = NULL;

  for (;;) {
    if (win == NULL) {
      win = towel_create_window(conn);
      towel_window_hide_cursor(win);
    }
    sleep(CHECK_PERIOD);
    towel_window_update_working_time(win, CHECK_PERIOD);

    /* TODO: option processing */
    if (win->working_time > WORKING_TIME) {
      time_t prev = time(NULL);
      towel_window_map(win);
      xcb_flush(conn);
      for (;;) {
        xcb_generic_event_t *event = xcb_wait_for_event(conn);
        if ((event->response_type & ~0x80) == XCB_EXPOSE) {
          towel_window_grab_input(win);
          towel_window_set_background_color(win);
          towel_window_render_time(win, REST_TIME);
          xcb_flush(conn);
          break;
        }
        free(event);
      }
      for (;;) {
        time_t now = time(NULL);
        towel_window_set_background_color(win);
        towel_window_render_time(win, REST_TIME - (now - prev));
        xcb_flush(conn);
        if (now - prev >= REST_TIME) {
          towel_window_unmap(win);
          towel_window_destroy(win);
          win = NULL;
          xcb_flush(conn);
          break;
        }
        sleep(1);
      }
    }
  }

  if (win)
    towel_window_destroy(win);
  xcb_disconnect(conn);
  return EXIT_SUCCESS;
}
