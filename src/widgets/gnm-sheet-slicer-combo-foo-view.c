/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * gnm-sheet-slicer-combo-foo-view.c: A foocanvas object for data slicer field combos
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include "gnm-sheet-slicer-combo-foo-view.h"
#include "gnm-cell-combo-foo-view-impl.h"

#include "gnm-sheet-slicer-combo.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-view.h"
#include "value.h"

#include "gui-gnumeric.h"
#include "go-data-slicer-field.h"
#include "go-data-cache-field.h"
#include <goffice/goffice.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-widget.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <string.h>

enum {
	SSCOMBO_COL_FILTERED,
	SSCOMBO_COL_NAME,
	SSCOMBO_COL_LAST
};

static gboolean
sscombo_activate (SheetObject *so, GtkWidget *popup, GtkTreeView *list,
		 WBCGtk *wbcg)
{
	GtkTreeIter	    iter;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (list), NULL, &iter)) {
		char		*strval;
		gtk_tree_model_get (gtk_tree_view_get_model (list), &iter,
			1, &strval,
			-1);
#if 0
	GnmSheetSlicerCombo *sscombo = GNM_SHEET_SLICER_COMBO (so);
		SheetView	*sv  = sscombo->sv;
		cmd_set_text (WORKBOOK_CONTROL (wbcg),
			sv_sheet (sv), &sv->edit_pos, strval, NULL);
#endif
		g_free (strval);
	}
	return FALSE;
}

static void
cb_filter_toggle (GtkCellRendererToggle *cell,
		  const gchar *path_str,
		  gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter  iter;
	GtkTreePath  *path = gtk_tree_path_new_from_string (path_str);
	if (gtk_tree_model_get_iter (model, &iter, path)) {
		gboolean val;
		gtk_tree_model_get (model, &iter, SSCOMBO_COL_FILTERED, &val, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, SSCOMBO_COL_FILTERED, val ^ 1, -1);
	}
	gtk_tree_path_free (path);
}

static GtkWidget *
sscombo_create_list (SheetObject *so, GtkTreePath **clip, GtkTreePath **select)
{
	GnmSheetSlicerCombo *sscombo = GNM_SHEET_SLICER_COMBO (so);
	GODataCacheField const *dcf  = go_data_slicer_field_get_cache_field (sscombo->dsf);
	GtkListStore	*model;
	GtkTreeIter	 iter;
	GtkCellRenderer *renderer;
	GtkWidget	*list;
	GOValArray const *vals;
	GString		*str;
	unsigned i;

	vals = go_data_cache_field_get_vals (dcf, TRUE);
	if (NULL == vals)
		vals = go_data_cache_field_get_vals (dcf, FALSE);
	g_return_val_if_fail (vals != NULL, NULL);

	model = gtk_list_store_new (SSCOMBO_COL_LAST,
		G_TYPE_BOOLEAN, G_TYPE_STRING);
	str = g_string_sized_new (20);
	for (i = 0; i < vals->len ; i++) {
		if (GO_FORMAT_NUMBER_OK == format_value_gstring (str, NULL,
			g_ptr_array_index (vals, i), NULL, -1,
			workbook_date_conv (sscombo->parent.sv->sheet->workbook))) {
			gtk_list_store_append (model, &iter);
			gtk_list_store_set (model, &iter, 0, TRUE, 1, str->str, -1);
			g_string_truncate (str, 0); 
		}
	}
	list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
	g_object_unref (model);

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer), "toggled",
		G_CALLBACK (cb_filter_toggle), model);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("filter",
			renderer, "active", SSCOMBO_COL_FILTERED,
			NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (list),
		gtk_tree_view_column_new_with_attributes ("ID",
			gtk_cell_renderer_text_new (), "text", SSCOMBO_COL_NAME,
			NULL));
	return list;
}

static GtkWidget *
sscombo_create_arrow (G_GNUC_UNUSED SheetObject *so)
{
	return gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
}

static void
sscombo_ccombo_init (GnmCComboFooViewIface *ccombo_iface)
{
	ccombo_iface->create_list	= sscombo_create_list;
	ccombo_iface->create_arrow	= sscombo_create_arrow;
	ccombo_iface->activate		= sscombo_activate;
}

/*******************************************************************************/

/* Somewhat magic.
 * We do not honour all of the anchor flags.  All that is used is the far corner. */
static void
sscombo_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		double h = (coords[3] - coords[1]) + 1.;
		if (h > 20.)	/* clip vertically */
			h = 20.;
		foo_canvas_item_set (view,
			/* put it outside the cell */
			"x",	  ((coords[2] >= 0.) ? coords[2] : (coords[0]-h+1.)),
			"y",	  coords [3] - h + 1.,
			"width",  h,	/* force a square, use h for width too */
			"height", h,
			NULL);
		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}
static void
sscombo_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
sscombo_sov_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= sscombo_destroy;
	sov_iface->set_bounds	= sscombo_set_bounds;
}

/****************************************************************************/

typedef FooCanvasWidget		GnmSheetSlicerComboFooView;
typedef FooCanvasWidgetClass	GnmSheetSlicerComboFooViewClass;
GSF_CLASS_FULL (GnmSheetSlicerComboFooView, gnm_sheet_slicer_combo_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, FOO_TYPE_CANVAS_WIDGET, 0,
	GSF_INTERFACE (sscombo_sov_init, SHEET_OBJECT_VIEW_TYPE)
	GSF_INTERFACE (sscombo_ccombo_init, GNM_CCOMBO_FOO_VIEW_TYPE)
)