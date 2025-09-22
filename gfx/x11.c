#include "../graphics.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>

typedef struct {
    Display *display;
    Window window;
    Pixmap pixmap;
    int screen;
    GC gc;
    Colormap colormap;
    XFontStruct *font_info;
    int width, height;
    bool should_quit;
    unsigned long bg_color;
    Atom wm_delete_window;
} x11_window_context_t;

typedef struct {
    x11_window_context_t *window_context;
    unsigned long current_color;
} x11_renderer_context_t;

typedef struct {
    XFontStruct *font_struct;
    Display *display;
} x11_font_context_t;

static bool x11_initialized = false;
static x11_window_context_t *x11_active_window = NULL;

static unsigned long x11_create_color(Display *display, int screen, color_t color) {
    Colormap colormap = DefaultColormap(display, screen);
    XColor xcolor;

    xcolor.red = color.r << 8;
    xcolor.green = color.g << 8;
    xcolor.blue = color.b << 8;
    xcolor.flags = DoRed | DoGreen | DoBlue;

    if (XAllocColor(display, colormap, &xcolor)) {
        return xcolor.pixel;
    }

    return BlackPixel(display, screen);
}

bool graphics_init(void) {
    if (x11_initialized) {
        return true;
    }

    x11_initialized = true;
    return true;
}

void graphics_cleanup(void) {
    if (!x11_initialized) return;
    x11_initialized = false;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window = malloc(sizeof(window_t));
    if (!window) return NULL;

    x11_window_context_t *ctx = malloc(sizeof(x11_window_context_t));
    if (!ctx) {
        free(window);
        return NULL;
    }

    ctx->display = XOpenDisplay(NULL);
    if (!ctx->display) {
        free(ctx);
        free(window);
        return NULL;
    }

    ctx->screen = DefaultScreen(ctx->display);
    ctx->width = width;
    ctx->height = height;
    ctx->should_quit = false;

    Window root = RootWindow(ctx->display, ctx->screen);
    ctx->bg_color = WhitePixel(ctx->display, ctx->screen);

    ctx->window = XCreateSimpleWindow(ctx->display, root, 0, 0, width, height, 1,
                                     BlackPixel(ctx->display, ctx->screen), ctx->bg_color);

    XStoreName(ctx->display, ctx->window, title);

    ctx->wm_delete_window = XInternAtom(ctx->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ctx->display, ctx->window, &ctx->wm_delete_window, 1);

    XSelectInput(ctx->display, ctx->window,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask | PointerMotionMask);

    ctx->pixmap = XCreatePixmap(ctx->display, ctx->window, width, height,
                               DefaultDepth(ctx->display, ctx->screen));

    ctx->gc = XCreateGC(ctx->display, ctx->pixmap, 0, NULL);
    XSetBackground(ctx->display, ctx->gc, ctx->bg_color);
    XSetForeground(ctx->display, ctx->gc, BlackPixel(ctx->display, ctx->screen));

    ctx->font_info = XLoadQueryFont(ctx->display, "fixed");
    if (!ctx->font_info) {
        ctx->font_info = XLoadQueryFont(ctx->display, "*");
    }
    if (ctx->font_info) {
        XSetFont(ctx->display, ctx->gc, ctx->font_info->fid);
    }

    XFillRectangle(ctx->display, ctx->pixmap, ctx->gc, 0, 0, width, height);

    XMapWindow(ctx->display, ctx->window);
    XFlush(ctx->display);

    x11_active_window = ctx;

    window->handle = ctx;
    return window;
}

void window_destroy(window_t *window) {
    if (!window) return;

    x11_window_context_t *ctx = (x11_window_context_t*)window->handle;
    if (ctx) {
        if (x11_active_window == ctx) {
            x11_active_window = NULL;
        }

        if (ctx->font_info) {
            XFreeFont(ctx->display, ctx->font_info);
        }
        if (ctx->gc) {
            XFreeGC(ctx->display, ctx->gc);
        }
        if (ctx->pixmap) {
            XFreePixmap(ctx->display, ctx->pixmap);
        }
        if (ctx->window) {
            XDestroyWindow(ctx->display, ctx->window);
        }
        if (ctx->display) {
            XCloseDisplay(ctx->display);
        }
        free(ctx);
    }
    free(window);
}

