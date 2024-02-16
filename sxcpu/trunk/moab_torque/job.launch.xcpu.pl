#!/usr/bin/perl -w

# This script launches jobs with XCPU from Torque
# The $preexec option must be specified in the mom's config file in order for 
# this file to be executed
#
# Written by Hugh Greenberg <hng@lanl.gov>


#Copyright (C) 2008 by Hugh Greenberg <hng@lanl.gov>
# 
#Permission is hereby granted, free of charge, to any person obtaining a
#copy of this software and associated documentation files (the "Software"),
#to deal in the Software without restriction, including without limitation
#the rights to use, copy, modify, merge, publish, distribute, sublicense,
#and/or sell copies of the Software, and to permit persons to whom the
#Software is furnished to do so, subject to the following conditions:
#
#The above copyright notice and this permission notice (including the next
#paragraph) shall be included in all copies or substantial portions of the
#Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
#HUGH GREENBERG AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
#OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
#ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#DEALINGS IN THE SOFTWARE.


use strict;
use FindBin;
use lib "$FindBin::Bin";
use Moab::Tools;
BEGIN { require "config.xcpu.pl"; }

our ($xstat, $processorsPerNode, $xrx, $nodes, $logLevel);

if ($#ARGV != 0) {
    print("Usage: $0 path_to_torque_script\n");
    logger("Usage: $0 path_to_torque_script");
    exit 1;
}

sleep(int(rand(200)));  #Sleep so jobs submitted simultaneously 
                        #are not scheduled to the same nodes
logger("Executed $0 with arguments: @ARGV");

my $pbs_jobid = $ENV{'PBS_JOBID'};
my $userhome = $ENV{'HOME'};
my $script = $ARGV[0];
my $exec_line = "";
my @commands = ();
my %scriptenvs;
my $xrx_opts = "";

if (!$pbs_jobid) {
    logger("PBS_JOBID env variable is empty.  Cannot continue.\n");
    exit 1;
}

open(SCRIPT, "<$script") or die "Can't open $script: $!\n"; #Open Torque batch script
                                                            
