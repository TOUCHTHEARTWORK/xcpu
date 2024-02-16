#! /usr/bin/perl
################################################################################
#
# Usage: node.query.xcpu.pl
#
# This script is used in querying moab nodes in an xcpu torque system.
#
# Output Format: <processor id> ACLASS=<comma separated list of available queues> CCLASS=<comma separated list of configured queues> CPROC=<configured processors> FEATURE=compute|login|yod RACK=<rack number> SLOT=<slot number> STATE=Idle|Busy
#
# This script was written by ClusterResources.  It was modified by 
# Hugh Greenberg to work with XCPU.
################################################################################

#Modifications to the original script are 
#Copyrighted (C) 2008 by Hugh Greenberg <hng@lanl.gov>

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
use warnings;
use FindBin;
use Getopt::Long;
use lib "$FindBin::Bin";
use Moab::Tools;    # Required before including config to set $homeDir
BEGIN { require "config.xcpu.pl"; }
our (
    $logLevel,          $xstat,
    $qstat,             $pbsnodes,
    $processorsPerNode, $memoryPerNode,
    $swapPerNode,       $populateNodeClassesFromTorque,
);

logPrint("Invoked: $0 " . join(' ', @ARGV) . "\n") if $logLevel;

my %node = ();

# Parse the qstat output to get the class information from pbs/Torque
my @classList = ();
QSTATQ:
if ($populateNodeClassesFromTorque)
{
    my $cmd    = "$qstat -q";
    my $output = `$cmd 2>&1`;
    my $rc     = $? >> 8;
    if ($rc)
    {
        my $msg = "Subcommand ($cmd) failed with rc=$rc:\n$output";
        logDie($msg);
    }
    logPrint("Subcommand ($cmd) returned with rc=$rc:\n$output")
      if $logLevel >= 1;
    my @lines = split /\n/, $output;
    foreach my $line (@lines)
    {
        chomp $line;
        my ($Queue, $Memory, $CpuTime, $Walltime, $Node, $Run, $Que, $Lm,
            $State) = split /\s+/, $line, 9;

        push @classList, $Queue if defined $State && $State =~ /E R/;
    }
}

# Parse the pbsnodes output to get the feature information from pbs/Torque
my %features = ();
PBSNODES:
{
    my $cmd    = "$pbsnodes -a";
    my $output = `$cmd 2>&1`;
    my $rc     = $? >> 8;
    logDie("Subcommand ($cmd) failed with rc=$rc:\n$output") if $rc;
    logPrint("Subcommand ($cmd) yielded rc=$rc:\n$output")
      if $logLevel >= 1;

    my $nodeId = "";
    my @lines = split /\n/, $output;
    foreach my $line (@lines)
    {
        if ($line =~ /^(\S+)/)
        {
            $nodeId = $1;
        }
        elsif ($line =~ /properties = (\S+)/)
        {
            $features{$nodeId} = $1;
        }
    }
}

my $cmd    = "$xstat";
my $output = `$cmd 2>&1`;
my $rc     = $? >> 8;
if ($rc)
{
    logDie("Subcommand ($cmd) failed with rc=$rc:\n$output") if $rc;
}
logPrint("Subcommand ($cmd) yielded rc=$rc:\n$output")
    if $logLevel >= 1;
my $nodeId = '';
foreach my $line (split /\n+/, $output)
{
    my ($nodeId, $ip, $os_arch, $status) = split /\s+/, $line;
    $os_arch =~ /\/(.*)\/(.*)/;
    my ($operatingSystem, $architecture) = ("\l$1", $2);
    next unless defined $nodeId;
    
    # Set status
    if    ('up'   eq $status) { $node{$nodeId}->{STATE} = 'Idle' }
    elsif ('down' eq $status) { $node{$nodeId}->{STATE} = 'Down' }
    else                      { $node{$nodeId}->{STATE} = 'Unknown' }
    
    # Add Configured Processors
    $node{$nodeId}->{CPROC} = $processorsPerNode;
    $node{$nodeId}->{APROC} = $processorsPerNode;
    
    # Add Configured Memory and Swap
    $node{$nodeId}->{CMEMORY} = $memoryPerNode;
    $node{$nodeId}->{CSWAP}   = $swapPerNode;
    
    # Add Architecture and Operating System
    $node{$nodeId}->{ARCH} = $architecture    if $architecture;
    $node{$nodeId}->{OS}   = $operatingSystem if $operatingSystem;
    
    # Add Class Info if defined
    if (scalar @classList)
    {
        foreach my $class (@classList)
        {
            $node{$nodeId}->{ACLASS} .= ",$class:$node{$nodeId}->{CPROC}";
            $node{$nodeId}->{CCLASS} .= ",$class:$node{$nodeId}->{CPROC}";
        }
        $node{$nodeId}->{ACLASS} =~ s/^,//;
        $node{$nodeId}->{CCLASS} =~ s/^,//;
    }
}

# Print out the nodes
foreach my $nodeId (sort numerically keys %node)
{
    my $line = "$nodeId";
    foreach my $key (sort keys %{$node{$nodeId}})
    {
        $line .= " $key=$node{$nodeId}->{$key}";
    }
    $line .= "\n";
    print $line;
    logPrint($line) if $logLevel >= 1;
}

sub numerically
{
    if   ($a =~ /^\d+$/ && $b =~ /^\d+$/) { return $a <=> $b; }
    else                                  { return $a cmp $b; }
}

