#include "graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the appropriate graphics driver based on compile-time flags
#ifdef GFX_SDL3
    #include "gfx/sdl3.c"
#elif defined(GFX_SDL)
    #include "gfx/sdl.c"
#elif defined(GFX_GTK)
    #include "gfx/gtk.c"
#elif defined(GFX_X11)
    #include "gfx/x11.c"
#else
    #error "No graphics driver selected. Use -DGFX_SDL3, -DGFX_SDL, -DGFX_GTK, or -DGFX_X11"
#endif