while (my $line = <SCRIPT>) { #Parse Torque batch script to 
    chomp($line);             #find executables and env variables
    if ($line =~ /^#PBS/) {
	$line =~ /nodes=(\d+)/;
	if ($1) {
	    $nodes = $1;
	    logger("The number of nodes to run on is: $nodes");
        }
    }
    elsif ($line =~/^#XCPU/) {
	if ($line =~ /-p/ && $xrx_opts !~ /-p/) {
	    $xrx_opts .= "-p";
	}
    }
    elsif ($line =~ /^export/ || $line =~ /^\S+=.+?$/ || $line =~ /^setenv/ || 
	   $line =~ /^set\s.+?=.+?$/) {
	if ($line =~ /^export (\S+)=(.+)/) {
	    $scriptenvs{"export " . $1} = [ split/:/,$2 ];
	}
	elsif ($line =~ /^setenv\s(.+?)\s(.+?)$/) {
	    $scriptenvs{"export " . $1} = [ split/:/,$2 ];
	}
	elsif ($line =~ /^(\S+)=(.+?)$/) {
	    $scriptenvs{$1} = [ split/:/,$2 ];
	}
	elsif ($line =~ /^set\s(.+?)=(.+?)$/) {
	    $scriptenvs{$1} = [ split/:/,$2 ];
	}
    }
    elsif ($line !~ /^$/ && $line =~ /^[^#].*$/) {
	 $commands[@commands] = $line;
    }
}

close(SCRIPT);
export_envs(); #Export envs that need to be exported to the user's environment
execute_commands(); #Execute all of the commands found, except for the one to run on the cluster

if (!$exec_line) {
    logger("The executable to execute was not found in the script.");
    exit 1;
}

#logger("ENV: " . `env` . "\n");
my @nodestouse = find_nodes($nodes); #Find the available nodes.
logger("Nodes allocated: @nodestouse");

if (!@nodestouse) {
    logger("Unable to determine which nodes to run on.");
    exit 1;
}

my $nodestouse = join ",", @nodestouse;
my $command = "$xrx $xrx_opts -j $pbs_jobid $nodestouse $exec_line";
logger("Executing the following: $command");
system($command); #Execute the command on the cluster

if ($?) {
    my $exitc = $? >> 8;
    my $sig = $? & 127; 
    logger("The command: $command, failed to execute. " . 
	   "The process returned exit code: $exitc and was sent signal: " 
	   . $sig);
    exit 1;
}

exit 0;
	    
sub export_envs {
    for my $key (keys %scriptenvs) {
	my @ret = expand_envs($key);
	if (@ret) {
	$scriptenvs{$key} = [ @ret ];
	if ($key =~/^export/) {
	      $key =~ s/^export\s//;
	      $ENV{$key} = join(":", @ret);
	}
       } 
    }
}

sub expand_envs { #Recursively search env variables and update %scriptenvs
    my $key = shift;
    my @values = ();

    if (!$scriptenvs{$key}) {
	$key =~ s/(\$|{|}|")//g;
	if (!$scriptenvs{$key}) {
		return ();
	}
    }
    
    for my $value (@{$scriptenvs{$key}}) {
	if ($value =~ /\$/) {
	    my $tmp = $value;
	    $tmp =~ s/(\$|{|}|")//g; 
	    if ($ENV{$tmp}) {
               	push(@values, split/:/,$ENV{$tmp});
	    }
	    else {
		my @exp_values;
                @exp_values = expand_envs($value);
		
		if (@exp_values) {
	           push(@values, @exp_values);
                }
	    }
	}
	else {
	    if ($value =~ /\"/) {
	      $value =~ s/\"//g;
            }
	    push(@values, $value);
	}
    }

    return @values;
}

sub execute_commands {
    logger("Number of commands: " . @commands);
    for (my $i=0; $i<@commands; $i++) {
	if ($commands[$i] =~ /\$\S+/) {
	    my @tokens = split/\s/, $commands[$i];
	    my $final = "";
	    for my $t (@tokens) {
		if ($t =~ /\$.*/) {
		    if (my @v = expand_envs($t)) {
			$final .= join(" ", @v) . " ";
		    }
		}
		else {
		    $final .= $t . " ";
		} 
	    }
	    $final =~ s/\s$//;
	    $commands[$i] = $final;
	}
		    
	if ($i == @commands - 1) {      #Do not execute last command.  
	    $exec_line = $commands[$i]; #This should be the command to execute on the cluster.
	}
	else {
	    logger("Executing command: $commands[$i]");
	    system($commands[$i]);
	}
    }
}

sub find_nodes {   
    my $numnodes = shift;
    my $command = "$xstat 2>&1";
    my $output = `$command`;
    my @nodestouse = ();
    my @nodesrequested = ();
    my %statfsnodes = ();
    
    if ($?) {
        my $exitc = $? >> 8;
	logger("The Command: $command failed to execute and" . 
	       " returned this exit code: $exitc");
    }
    
    my @lines = split/\n/,$output;
    for (my $i = 0; $i <= $#lines; $i++) {
	my $line = shift @lines;
	
	my ($host, $ip, $os_arch, $status, $numjobs) = split/\t/,$line;
	if ($status && defined($numjobs)) {
	    next if ($status =~ /down/);
	    $statfsnodes{$host} = {"ip" => $ip,
				   "os_arch" => $os_arch,
				   "status" => $status,
				   "numjobs" => $numjobs};
	}
	else {
	    logger("Invalid output from xstat.");
	    return;
	}
    }
	
    if ($numnodes =~ /^\d+$/ && $numnodes > 0) {
	for my $h (keys %statfsnodes) {
	    for ( ; $numnodes > 0 && $statfsnodes{$h}->{"numjobs"} < $processorsPerNode; $numnodes--) {
		push @nodestouse, $h;
		$statfsnodes{$h}->{"numjobs"} = $statfsnodes{$h}->{"numjobs"} + 1;
	    }
	    last if $numnodes <= 0;
	}
    }
    
    return @nodestouse;	
}

sub logger {
    my $msg = shift;
    if ($logLevel >= 1) {
	logPrint($msg . "\n");
    }
}
