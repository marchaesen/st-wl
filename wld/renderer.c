/* wld: renderer.c
 *
 * Copyright (c) 2013, 2014 Michael Forney
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wld-private.h"
#include "renderer.h"
#include "buffered_surface.h"

void renderer_initialize(struct wld_renderer * renderer)
{
    renderer->target = NULL;
}

EXPORT
void wld_destroy_renderer(struct wld_renderer * renderer)
{
    renderer_destroy(renderer);
}

EXPORT
uint32_t wld_capabilities(struct wld_renderer * renderer,
                          struct wld_buffer * buffer)
{
    return renderer_capabilities(renderer, (struct buffer *) buffer);
}

EXPORT
bool wld_set_target_buffer(struct wld_renderer * renderer,
                           struct wld_buffer * buffer)
{
    if (!renderer_set_target(renderer, (struct buffer *) buffer))
        return false;

    renderer->target = buffer;

    return true;
}

EXPORT
bool wld_set_target_surface(struct wld_renderer * renderer,
                            struct wld_surface * surface)
{
    struct buffer * back_buffer;

    if (!(back_buffer = surface_back(surface)))
        return false;

    return renderer_set_target(renderer, back_buffer);
}

EXPORT
void wld_fill_rectangle(struct wld_renderer * renderer, uint32_t color,
                        int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    renderer_fill_rectangle(renderer, color, x, y, width, height);
}

EXPORT
void wld_fill_region(struct wld_renderer * renderer, uint32_t color,
                     pixman_region32_t * region)
{
    renderer_fill_region(renderer, color, region);
}

EXPORT
void wld_copy_rectangle(struct wld_renderer * renderer,
                        struct wld_buffer * buffer,
                        int32_t dst_x, int32_t dst_y,
                        int32_t src_x, int32_t src_y,
                        uint32_t width, uint32_t height)
{
    renderer_copy_rectangle(renderer, (struct buffer *) buffer,
                                   dst_x, dst_y, src_x, src_y, width, height);
}

EXPORT
void wld_copy_region(struct wld_renderer * renderer,
                     struct wld_buffer * buffer,
                     int32_t dst_x, int32_t dst_y, pixman_region32_t * region)
{
    renderer_copy_region(renderer, (struct buffer *) buffer,
                                dst_x, dst_y, region);
}

EXPORT
void wld_draw_text(struct wld_renderer * renderer,
                   struct wld_font * font_base, uint32_t color,
                   int32_t x, int32_t y, const char * text, uint32_t length,
                   struct wld_extents * extents)
{
    struct font * font = (void *) font_base;

    renderer_draw_text(renderer, font, color, x, y, text, length,
                              extents);
}

EXPORT
void wld_flush(struct wld_renderer * renderer)
{
    renderer_flush(renderer);
    renderer_set_target(renderer, NULL);
}

