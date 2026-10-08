#include "plib/gnw/svga.h"

#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/winmain.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

// screen rect
Rect scr_size;

// 0x6ACA18
ScreenBlitFunc* scr_blit = GNW95_ShowRect;

SDL_Window* gSdlWindow = NULL;
SDL_Surface* gSdlSurface = NULL;
SDL_Renderer* gSdlRenderer = NULL;
SDL_Texture* gSdlTexture = NULL;
SDL_Surface* gSdlTextureSurface = NULL;

// Miyoo Mini: part of gSdlTextureSurface changed since the last present
// (w == 0 means nothing changed). renderPresent() skips presenting when
// nothing changed, which saves CPU and battery on the many static screens
// of the game. The whole texture is still uploaded: this device's renderer
// ignores the position of a partial update.
static SDL_Rect gScreenDirtyRect = { 0, 0, 0, 0 };
static Uint32 gLastPresentTicks = 0;

// Safety net: present at least this often even when nothing seems to change.
static const Uint32 kForcedPresentIntervalMs = 250;

// Changes every time the 8-bit screen (gSdlSurface) changes.
static unsigned int gScreenContentVersion = 1;

struct ScreenPaletteRangeCache {
    SDL_Surface* surface;
    unsigned int version;
    int start;
    int count;
    bool used;
};

static ScreenPaletteRangeCache gScreenPaletteRangeCache[4];
static int gScreenPaletteRangeCacheNext = 0;

static void screenMarkDirty(int x, int y, int width, int height)
{
    if (gSdlTextureSurface == NULL) {
        return;
    }

    int right = x + width;
    int bottom = y + height;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right > gSdlTextureSurface->w) {
        right = gSdlTextureSurface->w;
    }
    if (bottom > gSdlTextureSurface->h) {
        bottom = gSdlTextureSurface->h;
    }
    if (right <= x || bottom <= y) {
        return;
    }

    if (gScreenDirtyRect.w != 0) {
        int oldRight = gScreenDirtyRect.x + gScreenDirtyRect.w;
        int oldBottom = gScreenDirtyRect.y + gScreenDirtyRect.h;
        if (gScreenDirtyRect.x < x) {
            x = gScreenDirtyRect.x;
        }
        if (gScreenDirtyRect.y < y) {
            y = gScreenDirtyRect.y;
        }
        if (oldRight > right) {
            right = oldRight;
        }
        if (oldBottom > bottom) {
            bottom = oldBottom;
        }
    }

    gScreenDirtyRect.x = x;
    gScreenDirtyRect.y = y;
    gScreenDirtyRect.w = right - x;
    gScreenDirtyRect.h = bottom - y;
}

static void screenMarkDirtyAll()
{
    if (gSdlTextureSurface != NULL) {
        screenMarkDirty(0, 0, gSdlTextureSurface->w, gSdlTextureSurface->h);
    }
}

// Called by code that changes gSdlSurface and gSdlTextureSurface directly
// (movie frames).
void screenMarkChanged()
{
    gScreenContentVersion++;
    screenMarkDirtyAll();
}

// Returns true when some pixel of the 8-bit screen uses a palette index in
// [start, start + count). The answer is remembered until the screen changes,
// so color cycling over a static screen costs almost nothing.
static bool screenUsesPaletteRange(int start, int count)
{
    if (gSdlSurface == NULL || count <= 0) {
        return false;
    }

    if (start <= 0 && start + count >= 256) {
        return true;
    }

    for (int index = 0; index < 4; index++) {
        ScreenPaletteRangeCache* entry = &(gScreenPaletteRangeCache[index]);
        if (entry->surface == gSdlSurface && entry->version == gScreenContentVersion
            && entry->start == start && entry->count == count) {
            return entry->used;
        }
    }

    bool used = false;
    unsigned int low = static_cast<unsigned int>(start);
    unsigned int range = static_cast<unsigned int>(count);
    const unsigned char* row = static_cast<const unsigned char*>(gSdlSurface->pixels);
    for (int y = 0; y < gSdlSurface->h && !used; y++) {
        for (int x = 0; x < gSdlSurface->w; x++) {
            if (static_cast<unsigned int>(row[x]) - low < range) {
                used = true;
                break;
            }
        }
        row += gSdlSurface->pitch;
    }

    ScreenPaletteRangeCache* entry = &(gScreenPaletteRangeCache[gScreenPaletteRangeCacheNext]);
    gScreenPaletteRangeCacheNext = (gScreenPaletteRangeCacheNext + 1) % 4;
    entry->surface = gSdlSurface;
    entry->version = gScreenContentVersion;
    entry->start = start;
    entry->count = count;
    entry->used = used;

    return used;
}

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CB310
void GNW95_SetPaletteEntries(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);

        // Miyoo Mini: color cycling changes a few palette entries many times
        // per second. When none of them is on screen the image does not change,
        // so the whole-screen conversion (and the present) is skipped. Later
        // blits already use the new colors.
        if (!screenUsesPaletteRange(start, count)) {
            return;
        }

        screenMarkDirtyAll();
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB568
void GNW95_SetPalette(unsigned char* palette)
{
    if (gSdlSurface != NULL && gSdlSurface->format->palette != NULL) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        screenMarkDirtyAll();
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
    }
}

