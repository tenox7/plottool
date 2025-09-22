#include "../graphics.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <fontconfig/fontconfig.h>

static bool sdl_initialized = false;
static SDL_TimerID render_timer = 0;

static Uint32 render_timer_callback(Uint32 interval, void *param) {
    (void)param;
    SDL_Event event = {0};
    event.type = SDL_USEREVENT;
    event.user.code = 1;
    SDL_PushEvent(&event);
    return interval;
}

bool graphics_init(void) {
    if (sdl_initialized) {
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF initialization failed: %s\n", TTF_GetError());
        SDL_Quit();
        return false;
    }

    sdl_initialized = true;
    return true;
}

void graphics_cleanup(void) {
    if (!sdl_initialized) return;

    TTF_Quit();
    SDL_Quit();
    sdl_initialized = false;
}

window_t *window_create(const char *title, int32_t width, int32_t height) {
    window_t *window = malloc(sizeof(window_t));
    if (!window) return NULL;

    SDL_Window *sdl_window = SDL_CreateWindow(title,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             width, height,
                                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!sdl_window) {
        free(window);
        return NULL;
    }

    window->handle = sdl_window;
    return window;
}

void window_destroy(window_t *window) {
    if (!window) return;
    SDL_DestroyWindow((SDL_Window*)window->handle);
    free(window);
}

void window_set_fullscreen(window_t *window, bool fullscreen) {
    if (!window) return;
    SDL_SetWindowFullscreen((SDL_Window*)window->handle,
                           fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !width || !height) return;
    SDL_GetWindowSize((SDL_Window*)window->handle, width, height);
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    SDL_Renderer *sdl_renderer = SDL_CreateRenderer((SDL_Window*)window->handle,
                                                   -1, SDL_RENDERER_ACCELERATED);
    if (!sdl_renderer) {
        free(renderer);
        return NULL;
    }

    renderer->handle = sdl_renderer;
    return renderer;
}

void renderer_destroy(renderer_t *renderer) {
    if (!renderer) return;
    SDL_DestroyRenderer((SDL_Renderer*)renderer->handle);
    free(renderer);
}

void renderer_clear(renderer_t *renderer, color_t color) {
    if (!renderer) return;
    SDL_SetRenderDrawColor((SDL_Renderer*)renderer->handle,
                          color.r, color.g, color.b, color.a);
    SDL_RenderClear((SDL_Renderer*)renderer->handle);
}

void renderer_present(renderer_t *renderer) {
    if (!renderer) return;
    SDL_RenderPresent((SDL_Renderer*)renderer->handle);
}

void renderer_set_color(renderer_t *renderer, color_t color) {
    if (!renderer) return;
    SDL_SetRenderDrawColor((SDL_Renderer*)renderer->handle,
                          color.r, color.g, color.b, color.a);
}

void renderer_draw_line(renderer_t *renderer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!renderer) return;
    SDL_RenderDrawLine((SDL_Renderer*)renderer->handle, x1, y1, x2, y2);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;
    SDL_Rect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
    SDL_RenderDrawRect((SDL_Renderer*)renderer->handle, &sdl_rect);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;
    SDL_Rect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRect((SDL_Renderer*)renderer->handle, &sdl_rect);
}

font_t *font_create(const char *path, int32_t size) {
    font_t *font = malloc(sizeof(font_t));
    if (!font) return NULL;

    (void)path; /* Ignore path parameter, use system font */

    TTF_Font *ttf_font = NULL;
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (config) {
        FcPattern *pattern = FcPatternCreate();
        FcPatternAddString(pattern, FC_FAMILY, (FcChar8*)"monospace");

        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult result;
        FcPattern *match = FcFontMatch(config, pattern, &result);
        if (match) {
            FcChar8 *font_path = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &font_path) == FcResultMatch) {
                ttf_font = TTF_OpenFont((char*)font_path, size);
            }
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pattern);
        FcConfigDestroy(config);
    }

    if (!ttf_font) {
        free(font);
        return NULL;
    }

    font->handle = ttf_font;
    return font;
}

void font_destroy(font_t *font) {
    if (!font) return;
    TTF_CloseFont((TTF_Font*)font->handle);
    free(font);
}

void font_draw_text(renderer_t *renderer, font_t *font, color_t color,
                    int32_t x, int32_t y, const char *text) {
    if (!renderer || !font || !text) return;

    SDL_Color sdl_color = {color.r, color.g, color.b, color.a};
    SDL_Surface *surface = TTF_RenderUTF8_Blended((TTF_Font*)font->handle, text, sdl_color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface((SDL_Renderer*)renderer->handle, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_RenderCopy((SDL_Renderer*)renderer->handle, texture, NULL, &dest);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    if (!font || !text) return;

    int w, h;
    if (TTF_SizeUTF8((TTF_Font*)font->handle, text, &w, &h) == 0) {
        if (width) *width = w;
        if (height) *height = h;
    }
}

bool graphics_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
    }
    return true;
}

bool graphics_wait_events(void) {
    SDL_Event event;

    if (SDL_WaitEvent(&event)) {
        do {
            switch (event.type) {
                case SDL_QUIT:
                    return false;
                case SDL_WINDOWEVENT:
                case SDL_KEYDOWN:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEMOTION:
                case SDL_USEREVENT:
                    break;
            }
        } while (SDL_PollEvent(&event));

        return true;
    }

    return false;
}

void graphics_start_render_timer(int fps) {
    if (render_timer != 0) {
        SDL_RemoveTimer(render_timer);
    }

    Uint32 interval = 1000 / fps;
    render_timer = SDL_AddTimer(interval, render_timer_callback, NULL);
}

void graphics_stop_render_timer(void) {
    if (render_timer != 0) {
        SDL_RemoveTimer(render_timer);
        render_timer = 0;
    }
}