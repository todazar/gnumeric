/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xlsx-utils.c : Utilities shared between xlsx import and export.
 *
 * Copyright (C) 2006-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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

/*****************************************************************************/

#include <gnumeric-config.h>
#include <gnumeric.h>

#include "xlsx-utils.h"

#include "parse-util.h"
#include "position.h"
#include "workbook.h"
#include "sheet.h"
#include "func.h"
#include <expr-impl.h>
#include "gnm-format.h"
#include <goffice/goffice.h>
#include <glib-object.h>
#include <string.h>
#include <expr.h>
#include <value.h>

typedef struct {
	GnmConventions base;
	GHashTable *extern_id_by_wb;
	GHashTable *extern_wb_by_id;
	GHashTable *xlfn_map;
	GHashTable *xlfn_handler_map;
} XLSXExprConventions;

static void
xlsx_add_extern_id (GnmConventionsOut *out, Workbook *wb)
{
	if (wb != out->pp->wb) {
		XLSXExprConventions const *xconv = (XLSXExprConventions const *)out->convs;
		char *id = g_hash_table_lookup (xconv->extern_id_by_wb, wb);
		if (NULL == id) {
			id = g_strdup_printf ("[%u]",
				g_hash_table_size (xconv->extern_id_by_wb));
			g_object_ref (wb);
			g_hash_table_insert (xconv->extern_id_by_wb, wb, id);
		}
		g_string_append (out->accum, id);
	}
}

static Workbook *
xlsx_lookup_external_wb (GnmConventions const *convs,
			 G_GNUC_UNUSED Workbook *ref_wb,
			 char const *name)
{
	XLSXExprConventions const *xconv = (XLSXExprConventions const *)convs;
	if (strcmp (name, "0") == 0)
		return ref_wb;
	if (0) g_printerr ("lookup '%s'\n", name);
	return g_hash_table_lookup (xconv->extern_wb_by_id, name);
}

static void
xlsx_cellref_as_string (GnmConventionsOut *out,
			GnmCellRef const *cell_ref,
			G_GNUC_UNUSED gboolean no_sheetname)
{
	Sheet const *sheet = cell_ref->sheet;

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL) {
		xlsx_add_extern_id (out, sheet->workbook);
		g_string_append (out->accum, sheet->name_quoted);
		g_string_append_c (out->accum, '!');
	}
	cellref_as_string (out, cell_ref, TRUE);
}

static void
xlsx_rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	if (ref->a.sheet) {
		GnmRangeRef local_ref = *ref;

		xlsx_add_extern_id (out, ref->a.sheet->workbook);

		local_ref.a.sheet = local_ref.b.sheet = NULL;
		g_string_append (out->accum, ref->a.sheet->name_quoted);
		if (ref->b.sheet != NULL && ref->a.sheet != ref->b.sheet) {
			g_string_append_c (out->accum, ':');
			g_string_append (out->accum, ref->b.sheet->name_quoted);
		}
		g_string_append_c (out->accum, '!');

		rangeref_as_string (out, &local_ref);
	} else
		rangeref_as_string (out, ref);
}

Workbook *
xlsx_conventions_add_extern_ref (GnmConventions *convs, char const *path)
{
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;
	Workbook *res = g_object_new (WORKBOOK_TYPE, NULL);
	(void) go_doc_set_uri (GO_DOC (res), path);
	g_hash_table_insert (xconv->extern_wb_by_id,
		g_strdup_printf ("%d", g_hash_table_size (xconv->extern_wb_by_id) + 1),
		res);
	if (0) g_printerr ("add %d = '%s'\n", g_hash_table_size (xconv->extern_wb_by_id), path);
	return res;
}

