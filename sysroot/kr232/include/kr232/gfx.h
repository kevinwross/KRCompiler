#ifndef KR232_GFX_H
#define KR232_GFX_H

/* Drawing through the back surface of the aperture. Colours are 0x00RRGGBB in the
   low three bytes, the layout the framebuffer's pixels use. The current colour is
   set first so a call carries four arguments, which is what the ABI passes in
   registers; a fifth argument needs the stack form the backend does not emit yet. */
#define KR232_COLOR(r, g, b) (((unsigned)(r) << 16) | ((unsigned)(g) << 8) | (unsigned)(b))

void gfx_set_colour(unsigned colour);
void gfx_clear(void);
void gfx_fill_rect(int x, int y, int width, int height);

/* At most four arguments, because that is what the ABI passes in registers: the
   commands that need more travel as one block behind a pointer, and the ones that
   want a colour use the one gfx_set_colour recorded. Each answers the device
   STATUS, whose bit 0 is ERROR, so a caller sees a refusal instead of assuming
   success -- DRAW_TEXT always refuses until the glyph atlas ships. */
int gfx_copy_rect(int x, int y, int width, int height);
int gfx_draw_line(int x0, int y0, int x1, int y1);
int gfx_draw_text(int x, int y, const char* text, int length);
int gfx_upload(const void* pixels, int width, int height, int stride);

#endif
