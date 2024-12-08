bool renderer_set_target(struct wld_renderer * renderer,
                                struct buffer * buffer);
void renderer_fill_rectangle(struct wld_renderer * renderer,
                                    uint32_t color, int32_t x, int32_t y,
                                    uint32_t width, uint32_t height);
void renderer_fill_region(struct wld_renderer * base, uint32_t color,
                                 pixman_region32_t * region);
void renderer_copy_region(struct wld_renderer * base,
                                 struct buffer * buffer,
                                 int32_t dst_x, int32_t dst_y,
                                 pixman_region32_t * region);
void renderer_draw_text(struct wld_renderer * renderer,
                               struct font * font, uint32_t color,
                               int32_t x, int32_t y,
                               const char * text, uint32_t length,
                               struct wld_extents * extents);
void renderer_composite_image(struct wld_renderer * base,
                              pixman_image_t *src,
                              pixman_image_t *mask,
                              int32_t x, int32_t y,
                              uint32_t width, uint32_t height);
void renderer_flush(struct wld_renderer * renderer);
void renderer_destroy(struct wld_renderer * renderer);