static GnmExpr const *
xlsx_func_map_in (GnmConventions const *convs, 
		  G_GNUC_UNUSED Workbook *scope,
		  char const *name, GnmExprList *args)
{
	XLSXExprConventions const *xconv = (XLSXExprConventions const *)convs;
	GnmExpr const * (*handler) (GnmConventions const *convs, Workbook *scope, 
				    GnmExprList *args);
	GnmFunc  *f;
	char const *new_name;
	
	if (0 == g_ascii_strncasecmp (name, "_xlfn.", 6)) {
		if (NULL != xconv->xlfn_map &&
		    NULL != (new_name = g_hash_table_lookup (xconv->xlfn_map, name + 6)))
			name = new_name;
		else
			name = name + 6;
		handler = g_hash_table_lookup (xconv->xlfn_handler_map, name);
		if (handler != NULL) {
			GnmExpr const * res = handler (convs, scope, args);
			if (res != NULL)
				return res;
		}
	} else if (0 == g_ascii_strncasecmp (name, "_xlfnodf.", 9))
		/* This should at most happen for ODF functions incorporated */
		/* in an xlsx file, we should perform the appropriate translation! */
		name = name + 9;
	else if (0 == g_ascii_strncasecmp (name, "_xlfngnumeric.", 9))
		/* These are Gnumeric's own functions */
		name = name + 14;

	f = gnm_func_lookup_or_add_placeholder (name);

	return gnm_expr_new_funcall (f, args);	
}

static void
xlsx_func_map_out (GnmConventionsOut *out, GnmExprFunction const *func)
{
	XLSXExprConventions const *xconv = (XLSXExprConventions const *)(out->convs);
	char const *name = gnm_func_get_name (func->func, FALSE);
	gboolean (*handler) (GnmConventionsOut *out, GnmExprFunction const *func);

	handler = g_hash_table_lookup (xconv->xlfn_handler_map, name);

	if (handler == NULL || !handler (out, func)) {
		char const *new_name = g_hash_table_lookup (xconv->xlfn_map, name);
		GString *target = out->accum;

		if (new_name == NULL) {
				char *new_u_name;
				new_u_name = g_ascii_strup (name, -1);
				if (func->func->impl_status == 
				    GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC)
					g_string_append (target, "_xlfngnumeric.");
				/* LO & friends use _xlfnodf */
				g_string_append (target, new_u_name);
				g_free (new_u_name);
		}
		else {
			g_string_append (target, "_xlfn.");
			g_string_append (target, new_name);
		}

		gnm_expr_list_as_string (func->argc, func->argv, out);
	}
	return;
}


static GnmExpr const *
xlsx_func_binominv_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, GnmExprList *args)
/* BINOM.INV(a,b,c) --> R.QBINOM(c,a,b) */
{
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("r.qbinom");
	GnmExprList *arg;
	
	arg = g_slist_nth (args, 2);
	args = g_slist_remove_link (args, arg);
	args = g_slist_concat (arg, args);

	return gnm_expr_new_funcall (f, args);
}

static GnmExpr const *
xlsx_func_dist_handler (GnmExprList *args, guint n_args, char const *name, char const *name_p, char const *name_d)
{
	if (gnm_expr_list_length (args) != n_args) {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder (name);
		return gnm_expr_new_funcall (f, args);
	} else {
		GnmFunc  *f_if = gnm_func_lookup_or_add_placeholder ("if");
		GnmFunc  *f_p = gnm_func_lookup_or_add_placeholder (name_p);
		GnmFunc  *f_d = gnm_func_lookup_or_add_placeholder (name_d);
		GnmExprList *arg_cum, *args_c;
		GnmExpr const *cum;
		GnmValue const *constant;

		arg_cum = g_slist_nth (args, n_args - 1);
		args = g_slist_remove_link (args, arg_cum);
		cum = arg_cum->data;
		gnm_expr_list_free (arg_cum);

		constant = gnm_expr_get_constant (cum);
		
		if (constant == NULL || !VALUE_IS_NUMBER (constant)) {
			args_c = gnm_expr_list_copy (args);
			
			return gnm_expr_new_funcall3 
				(f_if, cum,
				 gnm_expr_new_funcall (f_p, args),
				 gnm_expr_new_funcall (f_d, args_c));
			
		} else if (value_is_zero (constant)) {
			gnm_expr_free (cum);
			return gnm_expr_new_funcall (f_d, args);
		} else {
			gnm_expr_free (cum);
			return gnm_expr_new_funcall (f_p, args);
		}
	}
}

