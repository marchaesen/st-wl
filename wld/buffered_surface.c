/* wld: buffered_surface.c
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
#include "buffered_surface.h"

struct buffer_entry
{
    struct buffer * buffer;
    bool busy;
};

struct wld_surface * buffered_surface_create
    (struct wld_context * context, uint32_t width, uint32_t height,
     uint32_t format, uint32_t flags, struct buffer_socket * buffer_socket)
{
    struct wld_surface * surface;

    if (!(surface = malloc(sizeof *surface)))
        return NULL;

    surface->context = context;
    surface->entries = NULL;
    surface->back = NULL;
    surface->entries_size = 0;
    surface->entries_capacity = 0;
    surface->buffer_socket = buffer_socket;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->flags = flags;

    return surface;
}

struct buffer * surface_back(struct wld_surface * surface)
{
    unsigned index;

    if (surface->back)
        return surface->back->buffer;

    /* The buffer socket may need to process any incoming buffer releases. */
    if (surface->buffer_socket)
        surface->buffer_socket->impl->process(surface->buffer_socket);

    for (index = 0; index < surface->entries_size; ++index)
    {
        if (!surface->entries[index].busy)
        {
            surface->back = &surface->entries[index];
            return surface->back->buffer;
        }
    }

    /* If there are no free buffers, we need to allocate another one. */
    struct buffer * buffer;

    buffer = surface->context->impl->create_buffer
        (surface->context, surface->width, surface->height,
         surface->format, surface->flags);

    if (!buffer)
        goto error0;

    if (surface->entries_size == surface->entries_capacity)
    {
        struct buffer_entry * new_entries;
        size_t new_capacity = surface->entries_capacity * 2 + 1;

        new_entries = realloc(surface->entries,
                              new_capacity * sizeof surface->entries[0]);

        if (!new_entries)
            goto error1;

        surface->entries = new_entries;
        surface->entries_capacity = new_capacity;
    }

    surface->back = &surface->entries[surface->entries_size++];
    *surface->back = (struct buffer_entry) {
        .buffer = buffer,
        .busy = false
    };

    return buffer;

  error1:
    wld_buffer_unreference(&buffer->base);
  error0:
    return NULL;
}

bool surface_release(struct wld_surface * surface, struct buffer * buffer)
{
    unsigned index;

    for (index = 0; index < surface->entries_size; ++index)
    {
        if (surface->entries[index].buffer == buffer)
        {
            surface->entries[index].busy = false;
            return true;
        }
    }

    return false;
}
