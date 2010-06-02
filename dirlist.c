/* $Header$
 *
 * Primitive directory lister
 *
 * Copyright (C)2008-2010 Valentin Hilbig <webmaster@scylla-charybdis.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 * $Log$
 * Revision 1.6  2010-06-02 00:26:39  tino
 * Option -t
 *
 * Revision 1.5  2010-06-01 23:51:17  tino
 * Standalone version, slightly improved with option -e
 *
 * Revision 1.3  2008-10-16 19:42:50  tino
 * Multiargs and options -a -d -l -m -u -o
 *
 * Revision 1.2  2008-05-21 17:56:53  tino
 * Options
 *
 * Revision 1.1  2008-05-07 15:12:17  tino
 * dirlist added
 */

#include "tino/main_getini.h"
#include "tino/err.h"
#include "tino/filetool.h"
#include "tino/slist.h"
#include "tino/put.h"

#include <dirent.h>

#include "dirlist_version.h"

static int f_nulls, f_buffered, f_parent, f_nodot, f_source, f_one, f_debug, f_read, f_readz, f_recurse, f_escape;
static unsigned f_set, f_unset, f_set_any, f_unset_any;
static TINO_SLIST	subdirs;
static const char	*f_type;
static int put = -1;

static void
putmode(unsigned mode)
{
  if (mode)
    putmode(mode/8);
  tino_io_put(put, '0'+(mode&7));
}

static long
get_mode(const char *dir, const char *file)
{
  static const char	*lastname;
  static unsigned	lastmode;
  const char		*tmp;
  tino_file_stat_t	st;
  long			ret;

  tmp	= tino_file_glue_pathOi(NULL, (size_t)0, dir, file);
  if (lastname && !strcmp(tmp, lastname))
    ret	= lastmode;
  else if (tino_file_lstatE(tmp, &st))
    {
      TINO_ERR1("ETTDI102B %s: cannot stat", tmp);
      ret	= -1;
    }
  else
    ret	= st.st_mode;

  if (lastname)
    tino_free_constO(lastname);
  lastname	= tmp;
  return ret;
}

static int
do_stat(const char *dir, const char *subdir, const char *name)
{
  long	mode;
  char	*tmp;

  tmp	= tino_file_glue_pathOi(NULL, (size_t)0, subdir, name);
  mode	= get_mode(dir, tmp);
  if (mode<0)
    {
      tino_freeO(tmp);
      return -1;
    }
  if (f_recurse && S_ISDIR(mode) && ( !name || name[0]!='.' || (name[1] && ( name[1]!='.' || name[2]) )))
    tino_glist_add_ptr(subdirs, tmp);
  else
    tino_freeO(tmp);

  if (f_set && ((unsigned)mode&f_set)!=f_set)
    return 1;
  if (f_unset && ((unsigned)mode&f_unset)!=0)	/* same as: if (st.st_mode&f_unset) return 1;	*/
    return 1;
  if (f_set_any && ((unsigned)mode&f_set_any)==0)
    return 1;
  if (f_unset_any && ((unsigned)mode&f_unset_any)==f_unset_any)
    return 1;

  if (f_debug)
    {
      putmode((unsigned)mode);
      tino_io_put(put, ' ');
    }
  return 0;
}

static void
out(const char *name)
{
  if (f_escape)
    tino_put_ansi(put, name, NULL);
  else
    tino_put_s(put, name);
  tino_io_put(put, f_nulls ? 0 : '\n');
  if (!f_buffered)
    tino_io_flush_write(put);
}

static int
do_dirlist(const char *dir, const char *subdir, const char *both, int stats)
{
  DIR           *dp;
  struct dirent *d;

  if ((dp=opendir(both))==0)
    {
      TINO_ERR1("ETTDI100B %s: cannot open", both);
      return 0;
    }
  while (errno=0, (d=readdir(dp))!=0)
    {
      if (d->d_name[0]=='.')
	{
	  if (d->d_name[1]==0 || (d->d_name[1]=='.' && d->d_name[2]==0))
	    {
	      if (!f_parent)
		continue;
	    }
	  else if (f_nodot)
	    continue;
	}

      if ((f_recurse || stats) && do_stat(dir, subdir, d->d_name))
	continue;

      if (f_source || subdir)
	{
	  const char	*tmp;

	  tmp	= tino_file_glue_pathOi(NULL, (size_t)0, (f_source ? both : subdir), d->d_name);
	  out(tmp);
	  tino_free_constO(tmp);
	}
      else
	out(d->d_name);
    }
  if (errno)
    {
      TINO_ERR1("ETTDI103B %s: read error", both);
      closedir(dp);
      return 0;
    }
  if (closedir(dp))
    {
      TINO_ERR1("ETTDI101B %s: close error", both);
      return 0;
    }
  return 1;
}

static int
do_dirlist_step(const char *dir, const char *sub, int stats)
{
  const char	*tmp;
  int		ret;

  tmp	= tino_file_glue_pathOi(NULL, (size_t)0, dir, sub);
  ret	= do_dirlist(dir, sub, tmp, stats);
  tino_free_constO(tmp);
  return ret;
}

static int
do_dirlist_start(const char *dir, int stats)
{
  const char	*sub;

  if (!do_dirlist_step(dir, NULL, stats))
    return 0;

  while ((sub=tino_slist_get(subdirs))!=0)
    {
      do_dirlist_step(dir, sub, stats);
      tino_free_constO(sub);
    }
  return 1;	/* Directory found, even if it contains errors in subdirectories	*/
}

