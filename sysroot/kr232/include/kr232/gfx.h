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

#endif
