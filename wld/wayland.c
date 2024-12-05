/* wld: wayland.c
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

#include "wayland.h"
#include "wayland-private.h"
#include "wld-private.h"

#include <stdlib.h>
#include <wayland-client.h>

struct wayland_buffer
{
    struct wld_exporter exporter;
    struct wld_destructor destructor;
    struct wl_buffer * wl;
};

EXPORT
struct wld_context * wld_wayland_create_context
    (struct wl_display * display, ...)
{
    struct wayland_context * context = NULL;
    struct wl_event_queue * queue;

    if (!(queue = wl_display_create_queue(display)))
        return NULL;

    context = wayland_create_context(display, queue);

    if (!context)
    {
        DEBUG("Could not initialize any of the specified implementations\n");
        return NULL;
    }

    context->display = display;
    context->queue = queue;

    return &context->base;
}

EXPORT
bool wld_wayland_has_format(struct wld_context * base, uint32_t format)
{
    return wayland_has_format(base, format);
}

static bool buffer_export(struct wld_exporter * exporter,
                          struct wld_buffer * buffer,
                          uint32_t type, union wld_object * object)
{
    struct wayland_buffer * wayland_buffer
        = CONTAINER_OF(exporter, struct wayland_buffer, exporter);

    switch (type)
    {
        case WLD_WAYLAND_OBJECT_BUFFER:
            object->ptr = wayland_buffer->wl;
            return true;
        default: return false;
    }
}

static void buffer_destroy(struct wld_destructor * destructor)
{
    struct wayland_buffer * wayland_buffer
        = CONTAINER_OF(destructor, struct wayland_buffer, destructor);

    wl_buffer_destroy(wayland_buffer->wl);
    free(wayland_buffer);
}

bool wayland_buffer_add_exporter(struct buffer * buffer, struct wl_buffer * wl)
{
    struct wayland_buffer * wayland_buffer;

    if (!(wayland_buffer = malloc(sizeof *wayland_buffer)))
        return false;

    wayland_buffer->wl = wl;
    wayland_buffer->exporter.export = &buffer_export;
    wld_buffer_add_exporter(&buffer->base, &wayland_buffer->exporter);
    wayland_buffer->destructor.destroy = &buffer_destroy;
    wld_buffer_add_destructor(&buffer->base, &wayland_buffer->destructor);

    return true;
}

void sync_done(void * data, struct wl_callback * callback, uint32_t msecs)
{
    bool * done = data;

    *done = true;
    wl_callback_destroy(callback);
}