static GnmExpr const *
xlsx_func_chisqdist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, 
			     GnmExprList *args)
{
	return xlsx_func_dist_handler (args, 3, "chisq.dist", "r.pchisq", "r.dchisq");
}

static GnmExpr const *
xlsx_func_fdist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, 
			 GnmExprList *args)
{
	return xlsx_func_dist_handler (args, 4, "f.dist", "r.pf", "r.df");
}

static GnmExpr const *
xlsx_func_lognormdist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, 
			       GnmExprList *args)
{
	return xlsx_func_dist_handler (args, 4, "lognorm.dist", "r.plnorm", "r.dlnorm");
}

static GnmExpr const *
xlsx_func_negbinomdist_handler (G_GNUC_UNUSED GnmConventions const *convs, G_GNUC_UNUSED Workbook *scope, 
				GnmExprList *args)
{
	return xlsx_func_dist_handler (args, 4, "negbinom.dist", "r.pnbinom", "r.dnbinom");
}

static void
xlsx_write_r_q_func (GnmConventionsOut *out, char const *name, char const *name_rt, 
		     GnmExprConstPtr const *ptr, int n, int n_p,
		     gboolean use_lower_tail, gboolean use_log)
{
	/* R.Qx(a_0,...,a_n_p,...,a_n) --> name(a_0,...,mod(a_n_p),...a_n) */
	GString *target = out->accum;
	int i;

	if (name_rt != NULL && !use_lower_tail) {
		use_lower_tail = TRUE;
		g_string_append (target, name_rt);
	} else
		g_string_append (target, name);
		
	g_string_append_c (target, '(');
	
	for (i = 1; i<=n_p; i++) {
		gnm_expr_as_gstring (ptr[i], out);
		g_string_append_c (target, ',');
	}

	if (!use_lower_tail)
			g_string_append (target, "1-");
	if (use_log) {
		g_string_append (target, "exp(");
		gnm_expr_as_gstring (ptr[0], out);
		g_string_append_c (target, ')');
	} else
		gnm_expr_as_gstring (ptr[0], out);

	if (n > n_p) {
		g_string_append_c (target, ',');
		for (i = n_p+1; i<=n; i++) {
			gnm_expr_as_gstring (ptr[i], out);
			if (i < n)
				g_string_append_c (target, ',');
		}
	}
	g_string_append_c (target, ')');
}

/**
 * xlsx_func_r_q_output_handler:
 *
 * @out: #GnmConventionsOut
 * @func: #GnmExprFunction 
 * @n: last index used for a parameter
 * @n_p: index of the probability argument, usually 0 
 * @name:
 *
 * Print the appropriate simple function call
 */
