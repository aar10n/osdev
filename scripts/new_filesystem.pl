#!/usr/bin/env perl
# This script bootstraps a new filesystem type by generating the files and stub functions.
use warnings FATAL => 'all';
use strict;
use Getopt::Long;
use Data::Dumper;
use POSIX qw(strftime);

my $readonly = "";
GetOptions ("read-only" => \$readonly);

my $name = shift or die "usage: $0 [--read-only] <name>";
my $dir = "fs/$name";
die "error: please run from the project root\n" unless ( -e ".config");
die "error: filesystem with same name already exists\n" if ( -e $dir );

my $date = strftime("%Y-%m-%d", localtime time);
my $created_by = <<"EOF";
//
// Created by Aaron Gill-Braun on $date.
//
EOF

my $includes = <<"EOF";
#include <kernel/vfs/vfs.h>
#include <kernel/vfs/vnode.h>
#include <kernel/vfs/ventry.h>
EOF

my $preamble = <<"EOF";
#include <kernel/mm.h>
#include <kernel/panic.h>

#include "$name.h"

#define ASSERT(x) kassert(x)
EOF

my $static_init = <<"EOF";
static void ${name}_static_init() {
  if (fs_register_type(&${name}_type) < 0) {
    panic("failed to register $name type\\n");
  }
}
STATIC_INIT(${name}_static_init);
EOF

# START OF MAIN
my @vfs_ops = ops_from_struct("vfs_ops");
my @vnode_ops = ops_from_struct("vnode_ops");

mkdir $dir;
write_source_file(\@vfs_ops, \@vnode_ops); # name.c
write_header_file(\@vfs_ops, \@vnode_ops); # name.h
write_vfsops_file(\@vfs_ops);              # name_vfsops.c
write_vnops_file(\@vnode_ops);             # name_vnops.c

print "# fs/$name\n";
print "fs += $name/$name.c $name/${name}_vfsops.c $name/${name}_vnops.c\n";
# END OF MAIN


sub write_header_file {
    my ($vfs_ops, $vnode_ops) = @_;
    my $guard = "FS_" . uc $name . "_" . uc $name . "_H";
    open(my $file, '>', "$dir/$name.h") or die $!;
    print $file "$created_by\n";
    print $file "#ifndef $guard\n";
    print $file "#define $guard\n";
    print $file "\n";
    print $file "#include <kernel/vfs_types.h>\n";
    print $file "#include <kernel/device.h>\n";
    print $file "\n";
    print $file "// vfs operations\n";
    write_format_ops($file, $vfs_ops, "{ret}{function}({params});");
    print $file "void ${name}_vfs_cleanup(vfs_t *vfs);\n";
    print $file "\n";
    print $file "// vnode operations\n";
    write_format_ops($file, $vnode_ops, "{ret}{function}({params});");
    print $file "\n";
    print $file "void ${name}_vn_cleanup(vnode_t *vn);\n";
    print $file "void ${name}_ve_cleanup(ventry_t *ve);\n";
    print $file "\n";
    print $file "#endif\n";
    print $file "\n";
    close($file);
}

sub write_source_file {
    my ($vfs_ops, $vnode_ops) = @_;
    open(my $file, '>', "$dir/$name.c") or die $!;
    print $file "$created_by\n";
    print $file "#include <kernel/fs.h>\n";
    print $file "$preamble\n";
    print $file "struct vfs_ops ${name}_vfs_ops = {\n";
    write_format_ops($file, $vfs_ops, "  .{field} = {function},");
    print $file "  .v_cleanup = ${name}_vfs_cleanup,\n";
    print $file "};\n";
    print $file "\n";
    print $file "struct vnode_ops ${name}_vnode_ops = {\n";
    write_format_ops($file, $vnode_ops, "  .{field} = {function},");
    print $file "  .v_cleanup = ${name}_vn_cleanup,\n";
    print $file "};\n";
    print $file "\n";
    print $file "struct ventry_ops ${name}_ventry_ops = {\n";
    print $file "  .v_cleanup = ${name}_ve_cleanup,\n";
    print $file "};\n";
    print $file "\n";
    print $file "static fs_type_t ${name}_type = {\n";
    print $file "  .name = \"${name}\",\n";
    if ($readonly eq 1) {
        print $file "  .flags = VFS_RDONLY,\n";
    }
    print $file "  .vfs_ops = &${name}_vfs_ops,\n";
    print $file "  .vnode_ops = &${name}_vnode_ops,\n";
    print $file "  .ventry_ops = &${name}_ventry_ops,\n";
    print $file "};\n";
    print $file "\n\n";
    print $file "$static_init";
    close($file);
}

