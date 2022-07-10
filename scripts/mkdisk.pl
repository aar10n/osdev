#!/usr/bin/perl
use strict;
use warnings FATAL => 'all';
use feature qw(state);
use Getopt::Long;
use File::Basename;

my %dirs = ();
my %files = ();

my $usage = <<"EOF";
Usage: $0 -o <file> [-s <size>] [-L <label>] [<src>:<dest> ...]
EOF

GetOptions(
    "o|outfile=s" => \(my $outfile),
    "s|size=s" => \(my $size),
    "L|label=s" => \(my $label),
);

if (!defined $outfile || (! -f $outfile && !defined $size)) {
    die $usage;
}

if (! -f $outfile || defined $size) {
    $label = "Untitled" if !defined $label;
    my $mkfs_opts = "";
    if (defined $label) {
        $mkfs_opts .= " -L '$label'";
    }

    my ($bs, $count) = parse_size($size);
    if (-f $outfile) {
        unlink $outfile;
    }

    command_or_die("dd if=/dev/zero of=$outfile bs=$bs count=$count");
    command_or_die("mke2fs -t ext2 $mkfs_opts $outfile");
}

for my $arg (@ARGV) {
    print "copying $arg\n";
    my ($src, $dest) = split /:/, $arg;
    if (!defined $src) {
        die $usage;
    } elsif (!defined $dest) {
        $dest = "/";
    }

    disk_cp($src, $dest);
}

#

sub disk_cp {
    my ($src, $dest) = @_;
    if (! -d $src) {
        command_or_die("e2cp $src $outfile:$dest");
        return;
    }

    $src =~ s/\/$//;
    $dest =~ s/\/$//;
    my $files = qx(find $src -type f);
    for my $srcfile (split /\n/, $files) {
        my $destfile = $srcfile;
        $destfile =~ s/^$src\//$dest\//;

        disk_mkdir(dirname($destfile));
        command_or_die("e2cp $srcfile $outfile:$destfile");
    }
}

sub disk_mkdir {
    my ($dir) = @_;
    if (defined $dirs{$dir} || $dir =~ /^(\.\.?|\.?\/)$/) {
        return;
    }
    command_or_die("e2mkdir $outfile:$dir");
    $dirs{$dir} = 1;
}

sub disk_isdir {
    my ($path) = @_;
    if (defined $dirs{$path} || $path =~ /^(\.\.?|\.?\/)$/) {
        return 1;
    } elsif (defined $files{$path}) {
        return 0;
    }

    my $ret = qx(e2ls -a $outfile:$path 2>&1);
    $ret =~ s/\s*$//;
    if ($? == 0) {
        my $filename = basename($path);
        if ($ret =~ /^$filename$/) {
            $files{$path} = 1;
            return 0;
        }
        $dirs{$path} = 1;
        return 1;
    } else {
        return 0;
    }
}

#

sub parse_size {
    my $sz = shift;
    if ($sz =~ /^(\d+)([kmg]?)$/i) {
        my $num = $1;
        my $unit = lc $2;
        if ($unit eq "") {
            return "$num", "1";
        } elsif ($unit eq "k") {
            return "1k", "$num";
        } elsif ($unit eq "m") {
            return "1m", "$num";
        } elsif ($unit eq "g") {
            return "1G", "$num";
        }
    }
    die "Error: invalid size: $size";
}

sub command_or_die {
    print "@_\n";
    return if defined $ENV{DEBUG} && $ENV{DEBUG} == 1;

    my $out = qx(@_);
    if ($? != 0) {
        die "Error: '@_' failed: $!";
    }
    return $out;
}
