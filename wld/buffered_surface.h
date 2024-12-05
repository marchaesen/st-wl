#ifndef __jUFFERED_SURFACE_H__
#define __BUFFERED_SURFACE_H__

struct buffer * surface_back(struct wld_surface * surface);
bool surface_release(struct wld_surface * surface,
                     struct buffer * buffer);


#endif

