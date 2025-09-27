#include "../graphics.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <fontconfig/fontconfig.h>

static bool sdl_initialized = false;
static bool window_resized = false;

static uint32_t frame_count = 0;
static uint64_t fps_last_time = 0;
static float current_fps = 0.0f;

static graphics_event_t pending_event = {GRAPHICS_EVENT_NONE, 0};
static bool fullscreen_state = false;

bool graphics_init(void) {
    if (sdl_initialized) {
        return true;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }

    if (!TTF_Init()) {
        fprintf(stderr, "TTF initialization failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    fps_last_time = SDL_GetTicks();
    frame_count = 0;
    current_fps = 0.0f;

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

    SDL_Window *sdl_window = SDL_CreateWindow(title, width, height,
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
    SDL_SetWindowFullscreen((SDL_Window*)window->handle, fullscreen);
    fullscreen_state = fullscreen;
}

bool window_is_fullscreen(window_t *window) {
    if (!window) return false;

    SDL_Window* sdl_window = (SDL_Window*)window->handle;
    return SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN;
}

void window_set_topmost(window_t *window, bool topmost) {
    if (!window) return;

    SDL_Window* sdl_window = (SDL_Window*)window->handle;
    SDL_SetWindowAlwaysOnTop(sdl_window, topmost);
}

void window_get_size(window_t *window, int32_t *width, int32_t *height) {
    if (!window || !width || !height) return;
    SDL_GetWindowSize((SDL_Window*)window->handle, width, height);
}

renderer_t *renderer_create(window_t *window) {
    renderer_t *renderer = malloc(sizeof(renderer_t));
    if (!renderer) return NULL;

    SDL_Renderer *sdl_renderer = SDL_CreateRenderer((SDL_Window*)window->handle, NULL);
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
    SDL_RenderLine((SDL_Renderer*)renderer->handle, x1, y1, x2, y2);
}

void renderer_draw_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;
    SDL_FRect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
    SDL_RenderRect((SDL_Renderer*)renderer->handle, &sdl_rect);
}

void renderer_fill_rect(renderer_t *renderer, rect_t rect) {
    if (!renderer) return;
    SDL_FRect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
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
    SDL_Surface *surface = TTF_RenderText_Blended((TTF_Font*)font->handle, text, 0, sdl_color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface((SDL_Renderer*)renderer->handle, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect dest = {x, y, surface->w, surface->h};
    SDL_RenderTexture((SDL_Renderer*)renderer->handle, texture, NULL, &dest);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void font_get_text_size(font_t *font, const char *text, int32_t *width, int32_t *height) {
    if (!font || !text) return;

    int w, h;
    if (TTF_GetStringSize((TTF_Font*)font->handle, text, 0, &w, &h)) {
        if (width) *width = w;
        if (height) *height = h;
    } else {
        // Fallback values if TTF_GetStringSize fails
        if (width) *width = 0;
        if (height) *height = 0;
    }
}

bool graphics_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
    }
    return true;
}

bool graphics_wait_events(void) {
    SDL_Event event;

    extern int config_get_max_fps(void);
    int fps = config_get_max_fps();
    if (fps <= 0) fps = 1;
    Uint32 timeout_ms = 1000 / fps;

    if (SDL_WaitEventTimeout(&event, timeout_ms)) {
        do {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return false;
                case SDL_EVENT_WINDOW_RESIZED:
                    window_resized = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_Q:
                            pending_event.type = GRAPHICS_EVENT_QUIT;
                            pending_event.key = KEY_Q;
                            break;
                        case SDLK_R:
                            pending_event.type = GRAPHICS_EVENT_REFRESH;
                            pending_event.key = KEY_R;
                            break;
                        case SDLK_F:
                            pending_event.type = GRAPHICS_EVENT_FULLSCREEN_TOGGLE;
                            pending_event.key = KEY_F;
                            break;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_USER:
                    break;
            }
        } while (SDL_PollEvent(&event));
    }

    return true;
}

bool graphics_get_event(graphics_event_t *event) {
    if (!event) return false;

    if (pending_event.type != GRAPHICS_EVENT_NONE) {
        *event = pending_event;
        pending_event.type = GRAPHICS_EVENT_NONE;
        return true;
    }

    event->type = GRAPHICS_EVENT_NONE;
    return false;
}

void graphics_start_render_timer(int fps) {
    (void)fps;
}

void graphics_stop_render_timer(void) {
}

bool window_was_resized(void) {
    bool result = window_resized;
    window_resized = false; /* Reset flag */
    return result;
}

void graphics_draw_fps_counter(renderer_t *renderer, font_t *font, bool enabled) {
    if (!enabled || !renderer || !font) return;

    frame_count++;
    uint64_t current_time = SDL_GetTicks();

    if (current_time - fps_last_time >= 1000) {
        current_fps = (float)frame_count * 1000.0f / (float)(current_time - fps_last_time);
        frame_count = 0;
        fps_last_time = current_time;
    }

    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", current_fps);

    color_t fps_bg_color = {0, 0, 0, 180};
    color_t fps_text_color = {255, 255, 0, 255};
    rect_t fps_bg_rect = {5, 5, 80, 20};

    renderer_set_color(renderer, fps_bg_color);
    renderer_fill_rect(renderer, fps_bg_rect);

    font_draw_text(renderer, font, fps_text_color, 10, 8, fps_text);
}