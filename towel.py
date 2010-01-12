#!/usr/bin/python
# -*- coding: utf-8 -*-
def fmttime(t):
    ret = ''
    min = t / 60
    hour = min / 60
    if hour > 0:
        min -= hour * 60
        ret += ' {0} 個小時'.format(hour)
    if min > 0:
        if hour > 0:
            ret += '又'
        ret += ' {0} 分鐘'.format(min)
    if not min and not hour:
        ret = ' {0} 秒'.format(t)
    return ret

import dbus
def send_notify(t):
    bus = dbus.SessionBus()
    notif = bus.get_object('org.freedesktop.Notifications', '/org/freedesktop/Notifications')
    notify = dbus.Interface(notif, 'org.freedesktop.Notifications')
    notify.Notify("Towel", 0,
                  '/usr/share/icons/Tango/72x72/status/stock_dialog-warning.png',
                  "<span font='12.5' weight='bold'>休息一下</span>",
                  "<span font='14'>您己經盯著螢幕{0}了！</span>".format(fmttime(t)),
                  '', '', 10000)
def send_ok():
    bus = dbus.SessionBus()
    notif = bus.get_object('org.freedesktop.Notifications', '/org/freedesktop/Notifications')
    notify = dbus.Interface(notif, 'org.freedesktop.Notifications')
    notify.Notify("Towel", 0,
                  '/usr/share/icons/Tango/72x72/status/stock_dialog-info.png',
                  "<span font='12.5' weight='bold'>時間到</span>",
                  "<span font='14'>休息夠了吧！上工！</span>",
                  '', '', 0)

def main():
    import time
    import xcb, xcb.xproto, xcb.screensaver

    conn = xcb.connect()
    setup = conn.get_setup()
    root = setup.roots[0].root
    scr = conn(xcb.screensaver.key)

    # time in seconds
    REST_TIME    = 60*5
    CHECK_PERIOD = REST_TIME / 2
    WORKING_TIME = 60*50
    working_time = 0

    while True:
        time.sleep(CHECK_PERIOD)
        cookie = scr.QueryInfo(root)
        info = cookie.reply()
        if info.ms_since_user_input / 1000 < CHECK_PERIOD:
            working_time += CHECK_PERIOD
            if working_time > WORKING_TIME:
                send_notify(working_time)
        elif info.ms_since_user_input / 1000 >= REST_TIME:
            working_time = 0
            send_ok()

if __name__ == '__main__':
    main()
