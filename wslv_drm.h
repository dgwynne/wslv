/* */

/*
 * Copyright (c) 2023 David Gwynne <david@gwynne.id.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _WSLV_DRM_H_
#define _WSLV_DRM_H_

int		drm_init(void);
void		drm_flush(lv_disp_drv_t *, const lv_area_t *, lv_color_t *);
void		drm_wait_vsync(lv_disp_drv_t *);
void		drm_get_sizes(lv_coord_t *, lv_coord_t *, lv_coord_t *,
		    uint32_t *);

void		*drm_get_fb(int);
void		 drm_event_set(lv_disp_drv_t *);
int		 drm_svideo(int);
void		 drm_refresh(void);

#endif /* _WSLV_DRM_H_ */
