/* copyright 2005,2007 Christopher Lais */
/* GPLv2 or later. */
/* Yes, it's ugly.  I'm lazy. */

#include <unistd.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <umfpack.h>

#include "colorize.h"

#define min(a,b) ({__typeof(a) __a=(a),__b=(b); __a<__b?__a:__b;})
#define max(a,b) ({__typeof(a) __a=(a),__b=(b); __a>__b?__a:__b;})

#define WINDOW_RADIUS	1
#define WINDOW_WIDTH	(2*WINDOW_RADIUS + 1)
#define WINDOW_PIXELS	(WINDOW_WIDTH*WINDOW_WIDTH)

static void rgb2yiq(
	guchar r, guchar g, guchar b,
	gdouble *y, gdouble *i, gdouble *q
) {
#if YUV
	*y = (0.299*r + 0.587*g + 0.114*b) / (gdouble)255;
	*i = (0.147*r - 0.289*g + 0.436*b) / (gdouble)255;
	*q = (0.615*r - 0.515*g - 0.100*b) / (gdouble)255;
#else
	*y = (0.299*r + 0.587*g + 0.114*b) / (gdouble)255;
	*i = (0.596*r - 0.274*g - 0.322*b) / (gdouble)255;
	*q = (0.212*r - 0.523*g + 0.311*b) / (gdouble)255;
#endif
}

static void rgb2iq(
	guchar r, guchar g, guchar b,
	gdouble *i, gdouble *q
) {
#if YUV
	*i = (0.147*r - 0.289*g + 0.436*b) / (gdouble)255;
	*q = (0.615*r - 0.515*g - 0.100*b) / (gdouble)255;
#else
	*i = (0.596*r - 0.274*g - 0.322*b) / (gdouble)255;
	*q = (0.212*r - 0.523*g + 0.311*b) / (gdouble)255;
#endif
}

static void yiq2rgb(
	gdouble y, gdouble i, gdouble q,
	guchar *r, guchar *g, guchar *b
) {
	gdouble dr, dg, db;
#if YUV
	dr = (y + 1.140*q);
	dg = (y - 0.395*i - 0.581*q);
	db = (y + 2.032*i);
#else
	dr = (y + 0.956*i + 0.621*q);
	dg = (y - 0.272*i - 0.647*q);
	db = (y - 1.105*i + 1.702*q);
#endif
	dr = min(1.0, max(0.0, dr));
	dg = min(1.0, max(0.0, dg));
	db = min(1.0, max(0.0, db));
	*r = 255 * dr;
	*g = 255 * dg;
	*b = 255 * db;
}

#define LN_100	4.60517018598809136804

