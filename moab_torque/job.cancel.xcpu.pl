#!/usr/bin/perl -w

# This script cancels all jobs that match a specific Torque JobID.
# It is executed by the canceljob command from Moab.
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

our ($xk, $logLevel);

if ($#ARGV != 0) {
    logDie("Usage: $0 jobid\n");
}

my $jobId = $ARGV[0];
my $command = "$xk -m 9 $jobId 2>&1";
my $output = `$command`;
my $exitc = $? >> 8;

if ($exitc) {
    logPrint("Command: $command failed to execute. Exit code: $exitc" .
	     " was returned\n") if $logLevel >= 1;
}

exit 0;
