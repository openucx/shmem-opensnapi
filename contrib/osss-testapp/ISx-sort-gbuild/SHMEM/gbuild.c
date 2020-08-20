/*
Copyright (c) 2015, Intel Corporation
Copyright (c) 2018, 2019, Arm Inc

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions 
are met:

    * Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above 
      copyright notice, this list of conditions and the following 
      disclaimer in the documentation and/or other materials provided 
      with the distribution.
    * Neither the name of Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products 
      derived from this software without specific prior written 
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE.
*/
#define _GNU_SOURCE

#include <shmem.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>
#include <unistd.h> // sleep()
#include <sys/stat.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include "timer.h"
#include "gbuild.h"

// The number of iterations that an integer sort is performed
// (Burn in iterations are done first and are not timed)
#define NUM_ITERATIONS (1u)
#define BURN_IN (0u)

#define BARRIER_ATA

// Specifies if the all2all uses a per PE randomized target list
//#define PERMUTE
#define PRINT_MAX 64

#define ROOT_PE 0

// Needed for shmem collective operations
int pWrk[_SHMEM_REDUCE_MIN_WRKDATA_SIZE];
double dWrk[_SHMEM_REDUCE_SYNC_SIZE];
long long int llWrk[_SHMEM_REDUCE_MIN_WRKDATA_SIZE];
long pSync[_SHMEM_REDUCE_SYNC_SIZE];

int NUM_PES;    // Number of parallel workers
unsigned G_V;   // Vertices in the graph
unsigned G_E;   // Edges in the graph
unsigned V_PE;  // Each PE holds up to V_PE * V_PE edges
unsigned E_PE;

int XDIM_PES;
int YDIM_PES;

#define PE_X(_pe_) (_pe_ % XDIM_PES)
#define PE_Y(_pe_) (_pe_ / XDIM_PES)

#define XY_TO_PE(_x_, _y_) ((_y_ * XDIM_PES) + _x_)

#define E_TO_PE_X(_e_) ((_e_).x / BUCKET_WIDTH_X)
#define E_TO_PE_Y(_e_) ((_e_).y / BUCKET_WIDTH_Y)

#define EDGE_TO_PE(_e_) (XY_TO_PE( E_TO_PE_X(_e_), E_TO_PE_Y(_e_) ))

int NUM_BUCKETS;              // The number of buckets for edges = NUM_PES

unsigned BUCKET_WIDTH_X; // The size of each bucket in x dimension = G_V / XDIM_PES
unsigned BUCKET_WIDTH_Y; // The size of each bucket in y dimension = G_V / YDIM_PES

long unsigned BUCKET_TOTAL;

long long unsigned G_FILE_SIZE;

int DO_GEN_TUPLES;
int DO_CHECKPOINT = 0;

int snprintf_edgeplus(char *buf, size_t size, edge_t* e)
{
  int ret = snprintf_edge(buf, size, e);
  return ret + snprintf(buf + strlen(buf), size - ret, "-> PE (%d, %d) = %d\n",
			E_TO_PE_X(*e), E_TO_PE_Y(*e), EDGE_TO_PE(*e));
}


char *G_FILE;
char *LOG_FILE;

#define MAX_MSG 1024
char msg[MAX_MSG];

volatile int whose_turn;

typedef long long int lli_t;

lli_t receive_offset = 0;
lli_t my_tuples_len = 0;

typedef lli_t pe_hdr_t;

#define ISO_X 256
#define ISO_Y 512

#define E_TUPLE_SIZE (sizeof(edge_t))
//#define E_BUFFER_LEN (1uLL<<27uLL)
#define E_BUFFER_LEN (1uLL<<22uLL)
#define E_BUFFER_SIZE (E_BUFFER_LEN * E_TUPLE_SIZE)

#define FP_PER_PE_HDR (sizeof(pe_hdr_t))

#define FP_BYTES_PER_PE (E_BUFFER_SIZE + FP_PER_PE_HDR)

#define FP_HDR_OFFSET(_pe_)  (FP_BYTES_PER_PE * _pe_)
#define FP_TUPLES_OFFSET(_pe_) (FP_HDR_OFFSET(_pe_) + FP_PER_PE_HDR)

// The receive array for the All2All exchange
edge_t share_tuple_buffer[E_BUFFER_LEN];

#ifdef USING_SHMEMIO
void   *file_pe_hdr;
edge_t *file_bucket_tuples;
shmem_fspace_t fid;
shmem_fspace_conx_t conx;
int file_pe;
shmem_fp_t *kfp = NULL;

#else
int kfd = -1;

long pe_hdr_offset;
long bucket_tuples_offset;
#endif


#ifdef PERMUTE
int * permute_array;
#endif

int main(const int argc,  char ** argv)
{
  shmem_init();
  
  init_shmem_sync_array(pSync); 

  parse_params(argc, argv);

#ifdef USING_SHMEMIO
#ifdef DEBUG
  shmemio_set_loglvl("info");
#endif

  if (shmem_my_pe() == 0) {
    printf ("Connect to %s:%d\n", conx.storage_server_name, conx.storage_server_port);
  }
  
  fid = shmem_connect(&conx);

  if (fid == SHMEM_NULL_FSPACE) {
    printf ("Connect failed\n");
    return -1;
  }
#endif
  
  int err = bucket_sort();
  log_times(LOG_FILE);

#ifdef USING_SHMEMIO
  shmem_disconnect(fid);
#endif
  
  shmem_finalize();
  
  return err;
}