void colorize(
	GimpDrawable *image,
	GimpDrawable *marked,
	const colorize_vals_t *vals
)
{
	GimpDrawable *sel = NULL;

	int w, h;
	int i, j, ii, jj, n;
	gboolean has_sel;
	guchar thresh_guc = vals->thresh * 255;

	guchar *rgb;
	double *Y, *I, *Q;
	double *inI = NULL; // both only used if vals->use_chroma
	double *inQ = NULL;
	unsigned char *mask;
	double *outI, *outQ;

	int *Ap, *Ai;
	double *A;

	int *AI, *AJ, *Map;
	double *Ax;

	void *symbolic, *numeric;

	double control[UMFPACK_CONTROL];
	double info[UMFPACK_INFO];

	guchar *img_row, *mark_row;
	guchar *sel_row = NULL; // only used if sel

	GimpPixelRgn src_rgn, dst_rgn, mark_rgn, sel_rgn;

	gimp_progress_init("Colorizing...");

	has_sel = gimp_drawable_mask_intersect(image->drawable_id, &j, &i, &jj, &ii);
	if (!has_sel || vals->entire_image) {
		j = i = 0;
		jj = image->width;
		ii = image->height;
	}

	if (jj > image->width) jj = image->width;
	if (ii > image->height) ii = image->height;

	if (has_sel) {
		sel = gimp_drawable_get(gimp_image_get_selection(gimp_drawable_get_image(image->drawable_id)));

		if (sel) gimp_pixel_rgn_init(
			&sel_rgn,
			sel,
			j, i, jj, ii,
			FALSE, FALSE
		);
	}

	gimp_pixel_rgn_init(
		&src_rgn,
		image,
		j, i, jj, ii,
		FALSE, FALSE
	);

	gimp_pixel_rgn_init(
		&dst_rgn,
		image,
		j, i, jj, ii,
		TRUE, TRUE
	);

	gimp_pixel_rgn_init(
		&mark_rgn,
		marked,
		j, i, jj, ii,
		FALSE, FALSE
	);

#define M_ALLOC(type,i,j) malloc((i)*(j)*sizeof(type))
#define V_ALLOC(type,d) malloc((d)*sizeof(type))
#define M_IDX(i,j)	((i)*w+(j))
#define M_V(m,i,j)	m[M_IDX((i),(j))]
#define V_V(v,d)	v[d]

	h = src_rgn.h;
	w = src_rgn.w;

	A = M_ALLOC(*A, WINDOW_PIXELS, h*w);
	AI = V_ALLOC(*AI, WINDOW_PIXELS*h*w);
	AJ = V_ALLOC(*AJ, WINDOW_PIXELS*h*w);

	Y = M_ALLOC(*Y, h, w);
	I = M_ALLOC(*I, h, w);
	Q = M_ALLOC(*Q, h, w);
	if (vals->use_chroma) {
		inI = M_ALLOC(*inI, h, w);
		inQ = M_ALLOC(*inQ, h, w);
	}

	mask = M_ALLOC(*mask, h, w);

	if (sel) {
		sel_row = V_ALLOC(*rgb, w*sel_rgn.bpp);

		/* Retarded check for selections, because gimp doesn't
		   _REALLY_ return FALSE when there's no selection. */
		if (j == 0 && i == 0 && jj == image->width && ii == image->height) {
			for (i = 0; i < h; i++) {
				gimp_pixel_rgn_get_row(&sel_rgn, sel_row, sel_rgn.x, sel_rgn.y + i, w);
				for (j = 0; j < w; j++) {
					int sel_idx = j*sel_rgn.bpp;
					if (sel_row[sel_idx]) goto good_selection;
				}
			}

			/* Nothing set in the entire selection. */
			gimp_drawable_detach(sel);
			sel = NULL;
		}
	}
	good_selection:

	img_row = V_ALLOC(*rgb, w*src_rgn.bpp);
	mark_row = V_ALLOC(*rgb, w*mark_rgn.bpp);
	for (i = 0; i < h; i++) {
		gimp_pixel_rgn_get_row(&src_rgn, img_row, src_rgn.x, src_rgn.y + i, w);
		gimp_pixel_rgn_get_row(&mark_rgn, mark_row, mark_rgn.x, mark_rgn.y + i, w);
		if (sel) gimp_pixel_rgn_get_row(&sel_rgn, sel_row, sel_rgn.x, sel_rgn.y + i, w);

		for (j = 0; j < w; j++) {
			int img_idx = j*src_rgn.bpp;
			int mark_idx = j*mark_rgn.bpp;
			int sel_idx = j*sel_rgn.bpp;

			gdouble iY, iI, iQ;
			
			gint delta = 0;

			rgb2yiq(
				img_row[img_idx],
				img_row[img_idx+1],
				img_row[img_idx+2],
				&iY, &iI, &iQ
			);

			if (vals->use_chroma) {
				M_V(inI, i, j) = iI;
				M_V(inQ, i, j) = iQ;
			}

			if (vals->marked_includes_original) {
				gint v;
				v = img_row[img_idx] - mark_row[mark_idx];
				delta += abs(v);
				v = img_row[img_idx+1] - mark_row[mark_idx+1];
				delta += abs(v);
				v = img_row[img_idx+2] - mark_row[mark_idx+2];
				delta += abs(v);
			}

			/* big dirty if statement */
			if (vals->white_mask
				&& mark_row[mark_idx] >= 255
				&& mark_row[mark_idx+1] >= 255
				&& mark_row[mark_idx+2] >= 255
			) {
				M_V(mask, i, j) = TRUE;
			} else if ((vals->marked_includes_original &&
				(img_row[img_idx] != mark_row[mark_idx]
				|| img_row[img_idx+1] != mark_row[mark_idx+1]
				|| img_row[img_idx+2] != mark_row[mark_idx+2]))
			 || (!vals->marked_includes_original
				&& mark_row[mark_idx+3] >= thresh_guc)
			) {
				M_V(mask, i, j) = TRUE;
				rgb2iq(
					mark_row[mark_idx],
					mark_row[mark_idx+1],
					mark_row[mark_idx+2],
					&iI, &iQ
				);
			} else if (sel && sel_row[sel_idx] < thresh_guc) {
				M_V(mask, i, j) = TRUE;
			} else {
				M_V(mask, i, j) = FALSE;
				iI = iQ = 0;
			}

			M_V(Y, i, j) = iY;
			M_V(I, i, j) = iI;
			M_V(Q, i, j) = iQ;
		}
	}
	free(img_row);
	free(mark_row);
	if (sel) {
		free(sel_row);
		gimp_drawable_detach(sel);
	}

	gimp_progress_update(0.1);

	n = 0;
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			if (!M_V(mask, i, j)) {
				double sum_sq, sum;
				double min_variance;
				double sigma;

				int min_ii = max(0, i - WINDOW_RADIUS);
				int max_ii = min(h-1, i + WINDOW_RADIUS);
				int min_jj = max(0, j - WINDOW_RADIUS);
				int max_jj = min(w-1, j + WINDOW_RADIUS);
				int vary[WINDOW_PIXELS], varx[WINDOW_PIXELS];
				double var[WINDOW_PIXELS];
				int count;

				count = 0;
				sum_sq = sum = 0;
				min_variance = 1.0;
				for (ii = min_ii; ii <= max_ii; ii++) {
					for (jj = min_jj; jj <= max_jj; jj++) {
						double val = M_V(Y, ii, jj);
						sum += val;
						sum_sq += val*val;

						if (ii == i && jj == j) continue;

						vary[count] = M_IDX(i, j);
						varx[count] = M_IDX(ii, jj);
						var[count] = val - M_V(Y, i, j);
						var[count] *= var[count];
						if (vals->use_chroma) {
							val = M_V(inI, ii, jj) - M_V(inI, i, j);
							var[count] += val*val;
							val = M_V(inQ, ii, jj) - M_V(inQ, i, j);
							var[count] += val*val;
						}
						if (var[count] < min_variance) min_variance = var[count];
						++count;
					}
				}

				sigma = (sum_sq - (sum*sum)/(double)(count+1))/(double)count;
				if (sigma < 0.000002) sigma = 0.000002;
				else if (sigma < (min_variance / LN_100))
					sigma = min_variance / LN_100;

				sum = 0;
				for (ii = 0; ii < count; ii++) {
					var[ii] = exp(-var[ii] / sigma);
					sum += var[ii];
				}
				for (ii = 0; ii < count; ii++) {
					AI[n] = vary[ii];
					AJ[n] = varx[ii];
					A[n] = -var[ii] / sum;
					++n;
				}
			}

			AI[n] = AJ[n] = M_IDX(i, j);
			A[n] = 1.0;
			++n;
		}
	}

	free(mask);

	if (vals->use_chroma) {
		free(inI);
		free(inQ);
	}

	umfpack_di_defaults(control);

	Ax = M_ALLOC(*Ax, WINDOW_PIXELS, h*w);
	Ap = V_ALLOC(*Ap, h*w);
	Ai = V_ALLOC(*Ai, WINDOW_PIXELS*h*w);
	Map = V_ALLOC(*Map, WINDOW_PIXELS*h*w);

	umfpack_di_triplet_to_col(h*w, h*w, n, AI, AJ, A, Ap, Ai, Ax, Map);

	free(A); free(AI); free(AJ); free(Map);

	umfpack_di_symbolic(h*w, h*w, Ap, Ai, Ax, &symbolic, control, info);
	umfpack_di_numeric(Ap, Ai, Ax, symbolic, &numeric, control, info);

	umfpack_di_free_symbolic(&symbolic);

	gimp_progress_update(0.3);

	outI = M_ALLOC(*outI, h, w);
	outQ = M_ALLOC(*outQ, h, w);

	umfpack_di_solve(UMFPACK_A, Ap, Ai, Ax, outI, I, numeric, control, info);

	gimp_progress_update(0.6);

	umfpack_di_solve(UMFPACK_A, Ap, Ai, Ax, outQ, Q, numeric, control, info);

	umfpack_di_free_numeric(&numeric);

	gimp_progress_update(0.9);

	free(Ax); free(Ap); free(Ai);
	free(I); free(Q);

	img_row = V_ALLOC(*rgb, w*src_rgn.bpp);
	int img_idx;
	for (i = 0; i < h; i++) {
		/* FIXME: This is only for the alpha channel.. */
		gimp_pixel_rgn_get_row(&src_rgn, img_row, src_rgn.x, src_rgn.y + i, w);

		for (j = 0, img_idx = 0; j < w; j++, img_idx += src_rgn.bpp) {
			yiq2rgb(
				M_V(Y, i, j),
				M_V(outI, i, j),
				M_V(outQ, i, j),
				&img_row[img_idx],
				&img_row[img_idx+1],
				&img_row[img_idx+2]
			);
		}
		
		gimp_pixel_rgn_set_row(&dst_rgn, img_row, dst_rgn.x, dst_rgn.y + i, w);
	}
	free(img_row);

	free(Y); free(outI); free(outQ);

	gimp_drawable_flush(image);
	gimp_drawable_merge_shadow (image->drawable_id, TRUE);
	gimp_drawable_update(
		image->drawable_id,
		dst_rgn.x, dst_rgn.y,
		dst_rgn.w, dst_rgn.h
	);

	gimp_progress_update(1.0);
}