static void
do_dirlist1(const char *dir, int stats)
{
  if (do_dirlist_start(dir, stats) && f_one)
    exit(0);
}

static void
set_type(void)
{
  static struct { int bits; const char *type; } types[] =
    {
      { S_IFSOCK,	"sock" },
      { S_IFLNK,	"soft" },
      { S_IFREG,	"file" },
      { S_IFBLK,	"blk" },
      { S_IFDIR,	"dir" },
      { S_IFCHR,	"chr" },
      { S_IFIFO,	"fifo" },
    };
  int	i;

  for (i=sizeof types/sizeof *types; --i>=0; )
    if (!strcmp(types[i].type, f_type))
      {
        f_set	|= types[i].bits;
	f_unset	|= types[i].bits ^ S_IFMT;
	f_type	= 0;	/* we do not need to calc it again	*/
	return;
      }
  fprintf(stderr, "error: unknown type: '%s' (use type '?' to see a list)\n", f_type);
  if (!strcmp(f_type, "?"))
    {
      fprintf(stderr, "Possible types:\n");
      for (i=sizeof types/sizeof *types; --i>=0; )
	fprintf(stderr, "%-5s (%07o)\n", types[i].type, types[i].bits);
    }
  exit(1);
}

static void
dirlist(const char *dir, void *user)
{
  subdirs	= tino_slist_new();

  if (f_type)
    set_type();
  if (!dir)
    dir=".";
  if (!f_debug)
    do_dirlist1(dir, (f_set || f_unset || f_set_any || f_unset_any));
  else if (!do_stat(NULL, NULL, dir))
    {
      out(dir);
      if (f_recurse)
	do_dirlist_start(dir, (f_set || f_unset || f_set_any || f_unset_any));
    }
  tino_slist_destroy(subdirs);
  subdirs	= 0;

  tino_io_flush_write(put);
}

int
main(int argc, char **argv)
{
  put	= tino_io_fd(1, "stdout");
  return tino_main_if(dirlist,
		      NULL,	/* user	*/
		      NULL,
		      argc, argv,
		      0, -1,
		      TINO_GETOPT_VERSION(DIRLIST_VERSION)
		      " [directory..]\n"
		      "\tA primitive directory lister, lists all names in a dir,\n"
		      "\tskips . and .. by default.\n"
		      "\t# dirlist -rem0100000 |\n"  /* -0m04000 | dirlist -0zm04000 | dirlist -ezm0100000 |\n" */
		      "\t# while read -r a; do eval a=\"\\$'$a'\"; ..."
		      ,

		      TINO_GETOPT_FLAG
		      "0	write NUL terminated lines (instead of LF)\n"
		      "		Cannot be used with -e"
		      , &f_nulls,

		      TINO_GETOPT_UNSIGNED
		      "a mode	any given bit set in Mode. (see -m)"
		      , &f_set_any,

		      TINO_GETOPT_FLAG
		      "d	debug mode, print octal mode in front of filename\n"
		      "		Does no directlry list, needs a stat() call"
		      , &f_debug,

		      TINO_GETOPT_FLAG
		      "e	ANSI-escape mode for easy bash integration, example:\n"
		      "		# dirlist -e | while read f; do eval f=\"\\$'$f'\"; ...\n"
		      , &f_escape,

		      TINO_GETOPT_FLAG
		      "f	full buffered IO (no flushs after each line)"
		      , &f_buffered,
#if 0
		      TINO_GETOPT_FLAG
		      "i	on file arguments, read targets from it, '-' for stdIn"
		      , &f_read,
#endif
		      TINO_GETOPT_UNSIGNED
		      "l mode	given bits must be unset in Mode. (see -m)"
		      , &f_unset,

		      TINO_GETOPT_UNSIGNED
		      "m mode	given bits must be set in Mode. (see also -l -a -u)\n"
		      "		Prefix with 0 for octal, needs additional stat() calls.\n"
		      "		If -a -l -m -u is used together, all must apply."
		      , &f_set,

		      TINO_GETOPT_FLAG
		      "n	no dot-files, hides files starting with a ."
		      , &f_nodot,

		      TINO_GETOPT_FLAG
		      "o	one arg mode, this lists the first available directory\n"
		      "		dirlist a b c -- lists b but not c if a is not available"
		      , &f_one,

		      TINO_GETOPT_FLAG
		      "p	list . and .."
		      , &f_parent,

		      TINO_GETOPT_FLAG
		      "r	recurse subdirectories"
		      , &f_recurse,

		      TINO_GETOPT_FLAG
		      "s	add source path to output"
		      , &f_source,

		      TINO_GETOPT_STRING
		      "t type	set file type to use (dir, file, etc.)\n"
		      "		sets the correct flags for -m and -l"
		      , &f_type,

		      TINO_GETOPT_UNSIGNED
		      "u mode	any given bit unset in Mode. (see -m)"
		      , &f_unset_any,
#if 0
		      TINO_GETOPT_FLAG
		      "z	on -i read NUL terminated input (else lines)\n"
		      "		For convenience, 'dirlist -z' is 'dirlist -isz .'"
		      , &f_readz,
#endif
		      NULL);
}