// Parses all of the command line input and definitions in params.h
// to set all necessary runtime values and options
static void parse_params(const int argc, char ** argv)
{
#ifdef USING_SHMEMIO
  const int uargc = 9;
#else
  const int uargc = 7;
#endif
  
  if(argc != uargc)
  {
    printf ("argc %d\n", argc);
    for (int idx = 0; idx < argc; idx++) {
      printf("%d: %s\n", idx, argv[idx]);
    }
	
    if( shmem_my_pe() == 0){
      printf("Usage:  \n");
#ifdef USING_SHMEMIO
      printf("\t./%s <xdim> <num edges> <num vertices> <log_file> <generate tuples> <tuple file> <shmemio hostname> <shmemio port>\n",argv[0]);
#else
      printf("\t./%s <xdim> <num edges> <num vertices> <log_file> <generate tuples> <tuple file>\n",argv[0]);
#endif
      printf("\t\t<generate tuples> = 0 to read/sort/writeback existing tuples in file. No new tuples created.\n");
      printf("\t\t<generate tuples> = 1 to create num_edges new tuples, sort them, and writeback to a new file\n");
      printf("\t\t<generate tuples> = 2 to create num_edges new tuples, and writeback to a new file without sorting\n");
      printf("\t\t<generate tuples> = -N to make N new tuples and write them into random locations in the file\n");
    }

    shmem_finalize();
    exit(1);
  }

  NUM_PES = (uint64_t) shmem_n_pes();
  NUM_BUCKETS = NUM_PES;  
  G_FILE_SIZE = FP_BYTES_PER_PE * NUM_PES;
  
  XDIM_PES = atoi(argv[1]);
  YDIM_PES = NUM_PES / XDIM_PES;
  
  if ((XDIM_PES * YDIM_PES) != NUM_PES) {
    printf ("x dimension %d is not divisor of npes %d\n", XDIM_PES, NUM_PES);
    exit(1);
  }
  
  LOG_FILE = argv[4];

  DO_GEN_TUPLES = atoi(argv[5]);
  G_FILE = argv[6];

#ifdef USING_SHMEMIO
  conx.storage_server_name = argv[7];
  conx.storage_server_port = atoi(argv[8]);
#endif

  char scaling_msg[64];

  switch(SCALING_OPTION){
    case STRONG:
      {
        G_E = atoi(argv[2]);
	G_V = atoi(argv[3]);
	
        V_PE = (G_V / NUM_PES) + ((G_V % NUM_PES > 0) ? 1 : 0);
	E_PE = (G_E / NUM_PES) + ((G_E % NUM_PES > 0) ? 1 : 0);

	BUCKET_WIDTH_X = (G_V / XDIM_PES) + ((G_V % XDIM_PES > 0) ? 1 : 0);
	BUCKET_WIDTH_Y = (G_V / YDIM_PES) + ((G_V % YDIM_PES > 0) ? 1 : 0);
	
        sprintf(scaling_msg,"STRONG");
        break;
      }

    case WEAK:
      {
	E_PE = atoi(argv[2]);
	V_PE = atoi(argv[3]);

	G_E = E_PE * NUM_PES;
	G_V = V_PE * NUM_PES;

	BUCKET_WIDTH_X = (G_V / XDIM_PES) + ((G_V % XDIM_PES > 0) ? 1 : 0);
	BUCKET_WIDTH_Y = (G_V / YDIM_PES) + ((G_V % YDIM_PES > 0) ? 1 : 0);
	
        sprintf(scaling_msg,"WEAK");
        break;
      }

    case WEAK_ISOBUCKET:
      {
	E_PE = atoi(argv[2]);
	G_E = E_PE * NUM_PES;
	
        BUCKET_WIDTH_X = ISO_X;
	BUCKET_WIDTH_Y = ISO_Y;

	V_PE = ISO_X * ISO_Y;
	G_V = V_PE * NUM_PES;
	
        sprintf(scaling_msg,"WEAK_ISOBUCKET");
        break;
      }

    default:
      {
        if(shmem_my_pe() == 0){
          printf("Invalid scaling option! See params.h to define the scaling option.\n");
        }

        shmem_finalize();
        exit(1);
        break;
      }
  }

  BUCKET_TOTAL = BUCKET_WIDTH_X * BUCKET_WIDTH_Y;

  if(shmem_my_pe() == 0){
#ifdef PERMUTE
    printf("Random Permute Used in ATA.\n");
#endif
    printf("  Generate tuples:\t%d\n", DO_GEN_TUPLES);
    printf("  Vertex per PE:\t%u\n", V_PE);
    printf("  Edges per PE:\t%u\n", E_PE);
    printf("  Total edges:\t %u\n", G_E);
    printf("  Total vertices:\t %u\n", G_V);
    printf("  Bucket Width X: %u\n", BUCKET_WIDTH_X);
    printf("  Bucket Width Y: %u\n", BUCKET_WIDTH_Y);
    printf("  Bucket Width Total: %lu\n", BUCKET_TOTAL);
    printf("  Number of Iterations: %u\n", NUM_ITERATIONS);
    printf("  Number of PEs: %d\n", NUM_PES);
    printf("  X Dim PEs: %d\n", XDIM_PES);
    printf("  Y Dim PEs: %d\n", YDIM_PES);
    printf("  Edge buffer len: %llu\n", E_BUFFER_LEN);
    printf("  Edge buffer size: %llu\n", E_BUFFER_SIZE);
    printf("  Bytes per per PE in file: %llu\n", FP_BYTES_PER_PE);
    printf("  Bytes in graph file: %llu\n", G_FILE_SIZE);
    printf("  %s Scaling!\n",scaling_msg);
  }
  fflush(stdout);

  if (E_PE >= E_BUFFER_LEN) {
    printf ("Edges per PE exceed buffer space\n");
    exit(1);
  }
}