static gboolean
xlsx_func_r_q_output_handler (GnmConventionsOut *out, GnmExprFunction const *func, int n, int n_p, 
			      char const *name, char const *name_rt)
{
	GnmExprConstPtr const *ptr = func->argv;
	GString *target = out->accum;
	int use_lower_tail; /* 0: never; 1: always; 2: sometimes */
	int use_log;        /* 0: never; 1: always; 2: sometimes */
	
	if (func->argc <= n || func->argc > (n+3))
		return FALSE;

	if (func->argc > n+1) {
		GnmValue const *constant = gnm_expr_get_constant (ptr[n+1]);
		if (constant == NULL || !VALUE_IS_NUMBER (constant))
			use_lower_tail = 2;
		else
			use_lower_tail = value_is_zero (constant) ? 0 : 1;
	} else
		use_lower_tail = 1;
	if (func->argc > n+2) {
		GnmValue const *constant = gnm_expr_get_constant (ptr[n+2]);
		if (constant == NULL || !VALUE_IS_NUMBER (constant))
			use_log = 2;
		else 
			use_log = value_is_zero (constant) ? 0 : 1;
	} else
		use_log = 0;

	if (use_lower_tail < 2 && use_log == 0) {
		/* R.Qx(a,b,c) --> name(a,b,c) */		
		/* R.Qx(a,b,c) --> name(1-a,b,c) */
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, use_lower_tail, 0);
		return TRUE;
	} else if (use_lower_tail < 2 && use_log == 1) {
		/* R.Qx(a,b,c) --> name(exp(a),b,c) */
		/* R.Qx(a,b,c) --> name(1-exp(a),b,c) */
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, use_lower_tail, 1);
		return TRUE;
	} else if (/* use_lower_tail == 2 && */ use_log == 0) {
		/* R.Qx(a,b,c,d) --> if(d,name(a,b,c), name(1-a,b,c)) */
		g_string_append (target, "if(");
		gnm_expr_as_gstring (ptr[n+1], out);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 1, 0);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 0, 0);
		g_string_append_c (target, ')');
		return TRUE;
	} else if (use_lower_tail < 2 /* && use_log == 2 */) {
		/* R.Qx(a,b,c,d,e) -->
                               if(e,name(1-exp(a),b,c),name(1-a,b,c))*/
		/* R.Qx(a,b,c,d,e) -->
                          if(e,name(exp(a),b,c),name(a,b,c))*/
		g_string_append (target, "if(");
		gnm_expr_as_gstring (ptr[n+2], out);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, use_lower_tail, 1);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, use_lower_tail, 0);
		g_string_append_c (target, ')');
		return TRUE;
	} else /*if (use_lower_tail == 2 && use_log == 2 */ {
		/* R.Qx(a,b,c,d,e) -->
                          if(d,if(e,name(exp(a),b,c),name(a,b,c)),
                               if(e,name(1-exp(a),b,c),name(1-a,b,c)))*/
		g_string_append (target, "if(");
		gnm_expr_as_gstring (ptr[n+1], out);
		g_string_append (target, ",if(");
		gnm_expr_as_gstring (ptr[n+2], out);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 1, 1);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 1, 0);
		g_string_append (target, "),if(");
		gnm_expr_as_gstring (ptr[n+2], out);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 0, 1);
		g_string_append_c (target, ',');
		xlsx_write_r_q_func (out, name, name_rt, ptr, n, n_p, 0, 0);
		g_string_append (target, "))");
		return TRUE;
	}
}

static gboolean
xlsx_func_norminv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 0, "_xlfn.NORM.INV", NULL);
}

static gboolean
xlsx_func_chisqinv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 1, 0, "_xlfn.CHISQ.INV", "_xlfn.CHISQ.INV.RT");
}

static gboolean
xlsx_func_finv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 0, "_xlfn.F.INV", "_xlfn.F.INV.RT");
}

static gboolean
xlsx_func_binominv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 2, "_xlfn.BINOM.INV", NULL);
}

static gboolean
xlsx_func_betainv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 0, "_xlfn.BETA.INV", NULL);
}

static gboolean
xlsx_func_gammainv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 0, "_xlfn.GAMMA.INV", NULL);
}

static gboolean
xlsx_func_lognorminv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 2, 0, "_xlfn.LOGNORM.INV", NULL);
}

static gboolean
xlsx_func_tinv_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	return xlsx_func_r_q_output_handler (out, func, 1, 0, "_xlfn.T.INV", NULL);
}

static gboolean
xlsx_func_floor_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
/* FLOOR(a) --> ROUNDDOWN(a,0) */
{
	if (func->argc == 1) {
		GString *target = out->accum;
		GnmExprConstPtr const *ptr = func->argv;
		g_string_append (target, "ROUNDDOWN(");
		gnm_expr_as_gstring (ptr[0], out);
		g_string_append (out->accum, ",0)");
		return TRUE;
	}
	return FALSE;
}

static gboolean
xlsx_func_erf_output_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	/* func->argc == 1 is handled by the translation */
	if (func->argc != 1) {
		g_string_append (out->accum, "ERF");
		gnm_expr_list_as_string (func->argc, func->argv, out);
		return TRUE;
	}
	return FALSE;
}


