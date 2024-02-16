#! /usr/bin/perl -w
################################################################################
#
# Usage: job.start.xcpu.pl <job_id> <node_list> <user_id>
#
# This script is used to start jobs in a XCPU + PBS/TORQUE cluster.
#
# This script was written by ClusterResources. 
################################################################################

use strict;
use FindBin;
use Getopt::Long;
use lib "$FindBin::Bin";
use Moab::Tools;
BEGIN { require "config.xcpu.pl"; }
our ($logLevel, $pbsnodes, $qrun);
my ($cmd, $output, $rc);

logPrint("$0 ", join(' ', @ARGV), "\n") if $logLevel;

# Parse Command Line Arguments
my ($help, $jobId, $nodeList, $userId);
my $usage = "Usage: $0 <job_id> <node_list> <user_id>\n";
GetOptions('help|?' => \$help) or logDie($usage);
die $usage if $help;
if (@ARGV == 3)
{
    ($jobId, $nodeList, $userId) = @ARGV;
}
else
{
    logDie($usage);
}

my $masterHost;
{
    # Pick a random login node to run on
    my $cmd    = "$pbsnodes -a";
    my $output = `$cmd 2>&1`;
    my $rc     = $? >> 8;
    logDie("Subcommand ($cmd) failed with rc=$rc:\n$output") if $rc;
    logPrint("Subcommand ($cmd) yielded rc=$rc:\n$output")
      if $logLevel >= 1;

    my %numJobs   = ();
    my %nodeState = ();
    my $nodeId    = "";
    my @lines     = split /\n/, $output;
    foreach my $line (@lines)
    {
        if ($line =~ /^(\S+)/)
        {
            $nodeId = $1;
        }
        elsif ($line =~ /state = (\S+)/)
        {
            $nodeState{$nodeId} = $1;
            $numJobs{$nodeId}   = 0;
        }
        elsif ($line =~ /jobs=([^,]*)/)
        {
            my @jobs = split /s+/, $1;
            $numJobs{$nodeId} = scalar @jobs;
        }
    }

    foreach my $nodeId (sort { $numJobs{$a} <=> $numJobs{$b} } keys %numJobs)
    {
        if ($nodeState{$nodeId} eq "free")
        {
            $masterHost = $nodeId;
            last;
        }
    }
}
logDie("There are no mom nodes free to run jobs\n") unless $masterHost;

# Start the job with qrun
$cmd = "$qrun -H $masterHost $jobId";
logPrint("Invoking subcommand: $cmd\n") if $logLevel >= 1;
$output = `$cmd 2>&1`;
$rc     = $? >> 8;
print $output;
if ($rc)
{
    logPrint("Subcommand ($cmd) failed with rc=$rc:\n$output");
}
elsif ($logLevel >= 1)
{
    logPrint("Subcommand ($cmd) returned with rc=$rc:\n$output");
}
exit $rc;