/*
 * The primary compute function for the bucket sort
 * Executes the sum of NUM_ITERATIONS + BURN_IN iterations, as defined in params.h
 * Only iterations after the BURN_IN iterations are timed
 * Only the final iteration calls the verification function
 */
static int bucket_sort(void)
{
  int err = 0;

  init_timers(NUM_ITERATIONS);

#ifdef PERMUTE
  create_permutation_array();
#endif

  shmem_barrier_all();
  
  for(uint64_t i = 0; i < (NUM_ITERATIONS + BURN_IN); ++i)
  {

    // Reset timers after burn in 
    if(i == BURN_IN){
      init_timers(NUM_ITERATIONS);
      shmem_barrier_all();
    } 

    timer_start(&timers[TIMER_TOTAL]);

    edge_t* my_tuple_buffer = make_input();
    printf ("%d: done gen tuples\n", shmem_my_pe()); fflush(stdout);

    if (DO_GEN_TUPLES > 1) {
      output_and_barrier(my_tuple_buffer);
    }
    if ((DO_GEN_TUPLES < 0) || (DO_GEN_TUPLES > 1)) {
      i = NUM_ITERATIONS + BURN_IN;
      timer_stop(&timers[TIMER_TOTAL]);
      goto skip_sorting;
    }

    int * local_bucket_sizes =
      count_local_bucket_sizes(my_tuple_buffer);

   printf ("%d: done count local bucket\n", shmem_my_pe()); fflush(stdout);


    int * send_offsets;
    int * local_bucket_offsets =
      compute_local_bucket_offsets(local_bucket_sizes,
				   &send_offsets);

    edge_t * my_local_bucketed_tuples =
      bucketize_local_tuples(my_tuple_buffer,
			     local_bucket_offsets);

    exchange_tuples(send_offsets, 
		  local_bucket_sizes,
		  my_local_bucketed_tuples);

    printf ("%d: done exchange\n", shmem_my_pe()); fflush(stdout);

    my_tuples_len = receive_offset;
    
    int *my_local_tuple_counts = count_local_tuples();

    output_and_barrier(share_tuple_buffer);

    timer_stop(&timers[TIMER_TOTAL]);

    // Only the last iteration is verified
    if(i == NUM_ITERATIONS) {
      err = verify_results(my_local_tuple_counts, share_tuple_buffer);
    }

    // Reset receive_offset used in exchange_tuples
    receive_offset = 0;

    free(my_local_tuple_counts);
    free(my_local_bucketed_tuples);
    free(local_bucket_offsets);
    free(send_offsets);
    free(local_bucket_sizes);
  skip_sorting:    
    free(my_tuple_buffer);

    shmem_barrier_all();
  }

  return err;
}


/*
 * Generates uniformly random tuples [0, MAX_TUPLE_VAL] on each rank using the time and rank
 * number as a seed
 */