GnmConventions *
xlsx_conventions_new (gboolean output)
{
	static struct {
		char const *gnm_name;
		gpointer handler;
	} const xlfn_func_handlers[] = {
		{"BINOM.INV", xlsx_func_binominv_handler},
		{"CHISQ.DIST", xlsx_func_chisqdist_handler},
		{"F.DIST", xlsx_func_fdist_handler},
		{"NEGBINOM.DIST", xlsx_func_negbinomdist_handler},
		{"LOGNORM.DIST", xlsx_func_lognormdist_handler},
		{NULL, NULL}
	};

	static struct {
		char const *gnm_name;
		gpointer handler;
	} const xlfn_func_output_handlers[] = {
		{"R.QBETA", xlsx_func_betainv_output_handler},
		{"R.QBINOM", xlsx_func_binominv_output_handler},
		{"R.QCHISQ", xlsx_func_chisqinv_output_handler},
		{"R.QF", xlsx_func_finv_output_handler},
		{"R.QGAMMA", xlsx_func_gammainv_output_handler},
		{"R.QLNORM", xlsx_func_lognorminv_output_handler},
		{"R.QNORM", xlsx_func_norminv_output_handler},
		{"R.QT", xlsx_func_tinv_output_handler},
		{"ERF", xlsx_func_erf_output_handler},
		{"FLOOR", xlsx_func_floor_output_handler},
		{NULL, NULL}
	};
	
	static struct {
		char const *xlsx_name;
		char const *gnm_name;
	} const xlfn_func_renames[] = {
		{ "BETA.INV", "BETAINV" },
		{ "BINOM.DIST", "BINOMDIST" },
		/* { "BINOM.INV", "R.QBINOM" }, see handlers */
		{ "CHISQ.DIST.RT", "CHIDIST" },
		{ "CHISQ.INV", "R.QCHISQ" }, /* see output handler */
		{ "CHISQ.INV.RT", "CHIINV" },
		{ "CHISQ.TEST", "CHITEST" },
		{ "CONFIDENCE.NORM", "CONFIDENCE" },
		{ "COVARIANCE.P", "COVAR" },
		{ "ERF.PRECISE", "ERF" },
		{ "ERFC.PRECISE", "ERFC" },
		{ "EXPON.DIST", "EXPONDIST" },
		{ "F.DIST.RT", "FDIST" },
		{ "F.INV", "R.QF" }, /* see output handler */
		{ "F.INV.RT", "FINV" },
		{ "F.TEST", "FTEST" },
		{ "GAMMA.DIST", "GAMMADIST" },
		{ "GAMMA.INV", "GAMMAINV" },
		{ "HYPGEOM.DIST", "HYPGEOMDIST" },
		{ "LOGNORM.INV", "LOGINV" },
		{ "MODE.SNGL", "MODE" },
		{ "NORM.DIST", "NORMDIST" },
		{ "NORM.INV", "NORMINV" },
		{ "NORM.S.INV", "NORMSINV" },
		{ "PERCENTILE.INC", "PERCENTILE" },
		{ "PERCENTRANK.INC", "PERCENTRANK" },
		{ "POISSON.DIST", "POISSON" },
		{ "QUARTILE.INC", "QUARTILE" },
		{ "RANK.EQ", "RANK" },
		{ "STDEV.P", "STDEVP" },
		{ "STDEV.S", "STDEV" },
		{ "T.TEST", "TTEST" },
		{ "T.INV.2T", "TINV" },
		{ "VAR.P", "VARP" },
		{ "VAR.S", "VAR" },
		{ "WEIBULL.DIST", "WEIBULL" },
		{ "Z.TEST", "ZTEST" },
		{ NULL, NULL }
	};	
	GnmConventions *convs = gnm_conventions_new_full (
		sizeof (XLSXExprConventions));
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;
	int i;

	convs->decimal_sep_dot		= TRUE;
	convs->input.range_ref		= rangeref_parse;
	convs->input.external_wb	= xlsx_lookup_external_wb;
	convs->output.cell_ref		= xlsx_cellref_as_string;
	convs->output.range_ref		= xlsx_rangeref_as_string;
	convs->range_sep_colon		= TRUE;
	convs->sheet_name_sep		= '!';
	convs->arg_sep			= ',';
	convs->array_col_sep		= ',';
	convs->array_row_sep		= ';';
	convs->output.translated	= FALSE;
	xconv->extern_id_by_wb = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		(GDestroyNotify) g_object_unref, g_free);
	xconv->extern_wb_by_id = g_hash_table_new_full (g_str_hash, g_str_equal,
		g_free, (GDestroyNotify) g_object_unref);

	if (output) {
		convs->output.func      = xlsx_func_map_out;

		xconv->xlfn_map = g_hash_table_new (go_ascii_strcase_hash,
						    go_ascii_strcase_equal);
		for (i = 0; xlfn_func_renames[i].xlsx_name; i++)
			g_hash_table_insert (xconv->xlfn_map,
					     (gchar *) xlfn_func_renames[i].gnm_name,
					     (gchar *) xlfn_func_renames[i].xlsx_name);
		xconv->xlfn_handler_map = g_hash_table_new (go_ascii_strcase_hash,
							    go_ascii_strcase_equal);
		for (i = 0; xlfn_func_output_handlers[i].gnm_name; i++)
			g_hash_table_insert (xconv->xlfn_handler_map,
					     (gchar *) xlfn_func_output_handlers[i].gnm_name,
					     xlfn_func_output_handlers[i].handler);
	} else {
		convs->input.func	= xlsx_func_map_in;

		xconv->xlfn_map = g_hash_table_new (go_ascii_strcase_hash,
						    go_ascii_strcase_equal);
		for (i = 0; xlfn_func_renames[i].xlsx_name; i++)
			g_hash_table_insert (xconv->xlfn_map,
					     (gchar *) xlfn_func_renames[i].xlsx_name,
					     (gchar *) xlfn_func_renames[i].gnm_name);
		xconv->xlfn_handler_map = g_hash_table_new (go_ascii_strcase_hash,
							    go_ascii_strcase_equal);
		for (i = 0; xlfn_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (xconv->xlfn_handler_map,
					     (gchar *) xlfn_func_handlers[i].gnm_name,
					     xlfn_func_handlers[i].handler);
	}

	return convs;
}

