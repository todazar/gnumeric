/*
 * sstest.c: Test code for Gnumeric
 *
 * Copyright (C) 2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include <goffice/app/go-plugin.h>
#include "command-context-stderr.h"
#include <goffice/app/io-context.h>
#include <goffice/app/error-info.h>
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "parse-util.h"
#include "expr-name.h"
#include "search.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "str.h"
#include "func.h"
#include "parse-util.h"
#include "sheet-object-cell-comment.h"

#include <goffice/utils/go-file.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/app/go-cmd-context.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean sstest_show_version = FALSE;

static GOptionEntry const sstest_options [] = {
	{
		"version", 'V',
		0, G_OPTION_ARG_NONE, &sstest_show_version,
		N_("Display program version"),
		NULL
	},

	{ NULL }
};

static void
mark_test_start (const char *name)
{
	g_printerr ("-----------------------------------------------------------------------------\nStart: %s\n-----------------------------------------------------------------------------\n\n", name);
}

static void
mark_test_end (const char *name)
{
	g_printerr ("End: %s\n\n", name);
}

static void
cb_collect_names (const char *name, GnmNamedExpr *nexpr, GSList **names)
{
	*names = g_slist_prepend (*names, nexpr);
}

static void
dump_names (Workbook *wb)
{
	GSList *l, *names = NULL;

	workbook_foreach_name (wb, FALSE, (GHFunc)cb_collect_names, &names);
	names = g_slist_sort (names, (GCompareFunc)expr_name_cmp_by_name);
	
	g_printerr ("Dumping names...\n");
	for (l = names; l; l = l->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmConventionsOut out;

		out.accum = g_string_new (NULL);
		out.pp = &nexpr->pos;
		out.convs = gnm_conventions_default;

		g_string_append (out.accum, "Scope=");
		if (out.pp->sheet)
			g_string_append (out.accum, out.pp->sheet->name_quoted);
		else
			g_string_append (out.accum, "Global");

		g_string_append (out.accum, " Name=");
		go_strescape (out.accum, expr_name_name (nexpr));

		g_string_append (out.accum, " Expr=");
		gnm_expr_top_as_gstring (nexpr->texpr, &out);

		g_printerr ("%s\n", out.accum->str);
		g_string_free (out.accum, TRUE);
	}
	g_printerr ("Dumping names... Done\n");

	g_slist_free (names);
}

static void
define_name (const char *name, const char *expr_txt, gpointer scope)
{
	GnmParsePos pos;
	GnmExprTop const *texpr;
	GnmNamedExpr const *nexpr;

	if (IS_SHEET (scope)) {
		parse_pos_init_sheet (&pos, scope);
	} else {
		parse_pos_init (&pos, WORKBOOK (scope), NULL, 0, 0);
	}

	texpr = gnm_expr_parse_str_simple (expr_txt, &pos);
	if (!texpr) {
		g_printerr ("Failed to parse %s for name %s\n",
			    expr_txt, name);
		return;
	}

	nexpr = expr_name_add (&pos, name, texpr, NULL, TRUE, NULL);
	if (!nexpr)
		g_printerr ("Failed to add name %s\n", name);
}

static void
test_insdel_rowcol_names (void)
{
	Workbook *wb;
	Sheet *sheet1,*sheet2;
	const char *test_name = "test_insdel_rowcol_names";
	GOUndo *undo;
	int i;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	sheet2 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

	define_name ("NAMEGA1", "A1", wb);
	define_name ("NAMEG2", "$A$14+Sheet1!$A$14+Sheet2!$A$14", wb);

	define_name ("NAMEA1", "A1", sheet1);
	define_name ("NAMEA2", "A2", sheet1);
	define_name ("NAMEA1ABS", "$A$1", sheet1);
	define_name ("NAMEA2ABS", "$A$2", sheet1);

	dump_names (wb);

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet1->name_unquoted);
		sheet_insert_cols (sheet1, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet2->name_unquoted);
		sheet_insert_cols (sheet2, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to delete column %d on %s\n",
			    i, sheet1->name_unquoted);
		sheet_delete_cols (sheet1, i, 1, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	g_object_unref (wb);

	mark_test_end (test_name);
}

#define MAYBE_DO(name) if (strcmp (testname, "all") != 0 && strcmp (testname, (name)) != 0) { } else

int
main (int argc, char const **argv)
{
	ErrorInfo	*plugin_errs;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	const char *testname;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("[testname]"));
	g_option_context_add_main_entries (ocontext, sstest_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, g_get_prgname ());
		g_error_free (error);
		return 1;
	}

	if (sstest_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	} 

	gnm_init (FALSE);

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		error_info_free (plugin_errs);
	}

	testname = argv[1];
	if (!testname) testname = "all";

	/* ---------------------------------------- */

	MAYBE_DO ("test_insdel_rowcol_names") test_insdel_rowcol_names ();

	/* ---------------------------------------- */

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}