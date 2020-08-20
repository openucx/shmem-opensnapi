# ISx Graph edge builder

This is a graph edge creation and sorting benchmark based on ISx Scalable
Integer Sort Application.

See license.txt in this directory for copyright information

## Original ISx information

----------------------------------------
ISx is Scalable Integer Sort Application 
----------------------------------------

* ISx is a new scalable integer sort application designed for co-design 
  in the exascale era, scalable to large numbers of nodes.

* ISx belongs to the class of bucket sort algorithms which perform an 
  all-to-all communication pattern.

* ISx is inspired by the NAS Parallel Benchmark Integer Sort and its OpenSHMEM
  implementation of University of Houston. ISx addresses identified shortcomings 
  of the NPB IS.

* ISx is a highly modular application implemented in the OpenSHMEM parallel 
  programming model and supports both strong and weak scaling studies.

* ISx uses an uniform random key distribution and guarantees load balance.  

* ISx includes a verification stage.

* ISx is not a benchmark. It does not define fixed problems that can be used 
  to rank systems. Furthermore ISx has not been optimzed for the features 
  of any particular system.

* ISx has been presented at the PGAS 2015 conference 


References:
ISx, a Scalable Integer Sort for Co-design in the Exascale Era. 
Ulf Hanebutte and Jacob Hemstad. Proc. Ninth Conf. on Partitioned Global Address Space 
Programming Models (PGAS). Washington, DC. Sep 2015. http://hpcl.seas.gwu.edu/pgas15/
http://ieeexplore.ieee.org/xpl/mostRecentIssue.jsp?punumber=7306013

Information about the NAS Parallel Benchmarks may be found here:
https://www.nas.nasa.gov/publications/npb.html

The OpenSHMEM NAS Parallel Benchmarks 1.0a by the HPCTools Group University of Houston
can be downloaded at http://www.openshmem.org/site/Downloads/Examples


STRONG SCALING (isx.strong): Total number of keys are fixed and the number of keys per PE
are reduced with increasing number of PEs
 Invariants: Total number of keys, max key value
 Variable:   Number of keys per PE, Bucket width

WEAK SCALING (isx.weak): The number of keys per PE is fixed and the total number of keys
grow with increasing number of PEs
 Invariants: Number of keys per PE, max key value
 Variable:   Total Number of Keys, Bucket width 

WEAK_ISOBUCKET (isx.weak_iso): Same as WEAK except the maximum key value grows with the 
number of PEs to keep bucket width constant This option is provided in effort to 
keep the amount of time spent in the local sort per PE constant. Without this option,
the local sort time reduces with growing numbers of PEs due to a shrinking histogram 
improving cache performance.
 Invariants: Number of keys per PE, bucket width
 Variable:   Total number of keys, max key value
