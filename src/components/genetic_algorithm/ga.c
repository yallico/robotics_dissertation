#include "ga.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "https.h"
#include "cJSON.h"
#include "esp_log.h"
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "globals.h"
#include "data_logging.h"

static const char *TAG = "GA";

//TODO: handle error seed
//Used to initialize the random seed
uint16_t seed = 0; //0 error code

// Used to help analyse performance, difference in time.
unsigned long dt;

// This 2D array is all our candidate GA solutions.
// 1st element: number of candidate solutions (pop_size)
// 2nd element: number of dimensions of problem (problem parameters)
float population[ POP_SIZE ][ MAX_GENES ];

float fitness[ POP_SIZE ];  // To store fitness per solution.
                            // Index correlates to population[][] index so
                            // avoid re-ordering either array or they will
                            // become mismatched/misaligned.

int rank[ POP_SIZE ];       // Used to optimise a sorting routine on fitness.
                            // Once createRanking() is called, then:
                            // rank[0] provides index to population[][] for the
                            // current worst population member, and rank[1] the
                            // second worst population member, etc.
                            // So rank[POP_SIZE -1] is the best current solution.
                            // rank[POP_SIZE-1] stores the INDEX of this solution
                            // in the population[][] array.

float true_f[ POP_SIZE ];   // Our roulette selection works as to optimise to maximum.
                            // However, rastrigin is a minimisation problem.  Therefore,
                            // we take the reciprocal of the rastrigin (1 + (1/rastrigin)) to 
                            // "convert" it into a maximisation problem.  This is a little
                            // confusing to review/debug though.  Therefore, we store the 
                            // original rastrigin value into this array just for 
                            // reviewing/debugging later.  

