#################################################################################
# Configuration file for xcpu tools
#
# This was written by ClusterResources.  Modifications were made for XCPU by
# Hugh Greenberg.
################################################################################

use FindBin qw($Bin);    # The $Bin directory is the directory this file is in

# Important:  Moab::Tools must be included in the calling script
# before this config file so that homeDir is properly set.
our ($homeDir);

# Set the PATH to include directories for bproc and torque binaries
$ENV{PATH} = "$ENV{PATH}:/opt/torque/bin:/usr/bin:/usr/local/bin";

# Set paths as necessary -- these can be short names if PATH is included above
$xstat    = 'xstat';
$xrx      = 'xrx';
$xk       = 'xk';
$qrun     = 'qrun';
$qstat    = 'qstat';
$pbsnodes = 'pbsnodes';

# Set configured node resources
$processorsPerNode = 2;        # Number of processors
$memoryPerNode     = 2048;     # Memory in megabytes
$swapPerNode       = 2048;     # Swap in megabytes

# Specify level of log detail
$logLevel = 1;

# The default number of processors to run on
$nodes = 1;