void window_set_fullscreen(window_t *window, bool fullscreen) {
    if (!window) return;

    x11_window_context_t *ctx = (x11_window_context_t*)window->handle;

    if (fullscreen) {
        XEvent event;
        event.type = ClientMessage;
        event.xclient.window = ctx->window;
        event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
        event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_FULLSCREEN", False);
        event.xclient.data.l[2] = 0;

        XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
    } else {
        XEvent event;
        event.type = ClientMessage;
        event.xclient.window = ctx->window;
        event.xclient.message_type = XInternAtom(ctx->display, "_NET_WM_STATE", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 0; // _NET_WM_STATE_REMOVE
        event.xclient.data.l[1] = XInternAtom(ctx->display, "_NET_WM_STATE_FULLSCREEN", False);
        event.xclient.data.l[2] = 0;

        XSendEvent(ctx->display, DefaultRootWindow(ctx->display), False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
    }

    XFlush(ctx->display);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !width || !height) return;

    x11_window_context_t *ctx = (x11_window_context_t*)window->handle;

    // Get the actual current window size from X11
    Window root;
    int x, y;
    unsigned int w, h, border_width, depth;

    if (XGetGeometry(ctx->display, ctx->window, &root, &x, &y, &w, &h, &border_width, &depth)) {
        // Update our cached values if they changed
        if ((int)w != ctx->width || (int)h != ctx->height) {
            ctx->width = w;
            ctx->height = h;

            // Recreate pixmap with new size
            XFreePixmap(ctx->display, ctx->pixmap);
            ctx->pixmap = XCreatePixmap(ctx->display, ctx->window, ctx->width, ctx->height,
                                       DefaultDepth(ctx->display, ctx->screen));

            // Clear the new pixmap
            XSetForeground(ctx->display, ctx->gc, ctx->bg_color);
            XFillRectangle(ctx->display, ctx->pixmap, ctx->gc, 0, 0, ctx->width, ctx->height);
        }

        *width = ctx->width;
        *height = ctx->height;
    } else {
        // Fallback to cached values
        *width = ctx->width;
        *height = ctx->height;
    }
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    x11_renderer_context_t *ctx = malloc(sizeof(x11_renderer_context_t));
    if (!ctx) {
        free(renderer);
        return NULL;
    }

    ctx->window_context = (x11_window_context_t*)window->handle;
    ctx->current_color = BlackPixel(ctx->window_context->display, ctx->window_context->screen);

    renderer->handle = ctx;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    if (ctx) {
        free(ctx);
    }
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    unsigned long x11_color = x11_create_color(ctx->window_context->display,
                                               ctx->window_context->screen, color);

    XSetForeground(ctx->window_context->display, ctx->window_context->gc, x11_color);
    XFillRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, 0, 0,
                   ctx->window_context->width, ctx->window_context->height);
}

void renderer_present(renderer_t *renderer) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;

    XCopyArea(ctx->window_context->display,
              ctx->window_context->pixmap,
              ctx->window_context->window,
              ctx->window_context->gc,
              0, 0,
              ctx->window_context->width, ctx->window_context->height,
              0, 0);

    XFlush(ctx->window_context->display);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    ctx->current_color = x11_create_color(ctx->window_context->display,
                                         ctx->window_context->screen, color);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
    XDrawLine(ctx->window_context->display, ctx->window_context->pixmap,
              ctx->window_context->gc, x1, y1, x2, y2);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
    XDrawRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, rect.x, rect.y, rect.w, rect.h);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;

    x11_renderer_context_t *ctx = (x11_renderer_context_t*)renderer->handle;
    XSetForeground(ctx->window_context->display, ctx->window_context->gc, ctx->current_color);
    XFillRectangle(ctx->window_context->display, ctx->window_context->pixmap,
                   ctx->window_context->gc, rect.x, rect.y, rect.w, rect.h);
}

