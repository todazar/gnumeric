#!/usr/bin/perl -w
# -*- Mode: perl; indent-tabs-mode: nil -*-

#
#  Nautilus
#
#  Copyright (C) 2000 Eazel, Inc.
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License as
#  published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this library; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#  Authors: Darin Adler <darin@eazel.com>
#           Morten Welinder <terra@gnome.org>
#

# check-config-h.pl: Search for .c files where someone forgot to
# put an include for <gnumeric-config.h> in.

use diagnostics;
use strict;

use Getopt::Long;

my $exitcode = 0;
my $configfile = &guess_config_file ();
my $configfileregexp = '[' . join ('][', split (//, $configfile)) . ']';

my $edit = 0;
&GetOptions("edit" => \$edit);

# default to all the files starting from the current directory
if (!@ARGV) {
    @ARGV = `find . '(' -name intl -type d -prune ')' -o '(' -name '*.c' -type f -print ')'`;
    foreach (@ARGV) { chomp; }
}

# locate all of the target lines
my @missing_files;
FILE: foreach my $file (@ARGV)
  {
    local (*FILE);
    open FILE, "< $file" or die "can't open $file: $!\n";
    while (<FILE>) {
        next FILE if /generated by/;
        next FILE if /^\s*\#\s*include\s*[<\"]$configfileregexp[>\"]/;
    }
    close FILE;
    push @missing_files, $file;
  }

if (@missing_files) {
    print "\n", scalar (@missing_files), " C files don't have <$configfile> includes:\n\n";
    if (!$edit) {
        print join("\n", @missing_files), "\n";
        $exitcode = 1;
    } else {
        foreach my $file (@missing_files) {
            local (*OLD);
            local (*NEW);

            open OLD, "< $file" or die "can't open $file: $!\n";
            open NEW, "> $file.new" or die "can't open $file.new: $!\n";
            while (<OLD>) {
                if (/^\s*\#\s*include\s*/) {
                    print NEW "$&<$configfile>\n";
                    print NEW;
                    last;
                }
                print NEW;
            }
            print NEW <OLD>;
            close NEW;
            close OLD;
            rename "$file.new", $file or die "can't rename $file: $!\n";
            print "Edited $file\n";
        }
    }
}

exit $exitcode;

# -----------------------------------------------------------------------------

sub guess_config_file {
    local (*FIL);
    $configfile = "config.h";
    open (*FIL, "<configure.ac") || return $configfile;
    while (<FIL>) {
        if (/^AM_CONFIG_HEADER\((.*)\)/) {
            $configfile = $1;
            $configfile =~ s/^\[(.*)\]$/$1/;
        }
    }
    close (FIL);
    return $configfile;
}

# -----------------------------------------------------------------------------
