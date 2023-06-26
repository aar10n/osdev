#!/usr/bin/env perl
# Usage: overrides.pl gen <var> <path>
#        overrides.pl [value ...]
#
# This script is called by make to either generate a list of possible
# override variables for a given path, or to select the first non-empty
# value from a list of possible override values.
use strict;
use warnings;

exit(1) if $#ARGV == 0;
if ($ARGV[0] eq 'gen') {
    # given a variable 'VAR' and path foo/bar/baz.c we need to
    # print the following variable names (ordered by priority):
    #     VAR-foo-bar-baz.c VAR-foo-bar VAR-foo VAR

    shift;
    my $var = shift or exit(1);
    my $path = shift or exit(1);
    die("path must be relative to project root") if $path =~ /^\//;
    $path =~ s/^\.\.?\///;
    my @overrides = ($var);
    for my $part (split(/\//, $path)) {
        $var .= '-' . $part;
        push(@overrides, $var);
    }

    print join(' ', reverse(@overrides)) . "\n";
} else {
    # return the first non-empty value from the provided arguments

    for my $arg (@ARGV) {
        $arg =~ s/^\s+|\s+$//g;
        if ($arg ne '') {
            print $arg . "\n";
            exit(0);
        }
    }
    exit(1);
}
