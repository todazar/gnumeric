/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-data-slicer.c:  Edit DataSlicers
 *
 * (c) Copyright 2008-2009 Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook-cmd-format.h>
#include <gnm-sheet-slicer.h>
#include <go-data-slicer.h>
#include <go-data-slicer-field.h>

#include <glade/glade.h>

typedef struct {
	GtkWidget	*dialog;
	GladeXML	*gui;
	WBCGtk		*wbcg;
	SheetView	*sv;
	GnmSheetSlicer	*slicer;

	GtkTreeView	*treeview;
	GtkTreeSelection	*selection;
} DialogDataSlicer;

enum {
	FIELD,
	FIELD_TYPE,
	FIELD_NAME,
	FIELD_HEADER_INDEX,
	NUM_COLUMNS
};
#define DIALOG_KEY "dialog-data-slicer"

static void
cb_dialog_data_slicer_destroy (DialogDataSlicer *state)
{
	if (NULL != state->gui) { g_object_unref (G_OBJECT (state->gui)); state->gui = NULL; }
	if (NULL != state->slicer) { g_object_unref (G_OBJECT (state->slicer)); state->slicer = NULL; }
	state->dialog = NULL;
	g_free (state);
}

static void
cb_dialog_data_slicer_ok (G_GNUC_UNUSED GtkWidget *button,
			  DialogDataSlicer *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_data_slicer_cancel (G_GNUC_UNUSED GtkWidget *button,
			      DialogDataSlicer *state)
{
	gtk_widget_destroy (state->dialog);
}

static gint
cb_sort_by_header_index (GtkTreeModel *model,
			 GtkTreeIter  *a,
			 GtkTreeIter  *b,
			 gpointer      user_data)
{
#if 0
	GtkTreeIter child_a, child_b;
	GtkRecentInfo *info_a, *info_b;
	gboolean is_folder_a, is_folder_b;

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &child_a, a);
	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &child_b, b);

	gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), &child_a,
			    RECENT_MODEL_COL_IS_FOLDER, &is_folder_a,
			    RECENT_MODEL_COL_INFO, &info_a,
			    -1);
	gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), &child_b,
			    RECENT_MODEL_COL_IS_FOLDER, &is_folder_b,
			    RECENT_MODEL_COL_INFO, &info_b,
			    -1);

	if (!info_a)
		return 1;

	if (!info_b)
		return -1;

	/* folders always go first */
	if (is_folder_a != is_folder_b)
		return is_folder_a ? 1 : -1;

	if (gtk_recent_info_get_modified (info_a) < gtk_recent_info_get_modified (info_b))
		return -1;
	else if (gtk_recent_info_get_modified (info_a) > gtk_recent_info_get_modified (info_b))
		return 1;
	else
#endif
		return 0;
}

static void
cb_dialog_data_slicer_create_model (DialogDataSlicer *state)
{
	struct {
		GODataSlicerFieldType	type;
		char const *		type_name;
		GtkTreeIter iter;
	} field_type_labels[] = {
		{ GDS_FIELD_TYPE_PAGE,	N_("Filter") },
		{ GDS_FIELD_TYPE_ROW,	N_("Row") },
		{ GDS_FIELD_TYPE_COL,	N_("Column") },
		{ GDS_FIELD_TYPE_DATA,	N_("Data") },
		{ GDS_FIELD_TYPE_UNSET,	N_("Unused") }
	};

	unsigned int	 i, j, n;
	GtkTreeStore	*model;
	GtkTreeModel	*smodel;

	model = gtk_tree_store_new (NUM_COLUMNS,
				    G_TYPE_POINTER,	/* field */
				    G_TYPE_INT,		/* field-type */
				    G_TYPE_STRING,	/* field-name */
				    G_TYPE_INT);		/* field-header-index */
	smodel = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (model));
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (smodel),
		FIELD_HEADER_INDEX, cb_sort_by_header_index, NULL, NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (smodel),
		FIELD_HEADER_INDEX, GTK_SORT_ASCENDING);

	for (i = 0 ; i < G_N_ELEMENTS (field_type_labels) ; i++) {
		gtk_tree_store_append (model, &field_type_labels[i].iter, NULL);
		gtk_tree_store_set (model, &field_type_labels[i].iter,
			FIELD,			NULL,
			FIELD_TYPE,		field_type_labels[i].type,
			FIELD_NAME,		_(field_type_labels[i].type_name),
			FIELD_HEADER_INDEX,	-1,
			-1);
	}
	n = go_data_slicer_num_fields (GO_DATA_SLICER (state->slicer));
	for (i = 0 ; i < n ; i++) {
		GtkTreeIter child_iter;
		unsigned int field_types;
		int header_indx;
		GODataSlicerField *field =
			go_data_slicer_get_field (GO_DATA_SLICER (state->slicer), i);
		GOString *name = go_data_slicer_field_get_name (field);

		g_object_get (field, "field-types", &field_types, "header-index", &header_indx, NULL);

		for (j = 0 ; j < G_N_ELEMENTS (field_type_labels) ; j++)
		       	if ((field_types & (1 << field_type_labels[j].type))) {
				gtk_tree_store_append (model, &child_iter, &field_type_labels[j].iter);
				gtk_tree_store_set (model, &child_iter,
					FIELD,			NULL,
					FIELD_TYPE,		field_type_labels[j].type,
					FIELD_NAME,		name->str,
					FIELD_HEADER_INDEX,	header_indx,
					-1);
			}
	}
	gtk_tree_view_set_model (state->treeview, smodel);
}

static void
cb_dialog_data_slicer_selection_changed (GtkTreeSelection *selection,
					 DialogDataSlicer *state)
{
}

void
dialog_data_slicer (WBCGtk *wbcg, gboolean create)
{
	static GtkTargetEntry row_targets[] = {
		{ (char *)"GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, 0 }
	};
	DialogDataSlicer *state;
	GladeXML *gui;
	GtkWidget *w;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg), "data-slicer.glade", NULL, NULL);
	if (NULL == gui)
		return;

	state = g_new (DialogDataSlicer, 1);
	state->wbcg	= wbcg;
	state->sv	= wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->gui	= gui;
	state->slicer	= sv_editpos_in_slicer (state->sv);
	if (NULL == state->slicer) {
	} else
		g_object_ref (G_OBJECT (state->slicer));

	state->dialog = glade_xml_get_widget (state->gui, "dialog_data_slicer");

	w = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_dialog_data_slicer_ok), state);
	w = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w), "clicked",
		G_CALLBACK (cb_dialog_data_slicer_cancel), state);

	state->treeview = GTK_TREE_VIEW (glade_xml_get_widget (state->gui, "field_tree"));
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (state->treeview), GDK_BUTTON1_MASK,
		row_targets, G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
	gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (state->treeview),
		row_targets, G_N_ELEMENTS (row_targets), GDK_ACTION_MOVE);
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_SINGLE);
	g_signal_connect (state->selection, "changed",
		G_CALLBACK (cb_dialog_data_slicer_selection_changed), state);

	gtk_tree_view_append_column (state->treeview,
		gtk_tree_view_column_new_with_attributes ("",
			gtk_cell_renderer_text_new (), "text", FIELD_NAME, NULL));
	cb_dialog_data_slicer_create_model (state);

	g_signal_connect (state->treeview, "realize", G_CALLBACK (gtk_tree_view_expand_all), NULL);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_DATA_SLICER);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_dialog_data_slicer_destroy);
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), DIALOG_KEY);
	gtk_widget_show (state->dialog);
}
