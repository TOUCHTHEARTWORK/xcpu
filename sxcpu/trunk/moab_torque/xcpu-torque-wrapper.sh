#!/bin/bash

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

# This script is the script launched by Torque.
# All this script does is execute job.launch.xcpu.pl
#
# The $preexec option must be specified in the mom's config file in order for 
# this file to be executed
#
# Written by Hugh Greenberg <hng@lanl.gov>

if [ -z $1 ]; then
    echo "Usage: $0 torque_script"
    exit 1
fi

/opt/moab/tools/job.launch.xcpu.pl $1
exit 0
