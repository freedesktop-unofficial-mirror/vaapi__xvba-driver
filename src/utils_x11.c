/*
 *  utils_x11.c - X11 utilities
 *
 *  xvba-video (C) 2009-2011 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "sysdeps.h"
#include <X11/Xutil.h>
#include "utils_x11.h"
#include "utils.h"

// X error trap
static int x11_error_code = 0;
static int (*old_error_handler)(Display *, XErrorEvent *);

static int error_handler(Display *dpy, XErrorEvent *error)
{
    x11_error_code = error->error_code;
    return 0;
}

void x11_trap_errors(void)
{
    x11_error_code    = 0;
    old_error_handler = XSetErrorHandler(error_handler);
}

int x11_untrap_errors(void)
{
    XSetErrorHandler(old_error_handler);
    return x11_error_code;
}

// X window management
static const int x11_event_mask = (KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask |
                                   PointerMotionMask |
                                   EnterWindowMask |
                                   ExposureMask |
                                   StructureNotifyMask);

/**
 * x11_create_window:
 * @dpy: an X11 #Display
 * @w: the requested width, in pixels
 * @h: the requested height, in pixels
 * @vis: the request visual
 * @cmap: the request colormap
 *
 * Creates a border-less window with the specified dimensions. If @vis
 * is %NULL, the default visual for @display will be used. If @cmap is
 * %None, no specific colormap will be bound to the window. Also note
 * the default background color is black.
 *
 * Return value: the newly created X #Window.
 */

Window
x11_create_window(
    Display     *dpy,
    unsigned int w,
    unsigned int h,
    Visual      *vis,
    Colormap     cmap
)
{
    Window rootwin, win;
    int screen, depth;
    XSetWindowAttributes xswa;
    unsigned long xswa_mask;
    XWindowAttributes wattr;
    unsigned long black_pixel, white_pixel;

    screen      = DefaultScreen(dpy);
    rootwin     = RootWindow(dpy, screen);
    black_pixel = BlackPixel(dpy, screen);
    white_pixel = WhitePixel(dpy, screen);

    if (!vis)
        vis = DefaultVisual(dpy, screen);

    XGetWindowAttributes(dpy, rootwin, &wattr);
    depth = wattr.depth;
    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
        depth = 24;

    xswa_mask             = CWBorderPixel | CWBackPixel;
    xswa.border_pixel     = black_pixel;
    xswa.background_pixel = black_pixel;

    if (cmap) {
        xswa_mask        |= CWColormap;
        xswa.colormap     = cmap;
    }

    win = XCreateWindow(
        dpy,
        rootwin,
        0, 0, w, h,
        0,
        depth,
        InputOutput,
        vis,
        xswa_mask, &xswa
    );
    if (!win)
        return None;

    XSelectInput(dpy, win, x11_event_mask);
    return win;
}

int
x11_get_geometry(
    Display      *dpy,
    Drawable      drawable,
    int          *px,
    int          *py,
    unsigned int *pwidth,
    unsigned int *pheight
)
{
    Window rootwin;
    int x, y;
    unsigned int width, height, border_width, depth;

    x11_trap_errors();
    XGetGeometry(
        dpy,
        drawable,
        &rootwin,
        &x, &y, &width, &height,
        &border_width,
        &depth
    );
    if (x11_untrap_errors())
        return 0;

    if (px)      *px      = x;
    if (py)      *py      = y;
    if (pwidth)  *pwidth  = width;
    if (pheight) *pheight = height;
    return 1;
}

void x11_wait_event(Display *dpy, Window w, int type)
{
    XEvent e;
    while (!XCheckTypedWindowEvent(dpy, w, type, &e))
        delay_usec(10);
}

// Returns X window background color at specified location
int
x11_get_window_colorkey(Display *dpy, Window w, int x, int y, unsigned int *ck)
{
    XClearArea(dpy, w, x, y, 1, 1, False);

    XImage *img = XGetImage(dpy, w, x, y, 1, 1, AllPlanes, ZPixmap);
    if (img == NULL)
        return -1;

    if (ck)
        *ck = XGetPixel(img, 0, 0);

    XDestroyImage(img);
    return 0;
}