font_t *font_create(const char *path, int32_t size) {
    font_t *font = malloc(sizeof(font_t));
    if (!font) return NULL;

    x11_font_context_t *ctx = malloc(sizeof(x11_font_context_t));
    if (!ctx) {
        free(font);
        return NULL;
    }

    ctx->display = x11_active_window ? x11_active_window->display : NULL;
    if (!ctx->display) {
        free(ctx);
        free(font);
        return NULL;
    }

    char font_pattern[256];

    // Try normal weight, roman (non-italic) fonts first
    snprintf(font_pattern, sizeof(font_pattern), "*-*-medium-r-*-*-%d-*-*-*-*-*-*-*", size);
    ctx->font_struct = XLoadQueryFont(ctx->display, font_pattern);

    if (!ctx->font_struct) {
        // Try any normal weight, roman font
        snprintf(font_pattern, sizeof(font_pattern), "*-*-normal-r-*-*-%d-*-*-*-*-*-*-*", size);
        ctx->font_struct = XLoadQueryFont(ctx->display, font_pattern);
    }

    if (!ctx->font_struct) {
        // Try fixed font (usually normal)
        ctx->font_struct = XLoadQueryFont(ctx->display, "fixed");
    }

    if (!ctx->font_struct) {
        // Try 6x13 which is usually normal
        ctx->font_struct = XLoadQueryFont(ctx->display, "6x13");
    }

    if (!ctx->font_struct) {
        // Last resort - any font
        ctx->font_struct = XLoadQueryFont(ctx->display, "*");
    }

    if (!ctx->font_struct) {
        free(ctx);
        free(font);
        return NULL;
    }

    font->handle = ctx;
    return font;
}

void font_destroy(font_t *font) {
    if (!font) return;

    x11_font_context_t *ctx = (x11_font_context_t*)font->handle;
    if (ctx) {
        if (ctx->font_struct && ctx->display) {
            XFreeFont(ctx->display, ctx->font_struct);
        }
        free(ctx);
    }
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    if (!renderer || !font || !text) return;

    x11_renderer_context_t *rctx = (x11_renderer_context_t*)renderer->handle;
    x11_font_context_t *fctx = (x11_font_context_t*)font->handle;

    unsigned long text_color = x11_create_color(rctx->window_context->display,
                                               rctx->window_context->screen, color);

    XSetForeground(rctx->window_context->display, rctx->window_context->gc, text_color);

    if (fctx->font_struct) {
        XSetFont(rctx->window_context->display, rctx->window_context->gc, fctx->font_struct->fid);

        y += fctx->font_struct->ascent;
        XDrawString(rctx->window_context->display, rctx->window_context->pixmap,
                    rctx->window_context->gc, x, y, text, strlen(text));
    }
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    if (!font || !text || !width || !height) return;

    x11_font_context_t *ctx = (x11_font_context_t*)font->handle;
    if (ctx->font_struct) {
        *width = XTextWidth(ctx->font_struct, text, strlen(text));
        *height = ctx->font_struct->ascent + ctx->font_struct->descent;
    } else {
        *width = 0;
        *height = 0;
    }
}

bool graphics_poll_events(void) {
    if (!x11_active_window) return true;

    while (XPending(x11_active_window->display)) {
        XEvent event;
        XNextEvent(x11_active_window->display, &event);

        switch (event.type) {
            case Expose:
                break;
            case ConfigureNotify:
                if (event.xconfigure.width != x11_active_window->width ||
                    event.xconfigure.height != x11_active_window->height) {
                    x11_active_window->width = event.xconfigure.width;
                    x11_active_window->height = event.xconfigure.height;

                    XFreePixmap(x11_active_window->display, x11_active_window->pixmap);
                    x11_active_window->pixmap = XCreatePixmap(x11_active_window->display,
                                                             x11_active_window->window,
                                                             x11_active_window->width,
                                                             x11_active_window->height,
                                                             DefaultDepth(x11_active_window->display,
                                                                         x11_active_window->screen));

                    XSetForeground(x11_active_window->display, x11_active_window->gc,
                                  x11_active_window->bg_color);
                    XFillRectangle(x11_active_window->display, x11_active_window->pixmap,
                                  x11_active_window->gc, 0, 0,
                                  x11_active_window->width, x11_active_window->height);
                }
                break;
            case KeyPress:
                if (XLookupKeysym(&event.xkey, 0) == XK_Escape) {
                    return false;
                }
                break;
            case ClientMessage:
                if ((unsigned long)event.xclient.data.l[0] == x11_active_window->wm_delete_window) {
                    x11_active_window->should_quit = true;
                    return false;
                }
                break;
        }
    }

    return !x11_active_window->should_quit;
}

bool graphics_wait_events(void) {
    if (!x11_active_window) return true;

    // Check if the window should quit first
    if (x11_active_window->should_quit) {
        return false;
    }

    // Just sleep a bit and then check for events non-blocking
    usleep(16666); // ~60 FPS (16.7ms)

    return graphics_poll_events();
}

void graphics_start_render_timer(int fps) {
    (void)fps;
}

void graphics_stop_render_timer(void) {
}