// 0x4CB850
void GNW95_ShowRect(unsigned char* src, unsigned int srcPitch, unsigned int a3, unsigned int srcX, unsigned int srcY, unsigned int srcWidth, unsigned int srcHeight, unsigned int destX, unsigned int destY)
{
    buf_to_buf(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
    screenMarkDirty(static_cast<int>(destX), static_cast<int>(destY), static_cast<int>(srcWidth), static_cast<int>(srcHeight));
    gScreenContentVersion++;
}

bool svga_init(VideoOptions* video_options)
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

    if (video_options->fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }

    gSdlWindow = SDL_CreateWindow(GNW95_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        video_options->width * video_options->scale,
        video_options->height * video_options->scale,
        windowFlags);
    if (gSdlWindow == NULL) {
        return false;
    }

    if (!createRenderer(video_options->width, video_options->height)) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;

        return false;
    }

    gSdlSurface = SDL_CreateRGBSurface(0,
        video_options->width,
        video_options->height,
        8,
        0,
        0,
        0,
        0);
    if (gSdlSurface == NULL) {
        destroyRenderer();

        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    scr_size.ulx = 0;
    scr_size.uly = 0;
    scr_size.lrx = video_options->width - 1;
    scr_size.lry = video_options->height - 1;

    mouse_blit_trans = NULL;
    scr_blit = GNW95_ShowRect;
    mouse_blit = GNW95_ShowRect;

    return true;
}

void svga_exit()
{
    destroyRenderer();

    if (gSdlWindow != NULL) {
        SDL_DestroyWindow(gSdlWindow);
        gSdlWindow = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&scr_size);
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == NULL) {
        return false;
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == NULL) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, NULL, NULL, NULL) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == NULL) {
        return false;
    }

    // New texture: the first present must upload everything.
    screenMarkDirtyAll();

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != NULL) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = NULL;
    }

    if (gSdlTexture != NULL) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = NULL;
    }

    if (gSdlRenderer != NULL) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = NULL;
    }
}

void handleWindowSizeChanged()
{
    destroyRenderer();
    createRenderer(screenGetWidth(), screenGetHeight());

    // Miyoo Mini: the new texture starts black; copy the current screen into
    // it, or only the parts redrawn afterwards would show up.
    if (gSdlSurface != NULL && gSdlTextureSurface != NULL) {
        SDL_BlitSurface(gSdlSurface, NULL, gSdlTextureSurface, NULL);
        screenMarkDirtyAll();
    }
}

void renderPresent()
{
    // Miyoo Mini: nothing changed since the last present, so the screen
    // already shows the right image.
    Uint32 nowTicks = SDL_GetTicks();
    if (gScreenDirtyRect.w == 0 && nowTicks - gLastPresentTicks < kForcedPresentIntervalMs) {
        return;
    }

    if (gScreenDirtyRect.w != 0) {
        SDL_UpdateTexture(gSdlTexture, NULL, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
        gScreenDirtyRect.w = 0;
        gScreenDirtyRect.h = 0;
    }
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, NULL, NULL);
    SDL_RenderPresent(gSdlRenderer);
    gLastPresentTicks = SDL_GetTicks();
}

} // namespace fallout
