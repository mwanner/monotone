#! /usr/bin/perl

# Copyright (C) 2010, 2011 Richard Levitte <richard@levitte.org>
#
# This program is made available under the GNU GPL version 2.0 or
# greater. See the accompanying file COPYING for details.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
# PURPOSE.

use warnings;
use strict;

my $mtn = $ENV{'MTN'} || 'mtn';
$ENV{'LANG'} = 'C';

open MANPAGE,"$mtn manpage|" || die "Couldn't start 'mtn manpage': $!\n";

my $current_command = "mtn";
my %options_noarg = ();
my %options_arg = ();
my %command_options = ();
my %command_args = ();
my %commands = ( $current_command => {} );

while (<MANPAGE>) {
    chomp;
    next if ! m|^\.IP\s|;

    # Because the manpage has a lot of dashes escaped, let's unescape them.
    s/\\-/-/g;

    if (m|^\.IP "(--[^"]*)"|) {
	my $opts = $1;
	$opts =~ s|\[\s*(-[a-zA-Z\@])\s*\](\s*<arg>\s*)|$2/ $1$2|g;
	$opts =~ s|\[\s*(-[a-zA-Z\@])\s*\]|/ $1 |g;
	foreach (split(m|\s*/\s*|, $opts)) {
#	    print STDERR "DEBUG[$current_command]: opt = $_\n";
	    my $current_option;
	    if (m|^\s*(-[-a-zA-Z\@]*)\s+\<arg\>\s*$|) {
		$current_option = $1;
		$options_arg{$current_option} = 1;
	    } elsif (m|^\s*(-[-a-zA-Z\@]*)\s*$|) {
		$current_option = $1;
		$options_noarg{$current_option} = 1;
	    }
	    $command_options{$current_command} = []
		if (!defined $command_options{$current_command});
	    push @{$command_options{$current_command}}, $current_option;
	}
    } elsif (m|^\.IP "\\fB(.*)\s\\fP ([^"]*)"|) {
	$current_command = "mtn $1";
	$commands{$current_command} = {};
	my $current_command_args = $2;
	$current_command_args =~ s|[\[\]]||g;
	$current_command_args =~ s|\.\.\.\s*$| \.\.\.|;
	$command_args{$current_command} =
	    [ split(m|\s+|,$current_command_args) ];

	my $parent_command = "";
	for my $commandlet (split(m|\s+|,$current_command)) {
#	    print STDERR "DEBUG[$current_command]: parent command = $parent_command\n";
#	    print STDERR "DEBUG[$current_command]: commandlet = $commandlet\n";
	    if ($parent_command eq "") {
		$parent_command = $commandlet;
	    } else {
		$commands{$parent_command} = {}
		    if !defined $commands{$parent_command};
		$commands{$parent_command}->{$commandlet} = 1;
		$parent_command .= " " . $commandlet;
	    }
	}
    } elsif (m|^\.IP "\\fB(.*)\s\\fP"|) {
	$current_command = "mtn $1";
	$commands{$current_command} = {};

	my $parent_command = "";
	for my $commandlet (split(m|\s+|,$current_command)) {
#	    print STDERR "DEBUG[$current_command]: parent command = $parent_command\n";
#	    print STDERR "DEBUG[$current_command]: commandlet = $commandlet\n";
	    if ($parent_command eq "") {
		$parent_command = $commandlet;
	    } else {
		$commands{$parent_command} = {}
		    if !defined $commands{$parent_command};
		$commands{$parent_command}->{$commandlet} = 1;
		$parent_command .= " " . $commandlet;
	    }
	}
    }
}

#print STDERR "DEBUG: command keys: \n  ",join("\n  ", sort keys %commands),"\n";

print "declare -a _monotone_options_noarg\n";
print "_monotone_options_noarg=(\n    "
    ,join("\n    ", sort keys %options_noarg),"\n)\n";

print "declare -a _monotone_options_arg\n";
print "_monotone_options_arg=(\n    "
    ,join("\n    ", sort keys %options_arg),"\n)\n";

print "declare -A _monotone_options_arg_fns\n";
print "_monotone_options_arg_fns=(\n";
print "    [--authors-file]=_monotone_files\n";
print "    [--bind]=_monotone_address_port\n";
print "    [--branch]=_monotone_branches [-b]=_monotone_branches\n";
print "    [--branches-file]=_monotone_files\n";
print "    [--confdir]=_monotone_dirs\n";
print "    [--conflicts-file]=_monotone_files\n";
print "    [--db]=_monotone_databases [-d]=_monotone_databases\n";
print "    [--dump]=_monotone_files\n";
print "    [--export-marks]=_monotone_files\n";
print "    [--from]=_monotone_revisions\n";
print "    [--import-marks]=_monotone_files\n";
print "    [--keydir]=_monotone_dirs\n";
print "    [--key]=_monotone_key [-k]=_monotone_public_key\n";
print "    [--key-to-push]=_monotone_public_key\n";
print "    [--log]=_monotone_files\n";
print "    [--message_file]=_monotone_files\n";
print "    [--pid-file]=_monotone_files\n";
print "    [--rcfile]=_monotone_files\n";
print "    [--refs]=_monotone_refs\n";
print "    [--remote-stdio-host]=_monotone_address_port\n";
print "    [--resolve-conflicts-file]=_monotone_files\n";
print "    [--revision]=_monotone_revisions [-r]=_monotone_revisions\n";
print "    [--root]=_monotone_dirs\n";
print "    [--ssh-sign]=_monotone_ssh_sign\n";
print "    [--ticker]=_monotone_ticker\n";
print "    [--xargs]=_monotone_files\n";
print "    [-@]=_monotone_files\n";
print ")\n";

print "declare -A _monotone_aliases\n";

print "_monotone_aliases['mtn au']='mtn automate'\n";
print "_monotone_aliases['mtn di']='mtn diff'\n";
print "_monotone_aliases['mtn ls']='mtn list'\n";
print "_monotone_aliases['mtn list dbs']='mtn list databases'\n";

print "declare -A _monotone_command_options\n";
print "declare -A _monotone_command_args\n";
print "declare -A _monotone_commands\n";

foreach my $key (sort keys %commands) {
#    print STDERR "DEBUG: key = $key\n";
    print "_monotone_commands['$key']='"
	,join(" ",sort keys %{$commands{$key}})
	,"'\n" if %{$commands{$key}};
    print "_monotone_command_args['$key']='"
	,join(" ",@{$command_args{$key}})
	,"'\n" if $command_args{$key};
    print "_monotone_command_options['$key']='"
	,join(" ",sort @{$command_options{$key}}),"'\n"
	if $command_options{$key};
    print "### Missing $key\n"
	if (!%{$commands{$key}} &&
	    !$command_args{$key} &&
	    !$command_options{$key});
}
