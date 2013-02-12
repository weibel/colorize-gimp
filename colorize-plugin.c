/* copyright 2005,2007 Christopher Lais */
/* GPLv2 or later. */

#include <unistd.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "colorize.h"

#define PLUG_IN_VERSION	"2007-09-30 153656"

static colorize_vals_t colorizevals = {
	.marked_id = -1,
	.use_mask = TRUE,
	.thresh = 0.5,
	.marked_includes_original = FALSE,
	.white_mask = FALSE,
	.use_chroma = FALSE,
	.entire_image = TRUE,
};

static gint dialog_marked_constrain(gint32 image_id, gint32 drawable_id, gpointer data)
{
	gboolean *has_guide_layer = (gboolean*)data;
	if (gimp_drawable_is_rgb(drawable_id) && gimp_drawable_has_alpha(drawable_id)) {
		*has_guide_layer = TRUE;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void dialog_marked_cb(GtkWidget *widget, gpointer data)
{
	GimpDrawable **drawable = data;
	gint32 marked_id;

	gimp_int_combo_box_get_active(GIMP_INT_COMBO_BOX(widget), &marked_id);

	if (marked_id != colorizevals.marked_id) {
		gimp_drawable_detach(*drawable);
		colorizevals.marked_id = marked_id;
		*drawable = gimp_drawable_get(marked_id);
	}
}

static void add_toggle_button(GtkWidget *box, const gchar *text, gint32 *val)
{
	GtkWidget *button;

	button = gtk_check_button_new_with_mnemonic(text);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *val);
	g_signal_connect(
		button, "toggled",
		G_CALLBACK(gimp_toggle_button_update),
		val
	);
	gtk_box_pack_start_defaults(GTK_BOX(box), button);
}

static gboolean colorize_dialog(GimpDrawable *default_drawable)
{
	GtkWidget *dialog, *combo;
	GimpDrawable *marked;
	gint status;
	gboolean has_guide_layer = FALSE;

	gimp_ui_init("colorize", TRUE);

	dialog = gimp_dialog_new(
		"Colorize", "colorize",
		NULL, 0,
		gimp_standard_help_func, "plug-in-colorize",
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		NULL
	);

	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)), 12);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 6);

	if (colorizevals.marked_id == -1)
		colorizevals.marked_id = default_drawable->drawable_id;
	marked = gimp_drawable_get(colorizevals.marked_id);
	if (!marked) {
		return FALSE;
	}

	/**/
	combo = gimp_drawable_combo_box_new(dialog_marked_constrain, &has_guide_layer);
	if (!has_guide_layer) {
		g_message("Color markers must be placed on an new RGB layer with an alpha channel.\n");
		return 0;
	}
	gimp_int_combo_box_connect(
		GIMP_INT_COMBO_BOX(combo), colorizevals.marked_id,
		G_CALLBACK(dialog_marked_cb), &marked
	);
	gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), combo);

	/**/
	add_toggle_button(
		GTK_DIALOG(dialog)->vbox,
		"Marked image includes original image",
		&colorizevals.marked_includes_original
	);

	/**/
	add_toggle_button(
		GTK_DIALOG(dialog)->vbox,
		"Unselected areas are mask",
		&colorizevals.use_mask
	);

	/**/
	add_toggle_button(
		GTK_DIALOG(dialog)->vbox,
		"Pure white is mask",
		&colorizevals.white_mask
	);

	/**/
	add_toggle_button(
		GTK_DIALOG(dialog)->vbox,
		"Use chroma in addition to luminance (for color images)",
		&colorizevals.use_chroma
	);

	/**/
	add_toggle_button(
		GTK_DIALOG(dialog)->vbox,
		"Use the entire image, even when the selected area is smaller",
		&colorizevals.entire_image
	);

	/**/

	gtk_widget_show_all(dialog);

	status = (gimp_dialog_run(GIMP_DIALOG(dialog)) == GTK_RESPONSE_OK);

	gtk_widget_destroy(dialog);

	gimp_drawable_detach(marked);

	return status;
}

