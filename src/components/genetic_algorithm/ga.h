#ifndef GA_H
#define GA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> 
#include <stdbool.h>

// Constants
#define POP_SIZE        30      // How many candidate solutions to evaluate.
                                // More = better search, but slower to compute
                                // Less = harder for algorithm to find solution
                                
#define MAX_GENES       3       // Dimensions (higher = more difficult)
                                // This is the number of parameters to be 
                                // represented in the "genotype" (candidate
                                // solution).

#define MAX_GENE_VALUE  5.12    // To use the Rastrigin Function as a problem, 
#define MIN_GENE_VALUE  -5.12   // we use values [-5.12 : +5.12]

// Mutation can be thought of as the "fine grained" search.
// A candidate solution is located in the search-space somewhere,
// and the mutation will just move it a small amount in that
// local area.
// Generally speaking, we want to do this small, local search
// more often.
#define MUTATE_PROB     0.6     // range [0:1], how often do we apply mutation?
#define MUTATE_WIDTH    0.05    // gaussian standard dev
#define MUTATE_MEAN     0.0     // gaussian center

// Cross-over takes two candidate solutions, chops them in half,
// and glues them back together.  This represents a coarse search,
// or large jumps in the search space.  This is very disruptive, but
// has theoretical advantage to get out of local maxima/minima.
// Therefore, applied less frequently than mutation.
#define XOVER_PROB      0.1     // range [0:1]


// This GA is using a steady-state evoluation. This means that for
// each generation, a percentage of the previous generation is kept
// without modification.  This GA also adopts an "elistist" approach,
// carrying over only the best candidates.  Below is the percentage
// of new children, therefore the elite carry-over is 1 - PERCENT_CHILD
#define PERCENT_CHILD   0.7     // range [0:1] 
                                // e.g. 1 = total replacement
                                //      0 = no children (no evolution)

// A is a part of the rastrigin function
// which we are using here as a fitness function
// (how well can the GA minimise rastrigin to 0)
// https://en.wikipedia.org/wiki/Rastrigin_function
#define A 10.0

// Global variables
extern const uint8_t qrng_anu_ca_crt_start[] asm("_binary_qrng_anu_ca_pem_start");
extern const uint8_t qrng_anu_ca_crt_end[] asm("_binary_qrng_anu_ca_pem_end");
//extern volatile bool experiment_ended;
extern uint16_t seed;

// Interface functions
void init_ga(void);
void print_population(void);
void print_ranking(void);
void ga_task(void *pvParameters);  // Expose the task function for external use

#ifdef __cplusplus
}
#endif

#endif // GA_H