void
xlsx_conventions_free (GnmConventions *convs)
{
	XLSXExprConventions *xconv = (XLSXExprConventions *)convs;
	g_hash_table_destroy (xconv->extern_id_by_wb);
	g_hash_table_destroy (xconv->extern_wb_by_id);
	g_hash_table_destroy (xconv->xlfn_map);
	g_hash_table_destroy (xconv->xlfn_handler_map);
	gnm_conventions_unref (convs);
}

/**
 * xlsx_pivot_date_fmt :
 *
 * Returns : A #GOFormat in the convention used for dates in pivot tables.
 **/
GOFormat *
xlsx_pivot_date_fmt (void)
{
	return go_format_new_from_XL ("yyyy-mm-dd\"T\"hh:mm:ss");
}

/**
 * xlsx_get_direction :
 *
 * Returns a GOGradientDirection corresponding to the angle ang (0...360)
 **/
GOGradientDirection
xlsx_get_gradient_direction (double ang)
{
	int ang_i;
	g_return_val_if_fail (ang >=-360. && ang <= 360., GO_GRADIENT_N_TO_S);

	ang_i = ang;
	while (ang_i < 0)
		ang_i += 360;
	while (ang_i >= 360)
		ang_i -= 360;

	ang_i = (ang_i + 22) / 45; /* now ang is between 0 and 8 */

	switch (ang_i) {
	case 1:
		return GO_GRADIENT_NW_TO_SE;
	case 2:
		return GO_GRADIENT_W_TO_E;
	case 3:
		return GO_GRADIENT_SW_TO_NE;
	case 4:
		return GO_GRADIENT_S_TO_N;
	case 5:
		return GO_GRADIENT_SE_TO_NW;
	case 6:
		return GO_GRADIENT_E_TO_W;
	case 7:
		return GO_GRADIENT_NE_TO_SW;
	case 0:
	case 8:
	default:
		return GO_GRADIENT_N_TO_S;
	}
}