sub write_vfsops_file {
    my $ops = shift;
    open(my $file, '>', "$dir/${name}_vfsops.c") or die $!;
    print $file "$created_by\n";
    print $file "$includes\n";
    print $file "$preamble\n";
    print $file "//\n\n";
    write_format_ops($file, $ops, "{ret}{function}({params}) {\n  unimplemented(\"{name}\");\n};\n", "//\n");
    print $file "void ${name}_vfs_cleanup(vfs_t *vfs) {\n  unimplemented(\"vfs_cleanup\");\n};\n";
    close($file);
}

sub write_vnops_file {
    my $ops = shift;
    open(my $file, '>', "$dir/${name}_vnops.c") or die $!;
    print $file "$created_by\n";
    print $file "$includes\n";
    print $file "$preamble\n";
    print $file "//\n\n";
    write_format_ops($file, $ops, "{ret}{function}({params}) {\n  unimplemented(\"{name}\");\n};\n", "//\n");
    print $file "//\n\n";
    print $file "void ${name}_vn_cleanup(vnode_t *vn) {\n  unimplemented(\"vn_cleanup\");\n};\n";
    print $file "\n";
    print $file "void ${name}_ve_cleanup(ventry_t *ve) {\n  unimplemented(\"ve_cleanup\");\n};\n";
    close($file);
}

#
# helpers
#

sub ops_from_struct {
    my ($structname, $file) = @_;
    $file ||= "include/kernel/vfs_types.h";
    my $group;
    if ($structname =~ /vfs_ops/) {
        $group = "vfs";
    } elsif ($structname =~ /vnode_ops/) {
        $group = "vn";
    } elsif ($structname =~ /ventry_ops/) {
        $group = "ve";
    }

    open(my $fh, '<', $file) or die $!;
    my $sep = 0;
    my @ops = ();
    while (<$fh>) {
        if (/struct $structname \{/ .. /\};/) {
            $_ =~ s/^\s+|\s+$//;

            # the below regex matches a c function pointer type like
            #   int (*v_load)(struct vnode *vn);
            if (/(?<ret>[a-z_]+ \*?)\(\*(?<name>\w+)\)\((?<params>[^)]+)\);$/) {
                my $field = $+{name};
                my $name = substr $field, 2;
                my $ret = subst_with_typedefs($+{ret});
                my $params = subst_with_typedefs(trim($+{params}));
                if ($field eq "v_cleanup") {
                    # handle this as special case
                    pop @ops; # remove previous empty sep
                    goto end;
                } elsif ($field eq "v_rename") {
                    goto end; # ignore
                } elsif (! ($field =~ /v_(mount|unmount|stat|open|close|read|map|load|readlink|readdir|lookup)/)) {
                    if ($readonly eq 1) {
                        print "skipping $field\n";
                    }
                    goto end if $readonly eq 1;
                }

                push @ops, {
                    group => $group,
                    field => $field,
                    name => $name,
                    params => $params,
                    ret => $ret,
                };
                $sep = 0;
            } elsif($_ eq "" && scalar(@ops) > 0) {
                # one empty value to separate groups
                if ($sep == 0) {
                    push @ops, {};
                    $sep = 1;
                }
            }
        }
    end:
    }

    close($fh);
    return @ops;
}


sub write_format_ops {
    my $file = shift or die "missing file handle";
    my $ops = shift or die "missing ops array";
    my $format = shift or die "missing format string";
    my $emptysep = shift;
    $emptysep ||= "";

    foreach my $op (@$ops) {
        if (exists $op->{name}) {
            my $tmp = $format;
            $tmp =~ s/\{field\}/$op->{field}/g;
            $tmp =~ s/\{name\}/$op->{name}/g;
            $tmp =~ s/\{ret\}/$op->{ret}/g;
            $tmp =~ s/\{params\}/$op->{params}/g;
            $tmp =~ s/\{function\}/${name}_$op->{group}_$op->{name}/g;
            print $file "$tmp\n";
        } else {
            print $file "$emptysep\n";
        }
    }
}

sub subst_with_typedefs {
    my $s = shift;
    $s =~ s/struct vnode\b/vnode_t/g;
    $s =~ s/struct ventry\b/ventry_t/g;
    $s =~ s/struct vfs\b/vfs_t/g;
    $s =~ s/struct vm_mapping\b/vm_mapping_t/g;
    $s =~ s/struct device\b/device_t/g;
    $s =~ s/struct kio\b/kio_t/g;
    return $s;
}

sub trim {
    (my $s = shift) =~ s/^\s+|\s+$//;
    return $s;
}
