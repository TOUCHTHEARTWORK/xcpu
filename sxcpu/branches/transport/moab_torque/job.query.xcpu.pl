#! /usr/bin/perl -w
################################################################################
#
# Usage: job.query.xcpu.pl
#
# This script is used in querying jobs in xcpu clusters.
#
# Output Format: <job id> NAME=<job name> NODES=<node count> ACCOUNT=<account> 
# RCLASS=<queue name> STARTTIME=<epoch start time> STATE=Idle|Running 
# TASKLIST=<colon separated processor list> TASKS=<task count> UNAME=<user name> 
# WCLIMIT=<wall clock limit>
#
# This script was written by ClusterResources.  It was slightly modified by 
# Hugh Greenberg.
################################################################################

use strict;
use FindBin;
use Getopt::Long;
use Time::Local;
use lib "$FindBin::Bin";
use Moab::Tools qw(:default :indexes);
BEGIN { require "config.xcpu.pl"; }
our ($logLevel, $qstat);

logPrint("Invoked: $0 " . join(' ', @ARGV) . "\n") if $logLevel;

# Parse Command Line Arguments
my ($help);
my $usage = "Usage: $0\n";
GetOptions('help|?' => \$help) or logDie($usage);
die $usage if $help;

# Run qstat -f to list jobs
my %job = ();
QSTATF:
{
    my $cmd    = "$qstat -f";
    my $output = `$cmd 2>&1`;
    my $rc     = $? >> 8;
    logDie("Subcommand ($cmd) failed with rc=$rc:\n$output") if $rc;
    logPrint("Subcommand ($cmd) yielded rc=$rc:\n$output")
      if $logLevel >= 1;
    my @lines     = split /\n/, $output;
    my $jobId     = '';
    my %comment   = ();
    my $completed = 0;

    while (defined(my $line = shift @lines))
    {
        # Parse Job Id
        if ($line =~ /^Job Id: (\S+)/)
        {
            # New job encountered. Finalize previous job
            if ($jobId)
            {
                $job{$jobId}->{COMMENT} = join '?', keys %comment if %comment;
            }

            # Initialize new job
            $jobId     = $1;
            %comment   = ();
            $completed = 0;
        }

        # Parse Job State
        elsif ($line =~ /job_state = (\S+)/)
        {
            if    ($1 eq "R") { $job{$jobId}->{STATE} = "Running"; }
            elsif ($1 eq "E") { $job{$jobId}->{STATE} = "Running"; }
            elsif ($1 eq "S") { $job{$jobId}->{STATE} = "Suspended"; }
            elsif ($1 eq "Q")
            {
                $job{$jobId}->{STATE}    = "Idle";
                $job{$jobId}->{HOLDTYPE} = "";
            }
            elsif ($1 eq "H") { $job{$jobId}->{STATE} = "Idle"; }
            elsif ($1 eq "C")
            {
                $job{$jobId}->{STATE} = "Completed";
                $completed = 1;
            }
            else { $job{$jobId}->{STATE} = "Unknown"; }
        }

        # Parse Job Name
        elsif ($line =~ /Job_Name = (\S+)/)
        {
            $job{$jobId}->{NAME} = $1;
        }

        # Parse Account
        elsif ($line =~ /Account_Name = (\S+)/)
        {
            $job{$jobId}->{ACCOUNT} = $1;
        }

        # Parse Class
        elsif ($line =~ /queue = (\S+)/)
        {
            $job{$jobId}->{RCLASS} = $1;
        }

        # Parse WallTime
        elsif ($line =~ /Resource_List.walltime = (\S+)/)
        {
            $job{$jobId}->{WCLIMIT} = $1;
        }

        # Parse Memory
        elsif ($line =~ /Resource_List.mem = (\S*)/)
        {
            $job{$jobId}->{RMEM} = &toMegaBytes($1);
        }

        # Parse Quality Of Service
        elsif ($line =~ /Resource_List.qos = (\S*)/)
        {
            $job{$jobId}->{QOS} = $1;
        }

        # Parse Feature
        elsif ($line =~ /Resource_List.feature = (\S*)/)
        {
            $job{$jobId}->{RFEATURES} = $1;
        }

        # Parse Partition
        elsif ($line =~ /Resource_List.partition = (\S*)/)
        {
            $job{$jobId}->{PARTITIONMASK} = $1;
        }

        # Parse Reservation Binding
        elsif ($line =~ /Resource_List.advres = (\S+)/)
        {
            if (exists $job{$jobId}->{FLAGS})
            {
                $job{$jobId}->{FLAGS} .= ",ADVRES:$1";
            }
            else
            {
                $job{$jobId}->{FLAGS} = "ADVRES:$1";
            }
        }

        # Parse Job Flags
        elsif ($line =~ /Resource_List.jobflags = (\S+)/)
        {
            my $flags = join(',', split(/:/, $1));
            if (exists $job{$jobId}->{FLAGS})
            {
                $job{$jobId}->{FLAGS} .= ",$flags";
            }
            else
            {
                $job{$jobId}->{FLAGS} = "$flags";
            }
        }

        # Parse User
        elsif ($line =~ /euser = (\S+)/)
        {
            $job{$jobId}->{UNAME} = $1;
        }

        # Parse User (a second way)
        elsif ($line =~ /Job_Owner = (\S+)\@/ && !defined $job{$jobId}->{UNAME})
        {
            $job{$jobId}->{UNAME} = $1;
        }

        # Parse Group
        elsif ($line =~ /egroup = (\S+)/)
        {
            $job{$jobId}->{GNAME} = $1;
        }

        # Parse Node List (size == nodes)
        elsif ($line =~ /Resource_List.size = (\S*)/)
        {
            $job{$jobId}->{TASKS} = $1;
        }

        # Parse Hold_Types
        elsif ($line =~ /Hold_Types = (\S.*)$/)
        {
            my $holdType = $1;
            if    ($holdType =~ /u/) { $job{$jobId}->{HOLDTYPE} = "User"; }
            elsif ($holdType =~ /s/) { $job{$jobId}->{HOLDTYPE} = "System"; }
        }

        # Parse Host List
        elsif ($line =~ /Resource_List.hostlist = (\S*)/)
        {
            $job{$jobId}->{HOSTLIST} = $1;
        }

        # Parse StartTime
        elsif ($line =~ /mtime = (.+)/ && $job{$jobId}->{STATE} eq "Running")
        {
            my ($dow, $mon, $day, $time, $year) = split /\s+/, $1;
            my ($hour, $min, $sec) = split /:/, $time;
            my $mtime =
              timelocal($sec, $min, $hour, $day, $monthToIndex{$mon},
                $year - 1900);
            $job{$jobId}->{STARTTIME} = $mtime;
        }

        # Parse SubmitTime
        elsif ($line =~ /qtime = (.+)/)
        {
            my ($dow, $mon, $day, $time, $year) = split /\s+/, $1;
            my ($hour, $min, $sec) = split /:/, $time;
            my $qtime =
              timelocal($sec, $min, $hour, $day, $monthToIndex{$mon},
                $year - 1900);
            $job{$jobId}->{QUEUETIME} = $qtime;
        }

        # Parse Interactive
        elsif ($line =~ /interactive = True/)
        {
            if (exists $job{$jobId}->{FLAGS})
            {
                $job{$jobId}->{FLAGS} .= "INTERACTIVE";
            }
            else
            {
                $job{$jobId}->{FLAGS} = "INTERACTIVE";
            }
        }

        # Parse Restartable
        elsif ($line =~ /Rerunable = True/)
        {
            if (exists $job{$jobId}->{FLAGS})
            {
                $job{$jobId}->{FLAGS} .= "RESTARTABLE";
            }
            else
            {
                $job{$jobId}->{FLAGS} = "RESTARTABLE";
            }
        }

        # Parse Depend
        elsif ($line =~ /Resource_List.depend = (\S+)/)
        {
            my $dependList = $1;
            while ($line = shift @lines)
            {
                if ($line =~ /^\t(\S+)/)
                {
                    $dependList .= $1;
                }
                else
                {
                    unshift @lines, $line;
                    last;
                }
            }
            $dependList = join ':', split /,/, $dependList;
            $comment{"depend=$dependList"}++;
        }

        # Parse Extension Attributes
        elsif ($line =~ /\bx = (\S+)/)
        {
            my @extensions = split /;/, $1;
            foreach my $extension (@extensions)
            {
                if ($extension =~ /^(\w+):(.+)/)
                {
                    my ($attr, $value) = ($1, $2);
                    if ($attr eq "depend")
                    {
                        $comment{"depend=$value"}++;
                    }
                }
            }
        }

        # Parse Nodes
        elsif ($line =~ /Resource_List.nodes = (\S+)/)
        {
            $job{$jobId}->{TASKS} = $1;
            #        $job{$jobId}->{NODES} = $1;
        }
        elsif ($line =~ /Resource_List.nodect = (\S+)/)
        {
            $job{$jobId}->{TASKS} = $1;
            #        $job{$jobId}->{NODES} = $1;
        }

        # Parse Variable_List
        elsif ($line =~ /Variable_List = (.*)/)
        {
            my $variableList = $1;
            while ($line = shift @lines)
            {
                if ($line =~ /^\t(.*)/)
                {
                    $variableList .= $1;
                    # Parse SUBMITHOST from environment variables
                    if ($line =~ /PBS_O_HOST=([^,\s]+)/)
                    {
                        my $submitHost = $1;
                        if ($submitHost =~ /jaguar(\d+)/)
                        {
                            $submitHost = "login" . $1;
                        }
                        $job{$jobId}->{SUBMITHOST} = $submitHost;
                    }
                }
                else
                {
                    unshift @lines, $line;
                    last;
                }
            }
            
            $job{$jobId}->{ENV} = packExpression($variableList)
              unless $completed;
        }
    }
    # Finalize last job
    if ($jobId)
    {
        $job{$jobId}->{COMMENT} = join '?', keys %comment if %comment;
    }
}