static inline edge_t* make_input(void)
{
  edge_t *my_tuple_buffer = malloc(E_BUFFER_SIZE);
  size_t new_tuples_len = 0;
  int me = shmem_my_pe();

#ifdef USING_SHMEMIO
  int err;

  if (kfp == NULL) {
    kfp = shmem_open(fid, G_FILE, G_FILE_SIZE, -1, -1, 1, -1, &err);
    if (kfp == NULL) {
      printf ("Failed to open file. Got NULL pointer. Error code is %d\n", err);
      exit(-1);
    }

#ifdef DEBUG
    //#if 1
    printf ("[%d]: fp returned is %p, addr=%lx, size=%lu, unit size=%d, pe [%d:%d] by %d\n",
	    me, kfp, (long unsigned)kfp->addr, (long unsigned)kfp->size, kfp->unit_size, kfp->pe_start,
	    kfp->pe_start + kfp->pe_size - 1, kfp->pe_stride);
#endif
    
    if (kfp->size < G_FILE_SIZE) {
      printf ("Opened file is wrong size, should be %lu\n", (long unsigned)(G_FILE_SIZE));
      exit(-1);
    }
  
    file_pe_hdr = (kfp->addr + (FP_HDR_OFFSET(me)));
    file_bucket_tuples = (edge_t*)(kfp->addr + (FP_TUPLES_OFFSET(me)));
    file_pe = kfp->pe_start;
  }

#ifdef DEBUG
  //#if 1
  printf ("[%d]: file_bucket_tuples lives at %p on pe %d\n", me, file_bucket_tuples, file_pe);
#endif
  
#else
  if (kfd < 0) {
    (void)G_FILE_SIZE;
    //kfd = open(G_FILE, O_SYNC | O_DIRECT | O_RDWR);
    kfd = open(G_FILE, O_RDWR);
    if (kfd < 0) {
      //kfd = open(G_FILE, O_SYNC | O_DIRECT | O_CREAT | O_RDWR, S_IRWXU);
      kfd = open(G_FILE, O_CREAT | O_RDWR, S_IRWXU);
      if (kfd < 0) {
	printf ("Failed to open or create file %s\n", G_FILE);
	exit(-1);
      }
    }
    pe_hdr_offset = (FP_HDR_OFFSET(me));
    bucket_tuples_offset = (FP_TUPLES_OFFSET(me));

    printf ("Open file %s with fd %d, pe_hdr_offset %u, bucket_tuples_offset %u\n",
	    G_FILE, kfd, (unsigned)pe_hdr_offset, (unsigned)bucket_tuples_offset);
  }
#endif

  timer_start(&timers[TIMER_INPUT]);
  
  if (DO_GEN_TUPLES <= 0) {
#ifdef USING_SHMEMIO
    shmem_getmem(&my_tuples_len, file_pe_hdr, sizeof(lli_t), file_pe);
    assert(my_tuples_len < (lli_t)E_BUFFER_LEN);
#else
    lseek(kfd, pe_hdr_offset, SEEK_SET);
    read(kfd, &my_tuples_len, sizeof(my_tuples_len));
    assert(my_tuples_len < (lli_t)E_BUFFER_SIZE);
#endif
  }

  if (DO_GEN_TUPLES == 0) {
#ifdef USING_SHMEMIO
    //printf ("Rank %d get %llu tuples at %p\n", shmem_my_pe(), my_tuples_len, file_bucket_tuples);
    shmem_getmem(my_tuple_buffer, file_bucket_tuples,
		 my_tuples_len * E_TUPLE_SIZE, file_pe);
#else
    read(kfd, my_tuple_buffer, my_tuples_len * E_TUPLE_SIZE);
#endif

  }
  
  if (DO_GEN_TUPLES >= 1) {
    my_tuples_len = E_PE;
    new_tuples_len = E_PE;
  }
  if (DO_GEN_TUPLES < 0) {
    new_tuples_len = -DO_GEN_TUPLES;
    assert(new_tuples_len < E_BUFFER_LEN);
  }

  if (new_tuples_len > 0) {

    pcg32_random_t rng;

    if (DO_GEN_TUPLES == 1)
      rng = seed_my_rank();
    else
      rng = seed_from_time();

    //printf ("Generate %lu new tuples into buffer size %llu\n", new_tuples_len, E_BUFFER_LEN);
    for(uint64_t i = 0; i < new_tuples_len; ++i) {
      my_tuple_buffer[i].x = pcg32_boundedrand_r(&rng, G_V);
      
      do {
	my_tuple_buffer[i].y = pcg32_boundedrand_r(&rng, G_V);
      } while (my_tuple_buffer[i].x == my_tuple_buffer[i].y);
      
      my_tuple_buffer[i].w = ( (double)pcg32_random_r(&rng) /
			       (double)pcg32_random_r(&rng) );
    }
  
    if (DO_GEN_TUPLES < 0) {
      //randomly put some new values into the data set
      for(uint64_t i = 0; i < new_tuples_len; ++i) {
	int r = pcg32_boundedrand_r(&rng, my_tuples_len);
	//printf("%d: r = %d\n", shmem_my_pe(), r);

#ifdef USING_SHMEMIO
	shmem_putmem(&(file_bucket_tuples[r]),
		     &(my_tuple_buffer[i]),
		     E_TUPLE_SIZE,
		     file_pe);
#else
	lseek(kfd, bucket_tuples_offset + (r * E_TUPLE_SIZE), SEEK_SET);
	write(kfd, &(my_tuple_buffer[i]), E_TUPLE_SIZE);
#endif
      }
      shmem_barrier_all();

#ifdef USING_SHMEMIO
      if (shmem_my_pe() == 0) {
	shmem_fp_flush(kfp, 0);
      }
#else
      fsync(kfd);
#endif
      
      shmem_barrier_all();
    }
  }
  
  timer_stop(&timers[TIMER_INPUT]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;
  const int my_rank = shmem_my_pe();
  if (new_tuples_len > 0) {
    msize -= snprintf(msg, msize, "Rank %d: New Tuples (%ld): ",
		      my_rank, new_tuples_len);
  
    for (size_t i = 0; (i < new_tuples_len) && (msize > 0); ++i){
      msize -= snprintf_edge(msg + strlen(msg), msize, &(my_tuple_buffer[i]));
    }
  }
  else if (my_tuples_len > 0) {
    msize -= snprintf(msg, msize, "Rank %d: My Tuples (%lld): ",
		      my_rank, my_tuples_len);
  
    for (lli_t i = 0; (i < my_tuples_len) && (msize > 0); ++i){
      msize -= snprintf_edge(msg + strlen(msg), msize, &(my_tuple_buffer[i]));
    }
  }
  printf("%s\n", msg);
  fflush(stdout);
  my_turn_complete();
#endif

  return my_tuple_buffer;
}


/*
 * Computes the size of each bucket by iterating all tuples and incrementing
 * their corresponding bucket's size
 */
static inline int *count_local_bucket_sizes(edge_t *my_tuple_buffer)
{
  int * local_bucket_sizes = malloc(NUM_BUCKETS * sizeof(int));

  timer_start(&timers[TIMER_BCOUNT]);

  memset(local_bucket_sizes, 0, NUM_BUCKETS * sizeof(int));

  for(lli_t i = 0; i < my_tuples_len; ++i) {
    const int bucket_index = EDGE_TO_PE(my_tuple_buffer[i]);

    if (bucket_index >= NUM_BUCKETS) {
      snprintf_edgeplus(msg, MAX_MSG, &(my_tuple_buffer[i]));
      printf ("%s\n", msg);
      fflush(stdout);
      shmem_barrier_all();
      sleep(1);
      assert(bucket_index < NUM_BUCKETS);
    }

    local_bucket_sizes[bucket_index]++;
  }

  timer_stop(&timers[TIMER_BCOUNT]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;
  const int my_rank = shmem_my_pe();
  msize -= snprintf(msg, msize, "Rank %d: local bucket sizes: ", my_rank);
  
  for(int i = 0; (i < NUM_BUCKETS) && (msize > 0); ++i){
    msize -= snprintf(msg + strlen(msg), msize, "%d ", local_bucket_sizes[i]);
  }
  printf("%s\n",msg);
  fflush(stdout);
  my_turn_complete();
#endif

  return local_bucket_sizes;
}


/*
 * Computes the prefix scan of the bucket sizes to determine the starting locations
 * of each bucket in the local bucketed array
 * Stores a copy of the bucket offsets for use in exchanging tuples because the
 * original bucket_offsets array is modified in the bucketize function
 */
static inline int * compute_local_bucket_offsets(int * local_bucket_sizes,
                                                 int ** send_offsets)
{
  const size_t offsets_size = NUM_BUCKETS * sizeof(int);
  int * local_bucket_offsets = malloc(offsets_size);
  (*send_offsets) = malloc(offsets_size);
  
  timer_start(&timers[TIMER_BOFFSET]);

  local_bucket_offsets[0] = 0;
  int temp = 0;
  for(int i = 1; i < NUM_BUCKETS; i++){
    temp = local_bucket_offsets[i-1] + local_bucket_sizes[i-1];
    local_bucket_offsets[i] = temp; 
  }
  memcpy(*send_offsets, local_bucket_offsets, offsets_size);
  
  timer_stop(&timers[TIMER_BOFFSET]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;
  const int my_rank = shmem_my_pe();
  msize -= snprintf(msg, msize, "Rank %d: local bucket offsets: ", my_rank);
  
  for(int i = 0; (i < NUM_BUCKETS) && (msize > 0); ++i){
    msize -= snprintf(msg + strlen(msg), msize, "%d ", local_bucket_offsets[i]);
  }
  printf("%s\n",msg);
  fflush(stdout);
  my_turn_complete();
#endif
  return local_bucket_offsets;
}

/*
 * Places local tuples into their corresponding local bucket.
 * The contents of each bucket are not sorted.
 */
static inline edge_t* bucketize_local_tuples(edge_t * my_tuple_buffer,
					     int * local_bucket_offsets)
{
  edge_t* my_local_bucketed_tuples
    = malloc(my_tuples_len * E_TUPLE_SIZE);

  timer_start(&timers[TIMER_BUCKETIZE]);

  for(lli_t i = 0; i < my_tuples_len; ++i) {
    
    const int bucket_index = EDGE_TO_PE(my_tuple_buffer[i]);
    
    assert(local_bucket_offsets[bucket_index] >= 0);
    const int index = local_bucket_offsets[bucket_index]++;
    assert(index < my_tuples_len);
    
    memcpy(&(my_local_bucketed_tuples[index]), &(my_tuple_buffer[i]), E_TUPLE_SIZE);
  }

  timer_stop(&timers[TIMER_BUCKETIZE]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;
  const int my_rank = shmem_my_pe();
  msize -= snprintf(msg, msize, "Rank %d: local bucketed tuples (%u): ",
		    my_rank, (unsigned)my_tuples_len);
  
  for(lli_t i = 0; (i < my_tuples_len) && (msize > 0); ++i){
    msize -= snprintf_edge(msg + strlen(msg), msize, &(my_local_bucketed_tuples[i]));
  }
  
  printf("%s\n", msg);
  fflush(stdout);
  my_turn_complete();
#endif
  
  return my_local_bucketed_tuples;
}


/*
 * Each PE sends the contents of its local buckets to the PE that owns that bucket.
 */
static inline void exchange_tuples(int * send_offsets,
				   int * local_bucket_sizes,
				   edge_t * my_local_bucketed_tuples)
{
  timer_start(&timers[TIMER_ATA_TUP]);

  const int my_rank = shmem_my_pe();
  unsigned int total_tuples_sent = 0;

  const lli_t write_offset_into_self =
    shmem_longlong_atomic_fetch_add(&receive_offset,
				    (lli_t)local_bucket_sizes[my_rank],
				    my_rank);

#ifdef USING_SHMEMIO
  if (DO_CHECKPOINT) {
    shmem_putmem(&(file_bucket_tuples[write_offset_into_self]), 
		 &(my_local_bucketed_tuples[send_offsets[my_rank]]), 
		 local_bucket_sizes[my_rank] * E_TUPLE_SIZE,
		 file_pe);
  }
  else
#endif
    // Tuples destined for local tuple buffer can be written with memcpy  
    memcpy(&(share_tuple_buffer[write_offset_into_self]), 
	   &(my_local_bucketed_tuples[send_offsets[my_rank]]), 
	   local_bucket_sizes[my_rank] * E_TUPLE_SIZE);
  
  for(int i = 0; i < NUM_PES; ++i){

#ifdef PERMUTE
    const int target_pe = permute_array[i];
#elif INCAST
    const int target_pe = i;
#else
    const int target_pe = (my_rank + i) % NUM_PES;
#endif

    // Local tuples already written with memcpy
    if(target_pe == my_rank){ continue; }

    const int read_offset_from_self = send_offsets[target_pe];
    const int my_send_size = local_bucket_sizes[target_pe];

    const lli_t write_offset_into_target =
      shmem_longlong_atomic_fetch_add(&receive_offset,
				      (lli_t)my_send_size,
				      target_pe);

#ifdef DEBUG
    printf("Rank: %d Target: %d Offset into target: %lld Offset into myself: %d Send Size: %d\n",
        my_rank, target_pe, write_offset_into_target, read_offset_from_self, my_send_size);
#endif

#ifdef USING_SHMEMIO
    if (DO_CHECKPOINT) {
      edge_t *target_bucket_tuples = (edge_t *)(kfp->addr + FP_TUPLES_OFFSET(target_pe));
      
      shmem_putmem(&(target_bucket_tuples[write_offset_into_target]), 
		    &(my_local_bucketed_tuples[read_offset_from_self]), 
		    my_send_size * E_TUPLE_SIZE, 
		    file_pe);
    }
    else
#endif
      shmem_putmem(&(share_tuple_buffer[write_offset_into_target]), 
		   &(my_local_bucketed_tuples[read_offset_from_self]), 
		   my_send_size * E_TUPLE_SIZE, 
		   target_pe);
    
    total_tuples_sent += my_send_size;
  }

#ifdef BARRIER_ATA
  shmem_barrier_all();
#endif

#ifdef USING_SHMEMIO
  if (DO_CHECKPOINT) {
    shmem_getmem(share_tuple_buffer,
		 file_bucket_tuples,
		 my_tuples_len * E_TUPLE_SIZE,
		 file_pe);
  }
#endif

  timer_stop(&timers[TIMER_ATA_TUP]);
  timer_count(&timers[TIMER_ATA_TUP], total_tuples_sent);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;

  msize -= snprintf(msg, msize,
		    "Rank %d: Bucket Size %lld | Total Tuples Sent: %u | Tuples after exchange:", 
		    my_rank, receive_offset, total_tuples_sent);
  
  for(lli_t i = 0; (i < receive_offset) && (msize > 0); ++i){
    msize -= snprintf_edge(msg + strlen(msg), msize, &(share_tuple_buffer[i]));
  }

  printf("%s\n",msg);
  fflush(stdout);
  my_turn_complete();
#endif
}


/*
 * Counts the occurence of each tuple in my bucket. 
 * Tuple indices into the count array are the tuple's value minus my bucket's 
 * minimum tuple value to allow indexing from 0.
 * my_tuples: All tuples in my bucket unsorted [my_rank * BUCKET_WIDTH, (my_rank+1)*BUCKET_WIDTH)
 */
static inline int * count_local_tuples()
{
  int * my_local_tuple_counts = malloc(BUCKET_TOTAL * sizeof(int));
  memset(my_local_tuple_counts, 0, BUCKET_TOTAL * sizeof(int));

  //printf ("%d: done malloc of %lu total\n", shmem_my_pe(), (long unsigned)BUCKET_TOTAL);
  fflush(stdout);
  
  timer_start(&timers[TIMER_SORT]);

  const int my_rank = shmem_my_pe();
  const int my_min_x = PE_X(my_rank) * BUCKET_WIDTH_X;
  const int my_min_y = PE_Y(my_rank) * BUCKET_WIDTH_Y;

  // Count the occurences of each tuple in my bucket
  for(lli_t i = 0; i < my_tuples_len; ++i) {
    const unsigned int index_x = share_tuple_buffer[i].x - my_min_x;
    const unsigned int index_y = share_tuple_buffer[i].y - my_min_y;
    const unsigned int index = (index_y * BUCKET_WIDTH_X) + index_x;

    assert(index < BUCKET_TOTAL);
    my_local_tuple_counts[index]++;
  }
  
  timer_stop(&timers[TIMER_SORT]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;

  msize -= snprintf(msg, msize, "Rank %d: Bucket Size %lld | Local Tuple Counts:", my_rank, my_tuples_len);
  
  for(uint64_t i = 0; (i < BUCKET_TOTAL) && (msize > 0); ++i){
    msize -= snprintf(msg + strlen(msg), msize, "%d ", my_local_tuple_counts[i]);
  }

  printf("%s\n", msg);
  fflush(stdout);
  my_turn_complete();
#endif

  return my_local_tuple_counts;
}

static inline void output_and_barrier(edge_t *my_tuples)
{
  timer_start(&timers[TIMER_OUTPUT]);
  
#ifdef USING_SHMEMIO
  printf ("output %lld tuples in %lld bytes to file using shmemio\n",
	  my_tuples_len, my_tuples_len * E_TUPLE_SIZE);
  shmem_putmem(file_pe_hdr, &my_tuples_len, sizeof(lli_t), file_pe);
  shmem_putmem(file_bucket_tuples, my_tuples, my_tuples_len * E_TUPLE_SIZE, file_pe);
#else
  printf ("output %lld tuples in %lld bytes to file using fileio at offset %lld\n",
	  my_tuples_len, my_tuples_len * E_TUPLE_SIZE, pe_hdr_offset);
  lseek(kfd, pe_hdr_offset, SEEK_SET);
  if (write(kfd, &my_tuples_len, sizeof(my_tuples_len) < 0)) {
    perror("File header write failed");
  }
  if (write(kfd, my_tuples, my_tuples_len * E_TUPLE_SIZE) < 0) {
    perror("File tuple write failed");
  }
#endif
  shmem_barrier_all();

#ifdef USING_SHMEMIO
  if (shmem_my_pe() == 0) {
    shmem_fp_flush(kfp, 0);
  }
  //shmem_close(kfp, 0);
#else
  fsync(kfd);
  close(kfd);
#endif

  shmem_barrier_all();
  timer_stop(&timers[TIMER_OUTPUT]);

#ifdef DEBUG
  wait_my_turn();
  int msize = MAX_MSG;
  const int my_rank = shmem_my_pe();
  
#ifdef USING_SHMEMIO
  msize -= snprintf(msg, msize, "Rank %d: put my_tuples_len %lld to address %p, pe %d: ", my_rank, my_tuples_len, file_pe_hdr, file_pe);
#else
  msize -= snprintf(msg, msize, "Rank %d: put my_tuples_len %lld to offset %u: ", my_rank, my_tuples_len, (unsigned)pe_hdr_offset);
#endif

  for(lli_t i = 0; (i < my_tuples_len) && (msize > 0); ++i){
    msize -= snprintf_edge(msg + strlen(msg), msize, &(my_tuples[i]));
  }
  printf("%s\n",msg);
  fflush(stdout);
  my_turn_complete();
#endif
  
}

/*
 * Verifies the correctness of the sort. 
 * Ensures all tuples are within a PE's bucket boundaries.
 * Ensures the final number of tuples is equal to the initial.
 */
static int verify_results(int * my_local_tuple_counts,
			  edge_t * my_local_tuples)
{

  shmem_barrier_all();

  int error = 0;

  const int my_rank = shmem_my_pe();
  const int pe_x = PE_X(my_rank);
  const int pe_y = PE_Y(my_rank);
  
  const unsigned my_min_x = pe_x * BUCKET_WIDTH_X;
  const unsigned my_max_x = (pe_x+1) * BUCKET_WIDTH_X;

  const unsigned my_min_y = pe_y * BUCKET_WIDTH_Y;
  const unsigned my_max_y = (pe_y+1) * BUCKET_WIDTH_Y;

  // Verify all tuples are within bucket boundaries
  for(lli_t i = 0; i < my_tuples_len; ++i) {
    edge_t *e = &(my_local_tuples[i]);
    
    if ( (e->x < my_min_x) || (e->x > my_max_x) ||
	 (e->y < my_min_y) || (e->y > my_max_y) ) {
      char buf[256];
      snprintf_edge(buf, 256, e);
      printf("Rank %d Failed Verification!\n", my_rank);
      printf("Tuple: %s is outside of bounds %d:%d, %d:%d\n",
	     buf, my_min_x, my_max_x, my_min_y, my_max_y);
      error = 1;
    }
  }

  // Verify the sum of the tuple population equals the expected bucket size
  lli_t bucket_size_test = 0;
  
  for(uint64_t i = 0; i < BUCKET_TOTAL; ++i){
    bucket_size_test +=  my_local_tuple_counts[i];
  }
  
  if(bucket_size_test != my_tuples_len){
      printf("Rank %d Failed Verification!\n",my_rank);
      printf("Actual Bucket Size: %lld Should be %lld\n",
	     bucket_size_test, my_tuples_len);
      error = 1;
  }
  
  printf("Rank %d Verify %lld tuples\n", my_rank, my_tuples_len);

  // Verify the final number of tuples equals the initial number of tuples
  static lli_t total_num_tuples = 0;
  
  shmem_longlong_sum_to_all(&total_num_tuples, &my_tuples_len, 1, 0, 0, NUM_PES, llWrk, pSync);
  
  shmem_barrier_all();

  if(total_num_tuples != (lli_t)(G_E)) {
    
    if(my_rank == ROOT_PE){
      printf("Verification Failed!\n");
      printf("Actual total number of tuples: %lld Expected %d\n",
	     total_num_tuples, G_E );
      error = 1;
    }
  }

  return error;
}

/*
 * Gathers all the timing information from each PE and prints
 * it to a file. All information from a PE is printed as a row in a tab seperated file
 */
static void log_times(char * log_file)
{
  FILE * logfp = NULL;

  for(uint64_t i = 0; i < TIMER_NTIMERS; ++i){
    timers[i].all_times = gather_rank_times(&timers[i]);
    timers[i].all_counts = gather_rank_counts(&timers[i]);
  }

  if(shmem_my_pe() == ROOT_PE)
  {
    int print_names = 0;
    if(file_exists(log_file) != 1){
      print_names = 1;
    }

    if((logfp = fopen(log_file, "a+b"))==NULL){
      perror("Error opening log file:");
      exit(1);
    }

    if(print_names == 1){
      print_run_info(logfp);
      print_timer_names(logfp);
    }
    print_timer_values(logfp);

    report_summary_stats();

    fclose(logfp);
  }

}

/*
 * Computes the average total time and average all2all time and prints it to the command line
 */
static void report_summary_stats(void)
{
  
  if(timers[TIMER_TOTAL].seconds_iter > 0) {
    const uint32_t num_records = NUM_PES * timers[TIMER_TOTAL].seconds_iter;
    double temp = 0.0;
    for(uint64_t i = 0; i < num_records; ++i){
      temp += timers[TIMER_TOTAL].all_times[i];
    }
      printf("Average total time (per PE): %f seconds\n", temp/num_records);
  }

  if(timers[TIMER_ATA_TUP].seconds_iter >0) {
    const uint32_t num_records = NUM_PES * timers[TIMER_ATA_TUP].seconds_iter;
    double temp = 0.0;
    for(uint64_t i = 0; i < num_records; ++i){
      temp += timers[TIMER_ATA_TUP].all_times[i];
    }
    printf("Average all2all time (per PE): %f seconds\n", temp/num_records);
  }
}

/*
 * Prints all the labels for each timer as a row to the file specified by 'fp'
 */
static void print_timer_names(FILE * fp)
{
  for(uint64_t i = 0; i < TIMER_NTIMERS; ++i){
    if(timers[i].seconds_iter > 0){
      fprintf(fp, "%14s(s)\t", timer_names[i]);
    }
    if(timers[i].count_iter > 0){
      fprintf(fp, "%14s(#)\t", timer_names[i]);
    }
  }
  fprintf(fp,"\n");
}

/*
 * Prints all of the timining information for an individual PE as a row
 * to the file specificed by 'fp'. 
 */
static void print_timer_values(FILE * fp)
{
  unsigned int num_records = NUM_PES * NUM_ITERATIONS; 

  for(uint64_t i = 0; i < num_records; ++i) {
    for(int t = 0; t < TIMER_NTIMERS; ++t){
      if(timers[t].all_times != NULL){
        fprintf(fp,"%17.6f\t", timers[t].all_times[i]);
      }
      if(timers[t].all_counts != NULL){
        fprintf(fp,"%17.6u\t", timers[t].all_counts[i]);
      }
    }
    fprintf(fp,"\n");
  }
}

/*
 * Prints all the relevant runtime parameters as a row to the file specified by 'fp'
 */
static void print_run_info(FILE * fp)
{
  fprintf(fp,"SHMEM\t");
  fprintf(fp,"NUM_PES %d\t", NUM_PES);
  fprintf(fp,"Num_Iters %u\t", NUM_ITERATIONS);

  switch(SCALING_OPTION){
    case STRONG: {
      fprintf(fp,"Strong Scaling: %u total tuples\t", G_E);
        break;
      }
    case WEAK: {
        fprintf(fp,"Weak Scaling: %u tuples per PE\t", E_PE);
        break;
      }
    case WEAK_ISOBUCKET: {
        fprintf(fp,"Weak Scaling Constant Bucket Width: %u tuples per PE \t", E_PE);
        fprintf(fp,"Constant Bucket Width X: %u\t", BUCKET_WIDTH_X);
        fprintf(fp,"Constant Bucket Width Y: %u\t", BUCKET_WIDTH_Y);
        break;
      }
    default:
      {
        fprintf(fp,"Invalid Scaling Option!\t");
        break;
      }

  }

#ifdef PERMUTE
    fprintf(fp,"Randomized All2All\t");
#elif INCAST
    fprintf(fp,"Incast All2All\t");
#else
    fprintf(fp,"Round Robin All2All\t");
#endif

    fprintf(fp,"\n");
}



/* 
 * Aggregates the per PE timing information
 */ 
static double * gather_rank_times(_timer_t * const timer)
{
  if(timer->seconds_iter > 0) {

    assert(timer->seconds_iter == timer->num_iters);

    const unsigned int num_records = NUM_PES * timer->seconds_iter;
    double * my_times = shmem_malloc(timer->seconds_iter * sizeof(double));
    memcpy(my_times, timer->seconds, timer->seconds_iter * sizeof(double));

    double * all_times = shmem_malloc( num_records * sizeof(double));

    shmem_barrier_all();

    shmem_fcollect64(all_times, my_times, timer->seconds_iter, 0, 0, NUM_PES, pSync);
    shmem_barrier_all();

    shmem_free(my_times);

    return all_times;
  }
  else{
    return NULL;
  }
}

/*
 * Aggregates the per PE timing 'count' information 
 */
static unsigned int * gather_rank_counts(_timer_t * const timer)
{
  if(timer->count_iter > 0){
    const unsigned int num_records = NUM_PES * timer->num_iters;

    unsigned int * my_counts = shmem_malloc(timer->num_iters * sizeof(unsigned int));
    memcpy(my_counts, timer->count, timer->num_iters*sizeof(unsigned int));

    unsigned int * all_counts = shmem_malloc( num_records * sizeof(unsigned int) );

    shmem_barrier_all();

    shmem_collect32(all_counts, my_counts, timer->num_iters, 0, 0, NUM_PES, pSync);

    shmem_barrier_all();

    shmem_free(my_counts);

    return all_counts;
  }
  else{
    return NULL;
  }

}

/*
 * Initializes the work array required for SHMEM collective functions
 */
static void init_shmem_sync_array(long * restrict const pSync)
{
  for(uint64_t i = 0; i < _SHMEM_REDUCE_SYNC_SIZE; ++i){
    pSync[i] = _SHMEM_SYNC_VALUE;
  }
  shmem_barrier_all();
}

/*
 * Tests whether or not a file exists. 
 * Returns 1 if file exists
 * Returns 0 if file does not exist
 */
static int file_exists(char * filename)
{
  struct stat buffer;

  if(stat(filename,&buffer) == 0){
    return 1;
  }
  else {
    return 0;
  }

}

#ifdef DEBUG
static void wait_my_turn()
{
  shmem_barrier_all();
  whose_turn = 0;
  shmem_barrier_all();
  const int my_rank = shmem_my_pe();

  shmem_int_wait_until((int*)&whose_turn, SHMEM_CMP_EQ, my_rank);
  sleep(1);

}

static void my_turn_complete()
{
  const int my_rank = shmem_my_pe();
  const int next_rank = my_rank+1;

  if(my_rank < (int)(NUM_PES-1)){ // Last rank updates no one
    shmem_int_put((int *) &whose_turn, &next_rank, 1, next_rank);
  }
  shmem_barrier_all();
}
#endif

#ifdef PERMUTE
/*
 * Creates a randomly ordered array of PEs used in the exchange_tuples function
 */
static void create_permutation_array()
{

  permute_array = (int *) malloc( NUM_PES * sizeof(int) );

  for(int i = 0; i < NUM_PES; ++i){
    permute_array[i] = i;
  }

  shuffle(permute_array, NUM_PES, sizeof(int));
}

/*
 * Randomly shuffles a generic array
 */
static void shuffle(void * array, size_t n, size_t size)
{
  char tmp[size];
  char * arr = array;
  size_t stride = size * sizeof(char);
  if(n > 1){
    for(size_t i = 0; i < (n - 1); ++i){
      size_t rnd = (size_t) rand();
      size_t j = i + rnd/(RAND_MAX/(n - i) + 1);
      memcpy(tmp, arr + j*stride, size);
      memcpy(arr + j*stride, arr + i*stride, size);
      memcpy(arr + i*stride, tmp, size);
    }
  }
}
#endif