static void query(void)
{
	static const GimpParamDef args[] = {
		{ GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
		{ GIMP_PDB_IMAGE, "image", "Input image" },
		{ GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
		{ GIMP_PDB_DRAWABLE, "marked", "Marking drawable" },
		{ GIMP_PDB_INT32, "use_mask", "Enable to mask unselected areas" },
		{ GIMP_PDB_FLOAT, "thresh", "Alpha threshold for markings" },
		{ GIMP_PDB_INT32, "mio", "Marked image includes the pixels from the original image" },
		{ GIMP_PDB_INT32, "white_mask", "Enable to mask pure white areas" },
		{ GIMP_PDB_INT32, "use_chroma", "Use chroma in addition to luminance" },
		{ GIMP_PDB_INT32, "entire_image", "Use the entire image, even when the selected area is smaller" },
	};

	gimp_install_procedure(
		"plug_in_colorize",
		"Re-color images using optimization techniques.",
		"This plug-in uses the algorithm described by Anat Levin, "
		"Dani Lischinski, and Yair Weiss.  Full information is "
		"available at http://www.cs.huji.ac.il/~yweiss/Colorization/ "
		"or http://www.cs.huji.ac.il/~yweiss/Colorization/colorization-siggraph04.pdf",
		"Christopher Lais",
		"Christopher Lais",
		PLUG_IN_VERSION,
		"Colorization...",
		"RGB*",
		GIMP_PLUGIN,
		G_N_ELEMENTS(args), 0,
		args, NULL
	);
	
	gimp_plugin_menu_register("plug_in_colorize", "<Image>/Colors");
}

static void run(
	const gchar *name, gint nparams, const GimpParam *param,
	gint *nreturn_vals, GimpParam **return_vals
)
{
	static GimpParam values[1];

	GimpPDBStatusType status;
	GimpDrawable *drawable;

	status = GIMP_PDB_SUCCESS;

	values[0].type = GIMP_PDB_STATUS;
	values[0].data.d_status = status;

	*nreturn_vals = 1;
	*return_vals = values;

	drawable = gimp_drawable_get(param[2].data.d_int32);
	if (!drawable) {
		values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;
		return;
	}

	if (!gimp_drawable_is_rgb(drawable->drawable_id)) {
		status = GIMP_PDB_CALLING_ERROR;
	} else
	switch (param[0].data.d_int32) {
	  case GIMP_RUN_INTERACTIVE:
		gimp_get_data(name, &colorizevals);
		if (!colorize_dialog(drawable)) {
			status = GIMP_PDB_EXECUTION_ERROR;
			break;
		}
		break;

	  case GIMP_RUN_NONINTERACTIVE:
		if (nparams <= 3) {
			status = GIMP_PDB_CALLING_ERROR;
			break;
		}

		colorizevals.marked_id = param[3].data.d_drawable;

		if (nparams > 4)
			colorizevals.use_mask = param[4].data.d_int32;
		else
			colorizevals.use_mask = TRUE;

		if (nparams > 5)
			colorizevals.thresh = param[5].data.d_float;
		else
			colorizevals.thresh = 0.5;

		if (nparams > 6)
			colorizevals.marked_includes_original = param[6].data.d_int32;
		else
			colorizevals.marked_includes_original = FALSE;

		break;
	  case GIMP_RUN_WITH_LAST_VALS:
		gimp_get_data(name, &colorizevals);
		break;

	  default:
		status = GIMP_PDB_CALLING_ERROR;
		break;
	}

	if (status != GIMP_PDB_SUCCESS) {
		values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;
	} else {
		GimpDrawable *marked;
		marked = gimp_drawable_get(colorizevals.marked_id);
		if (marked) {
			colorize(
				drawable,
				marked,
				&colorizevals
			);
			gimp_drawable_detach(marked);
		} else {
			status = values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;
		}

		if (param[0].data.d_int32 == GIMP_RUN_INTERACTIVE) {
			gimp_set_data(name, &colorizevals, sizeof(colorizevals));
		}
	}

	gimp_drawable_detach(drawable);
	gimp_displays_flush();
}

const GimpPlugInInfo PLUG_IN_INFO = { .query_proc = query, .run_proc = run };
MAIN();