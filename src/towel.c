#include <xcb/xcb.h>

int main(int argc, char *argv[])
{
  /* TODO: Accept DISPLAY environment or --display arguments */
  xcb_connection_t *conn = xcb_connect(NULL, NULL);
  xcb_disconnect(conn);
  return 0;
}