# Print out the jobs
foreach my $jobId (sort keys %job)
{
    my $line = "$jobId";
    foreach my $key (sort keys %{$job{$jobId}})
    {
        $line .= " $key=$job{$jobId}->{$key}";
    }
    $line .= "\n";
    print $line;
    logPrint($line) if $logLevel >= 1;
}

################################################################################
# $packedExpression = packExpression($unpackedExpression)
# Pack an expression for moab consumption
################################################################################
sub packExpression
{
    my ($expression) = @_;

    $expression =~ s{([\s\\;<>\"\#])}{sprintf"\\%02x", ord($1)}eg;
    return "\\START" . $expression;
}

################################################################################
# $megaBytes = toMegaBytes($string)
# Convert value with units to megaBytes
################################################################################
sub toMegaBytes($$)
{
    my ($value) = @_;
    if ($value =~ /tb/)
    {
        $value =~ s/tb//;
        return int $value * 1024 * 1024;
    }
    elsif ($value =~ /gb/)
    {
        $value =~ s/gb//;
        return int $value * 1024;
    }
    elsif ($value =~ /mb/)
    {
        $value =~ s/mb//;
        return int $value;
    }
    elsif ($value =~ /kb/)
    {
        $value =~ s/kb//;
        return int $value / 1024;
    }
    else
    {
        $value =~ s/b//;
        return int $value / 1024 / 1024;
    }
}
