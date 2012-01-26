#ifndef COLORIZE_H
#define COLORIZE_H 1

typedef struct {
	gint32 marked_id;
	gint32 use_mask;
	gdouble thresh;
	gint32 marked_includes_original;
	gint32 white_mask;
	gint32 use_chroma;
	gint32 entire_image;
} colorize_vals_t;

void colorize(
	GimpDrawable *image,
	GimpDrawable *marked,
	const colorize_vals_t *vals
);

#endif