// Utility function to generate a random floating-point number within a range
float randFloat(float min, float max) {
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

/*
 *  From: http://www.taygeta.com/random/gaussian.html
 *  
 *  This routine is a little troubling because it is 
 *  non-deterministic (we don't know when it will solve)
 *  and computationally expensive.
 *  However, using gaussian distribution is useful for 
 *  GAs to create an often-small mutation with occassional
 *  big mutation.  Uniform random numbers don't do this.
 */
float randGaussian(float mean, float sd) {
    float x1, x2, w, y;

    do {
        x1 = 2.0 * randFloat(0.0,1.0) - 1.0; // Generate uniform random value between -1 and 1
        x2 = 2.0 * randFloat(0.0,1.0) - 1.0; // Generate uniform random value between -1 and 1
        w = (x1 * x1) + (x2 * x2);
    } while (w >= 1.0); // Ensure that the point is inside the unit circle

    w = sqrt((-2.0 * log(w)) / w); // Compute scaling factor
    y = x1 * w; // y is normally distributed
    return mean + y * sd; // Scale by standard deviation and adjust by mean
}

// This is a standard selection mechanism, plenty of 
// resources to read on this.
// https://en.wikipedia.org/wiki/Fitness_proportionate_selection
// Note, we have preconditioned our fitness (rastrigin) to
// convert it to a maximisation problem so this routine works.
int rouletteSelection(void) {
    float sum_fitness = 0.0;
    int i;

    // Calculate the sum of all fitness values
    for (i = 0; i < POP_SIZE; i++) {
        sum_fitness += fitness[i];
    }
    
    // Generate a random stopping point along the cumulative sum of fitnesses
    float stop = randFloat(0.0, sum_fitness);
    
    // Move through each fitness and end when the value is
    // greater or equal to the stop position
    float cumulative_sum = 0.0;
    for (i = 0; i < POP_SIZE; i++) {
        cumulative_sum += fitness[i];
        if (cumulative_sum >= stop) {
            return i;
        }
    }

    // In the unlikely event that floating-point precision leads to not selecting
    // simply return the last index (should technically never happen)
    return POP_SIZE - 1;
}

// The fitness function is key to any GA.
// The rastrigin function is implemented here.
// Rastrigin is a minimisation problem, but our
// roulette method selection works to maximise.
// Therefore, we set the fitness (f) as the reciprocal of
// the rastrigin function, f = (1 / rastrigin_fitness).
// However, if rastrigin solves (e.g rastrigin_fitness = 0)
// we would get an error of (1/0), so we add an offset
// to the denominator, creating f = (1 / (rastrigin_fitness+1) ).
void determineFitness() {
    float f;

    for (int individual = 0; individual < POP_SIZE; individual++) {
        // For each population member, determine the succes (fitness).
        // Here, we just sum each value
        f = 0;
        float p0;
        float p1;
        float f_sum = 0.0;

        for (int gene = 0; gene < MAX_GENES; gene++) {
            float geneValue = population[individual][gene];
            // Rastrigin Function:
            p0 = pow(geneValue, 2);
            p1 = A * cos(TWO_PI * geneValue);
            f_sum += (p0 - p1);
        }

        f = A * MAX_GENES + f_sum;

        // Store the original Rastrigin value
        // Before we take the reciprocal to convert this to
        // a maximisation problem, we store the original 
        // rastrigin fitness.  This is what we really want to
        // see when we review the results.
        true_f[individual] = f;
        
        // Avoid division by zero
        f = 1.0 / (f + 1.0);

        // Assign calculated fitness
        fitness[individual] = f;
    }
}

// Using a simple bubble sort to determine the ranking of each
// individual in the population. Note that it is computationally
// expensive to reorder large arrays of numbers. Therefore, this
// function re-orders the index values stored in the rank array.
// That way we avoid copying around arrays.
void createRanking(void) {
    int i;

    // We don't want to affect the actual assignment
    // of fitness per individual, so here we create 
    // a duplicate array.
    float temp_f[POP_SIZE];

    // Set initial ranking into unsorted index order
    for (i = 0; i < POP_SIZE; i++) {
        rank[i] = i;
        temp_f[i] = fitness[i];
    }
    
    int sort;
    int genotype;

    // Perform bubble sort on the temporary fitness array
    for (sort = 0; sort < POP_SIZE; sort++) {
        for (genotype = 0; genotype < POP_SIZE - 1; genotype++) {
            // Compare this genotype to the next
            if (temp_f[genotype] > temp_f[genotype + 1]) {
                // Swap rank indices
                int rank_hold = rank[genotype + 1];
                rank[genotype + 1] = rank[genotype];
                rank[genotype] = rank_hold;

                // Swap fitness values in the temporary array
                float fitn_hold = temp_f[genotype + 1];
                temp_f[genotype + 1] = temp_f[genotype];
                temp_f[genotype] = fitn_hold;
            }
        }
    }
}

uint16_t init_random_seed(void) {
    // Utility to initialize random seed
    ESP_LOGI(TAG, "Fetching random seed from QRNG@ANU");
    http_response_t response = {
        .data = NULL,
        .len = 0
    };

    const char* url = "https://qrng.anu.edu.au/API/jsonI.php?length=1&type=uint16";
    esp_err_t result = https_get(url, &response, qrng_anu_ca_crt_start);
    if (result == ESP_OK && response.data != NULL) {
        printf("Received data: %s\n", response.data);
        
        // Parsing the JSON response to extract the seed
        cJSON *json = cJSON_Parse(response.data);
        if (json == NULL) {
            ESP_LOGE(TAG, "JSON parsing error");
        } else {
            cJSON *data = cJSON_GetObjectItem(json, "data");
            if (cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
                seed = (uint16_t)cJSON_GetArrayItem(data, 0)->valueint;
                ESP_LOGI(TAG, "Random Seed: %u", seed);
            }
            cJSON_Delete(json);
        }
    } else {
        ESP_LOGE(TAG, "Failed to fetch seed: %s", esp_err_to_name(result));
    }

    // Clean up
    if (response.data) {
        free(response.data);
    }

    return seed;
}

static void init_population(void) {
    // Utility to randomize initial population
    ESP_LOGI(TAG, "Randomizing initial population");
    uint16_t seed = init_random_seed();
    if (seed == 0) {
        ESP_LOGE(TAG, "Failed to fetch random seed");
        return;
    }

    srand(seed);

    // Calculate range for gene values
    float range = MAX_GENE_VALUE - MIN_GENE_VALUE;

    // Initialise with a random uniform distribution across all population members and genes
    for (int individual = 0; individual < POP_SIZE; individual++) {
        for (int gene = 0; gene < MAX_GENES; gene++) {
            // Generate random float within [MIN_GENE_VALUE, MAX_GENE_VALUE]
            float randomValue = (float)rand() / (float)RAND_MAX; // Normalized to [0, 1]
            population[individual][gene] = MIN_GENE_VALUE + randomValue * range;
        }

        // Set initial fitness to zero
        fitness[individual] = 0.0;
        rank[individual] = 0;
    }

    // Optionally determine fitness after initialization
    determineFitness();
}

void print_population(void) {
    ESP_LOGI(TAG, " "); // space our reporting to make it easier to see

    for (int individual = 0; individual < POP_SIZE; individual++) {
        char buffer[1024];  // Increase size if necessary
        char *ptr = buffer; // Pointer for the buffer

        ptr += sprintf(ptr, ":%d: ", individual);
        for (int gene = 0; gene < MAX_GENES; gene++) {
            ptr += sprintf(ptr, "%.2f,", population[individual][gene]);  // Using %.2f to format to 2 decimal places
        }
        ptr += sprintf(ptr, " F: %.2f (inverted: %.6f)", true_f[individual], fitness[individual]);  // Format true fitness and inverted fitness

        ESP_LOGI(TAG, "%s", buffer); // Log the complete individual info
    }
}

void print_ranking(void) {
    ESP_LOGI(TAG, "In rank order, 0 = worst, pop_size = best");

    for (int r = 0; r < POP_SIZE; r++) {
        ESP_LOGI(TAG, "Rank %d, Individual %d (F: %.6f, inverted: %.6f)",
                 r, rank[r], true_f[rank[r]], fitness[rank[r]]);
    }
}

void evolve(void) {
    // Apply Rastrigin to each candidate solution
    // to determine their "fitness"
    determineFitness();

    // Rank outcomes - note that the array rank[]
    // is sorted, which itself has the index of
    // population[][]
    createRanking();

    // We need to create a new temporary population so that
    // as we draw from the old population, we don't overwrite
    // the information with new children.  For example, if we
    // replace the worst population members with new children,
    // there are instances when a worst member may be used to 
    // generate a child.  Therefore, we don't want to 
    // prematurely overwrite a worst member.
    // Once we have a set of new children, we copy them back 
    // into the main population.
    
    // How many children as a percentage of the population?
    int how_many = (int)(POP_SIZE * PERCENT_CHILD);
    // Create a new array of children
    float children[how_many][MAX_GENES];

    // Generate 'how many' children
    for (int i = 0; i < how_many; i++) {
        // Select a parent.
        int parent1 = rouletteSelection();
        // Recombination.  
        // Here, two parents are used to generate
        // a single child offspring.  Could be all
        // of parent1, all of parent2, or a mix.
        // All parent1 by default:
        for (int gene = 0; gene < MAX_GENES; gene++) {
            children[i][gene] = population[parent1][gene];
        }
        // Do recombination?
        if ((double)rand() / RAND_MAX < XOVER_PROB) {
            // We need a second parent
            int parent2 = rouletteSelection();
            // How much of parent2 to inherit?
            // Select a point along the genotype [ 0 : MAX_GENES ]
            int xover = rand() % MAX_GENES;
            for (int gene = xover; gene < MAX_GENES; gene++) {
                children[i][gene] = population[parent2][gene];
            }
        } // end of xover

        // Mutation, evaluated per gene
        for (int gene = 0; gene < MAX_GENES; gene++) {
            if ((double)rand() / RAND_MAX < MUTATE_PROB) {
                // +=, but can be +/- mutation
                children[i][gene] += randGaussian(0.0, 0.05);
                // Limit range.  Another way would be to wrap around.
                if (children[i][gene] > MAX_GENE_VALUE) children[i][gene] = MAX_GENE_VALUE;
                if (children[i][gene] < MIN_GENE_VALUE) children[i][gene] = MIN_GENE_VALUE;
            }
        } // end of mutate
    } // Finished generating children

    // Insert back into population by rank[0] (worst) up to 'how many'.
    // Note, using rank[] which has sorted the index of the population
    // into rank order, rank[0] is the index of the worst individual
    // and rank[ pop_size -1 ] is the best individual.
    // Therefore, this routine will copy children over the worst population
    // members going up (improving) - keeping the current best solutions.
    for (int i = 0; i < how_many; i++) {
        // Per population member _i_, copy each gene across.
        for (int gene = 0; gene < MAX_GENES; gene++) {
            // Note, indexing population by the rank array.
            // Note, the children generated are essentially 
            // random so we don't care about a rank order for
            // children.
            population[rank[i]][gene] = children[i][gene];
        }
    }
}

void init_ga(void) {
    // Initialize the GA population
    init_population();
    print_population();
}

//Task function that the main application can call
void ga_task(void *pvParameters) {
    float last_best_fitness = -1.0;  // Init impossible fitness value
    float threshold = 0.001;  // Threshold for detecting significant changes in fitness
    while (1) {
        evolve();  // Run GA
        if (experiment_ended) {
            break;
        }
        // Prints the best true fitness of the population in this
        // generation to 3 decimal places.
        // Rastrigin is a minimisation problem.
        // So we should see this descending towards 0
        // true_f[ ] = our store of original fitness values
        // rank[ POP_SIZE -1 ] returns the index of the best population member
        float current_best_fitness = true_f[rank[POP_SIZE - 1]];  // Get the current best fitness
        if (fabs(current_best_fitness - last_best_fitness) > threshold) { // Print only if there's a change
            ESP_LOGI(TAG, "Best true fitness: %.3f", current_best_fitness);
            last_best_fitness = current_best_fitness;  // Update last known best fitness
        }

        vTaskDelay(100);
    }
    print_population();  // Print final population
    vTaskDelete(NULL);  // Delete task when done
}
