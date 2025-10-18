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

#include <spng.h>
#include <lvgl.h>

#include "lvgl/src/draw/lv_image_decoder_private.h"

static int
lv_spng_read(spng_ctx *ctx, void *usr, void *dst, size_t len)
{
	lv_fs_file_t *f = usr;
	uint32_t resid;

	lv_fs_read(f, dst, len, &resid);
	if (resid != len)
		return (SPNG_IO_EOF);

	return (0);
}

static lv_res_t
lv_spng_info(lv_image_decoder_t *dec, const void *src,
    lv_image_header_t *header)
{
	lv_image_src_t stype = lv_image_src_get_type(src);
	spng_ctx *ctx;
	struct spng_ihdr ihdr;
	lv_fs_file_t f;
        const lv_image_dsc_t *idsc;
	lv_res_t res = LV_RESULT_INVALID;

	ctx = spng_ctx_new(0);
	if (ctx == NULL)
		return (LV_RESULT_INVALID);

	switch (stype) {
	case LV_IMAGE_SRC_FILE:
		if (lv_fs_open(&f, src, LV_FS_MODE_RD) != LV_FS_RES_OK)
			goto free_ctx;

		if (spng_set_png_stream(ctx, lv_spng_read, &f) != 0)
			goto close;
		break;

	case LV_IMAGE_SRC_VARIABLE:
		idsc = src;
		if (spng_set_png_buffer(ctx, idsc->data, idsc->data_size) != 0)
			goto free_ctx;
		break;

	default:
		/* pretty unlikely... */
		goto free_ctx;
	}

	if (spng_get_ihdr(ctx, &ihdr) != 0)
		goto close;

	header->w = ihdr.width;
	header->h = ihdr.height;

	switch (ihdr.color_type) {
	case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
	case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
		header->cf = LV_COLOR_FORMAT_ARGB8888;
		break;
	default:
		header->cf = LV_COLOR_FORMAT_RGB888;
		break;
	}
	res = LV_RESULT_OK;

close:
	if (stype == LV_IMAGE_SRC_FILE)
		lv_fs_close(&f);
free_ctx:
	spng_ctx_free(ctx);
	return (res);
}

static lv_res_t
lv_spng_open(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
	uint8_t *img_data = NULL;
	spng_ctx *ctx;
	struct spng_ihdr ihdr;
	lv_fs_file_t f;
	const lv_image_dsc_t *idsc;
	lv_res_t res = LV_RESULT_INVALID;
	size_t img_size, i;
	int fmt = SPNG_FMT_RGBA8;

	ctx = spng_ctx_new(0);
	if (ctx == NULL)
		return (LV_RESULT_INVALID);

	switch (dsc->src_type) {
	case LV_IMAGE_SRC_FILE:
		if (lv_fs_open(&f, dsc->src, LV_FS_MODE_RD) != LV_FS_RES_OK)
			goto free_ctx;

		if (spng_set_png_stream(ctx, lv_spng_read, &f) != 0)
			goto close;
		break;

	case LV_IMAGE_SRC_VARIABLE:
		idsc = dsc->src;
		if (spng_set_png_buffer(ctx, idsc->data, idsc->data_size) != 0)
			goto free_ctx;
		break;

	default:
		goto free_ctx;
	}

	if (spng_decoded_image_size(ctx, fmt, &img_size) != 0)
		goto close;

	img_data = lv_malloc(img_size);
	if (img_data == NULL)
		goto close;

	if (spng_decode_image(ctx, img_data, img_size, fmt, 0) != 0) {
		lv_free(img_data);
		goto close;
	}

	/* sigh */
	for (i = 0; i < img_size; i += 4) {
		uint8_t *px = img_data + i;
		lv_color32_t c;

		c.ch.red = px[0];
		c.ch.green = px[1];
		c.ch.blue = px[2];
		c.ch.alpha = px[3];

		*(lv_color32_t *)px = c;
	}

	dsc->img_data = img_data;
	res = LV_RESULT_OK;

close:
	if (dsc->src_type == LV_IMAGE_SRC_FILE)
		lv_fs_close(&f);
free_ctx:
	spng_ctx_free(ctx);
	return (res);
}

static void
lv_spng_close(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
	if (dsc->img_data != NULL) {
		lv_free((void *)dsc->img_data);
		dsc->img_data = NULL;
	}
}

int
lv_spng_init(void)
{
	lv_image_decoder_t *dec = lv_image_decoder_create();
	if (dec == NULL)
		return (-1);

	lv_image_decoder_set_info_cb(dec, lv_spng_info);
	lv_image_decoder_set_open_cb(dec, lv_spng_open);
	lv_image_decoder_set_close_cb(dec, lv_spng_close);

	return (0);
}
