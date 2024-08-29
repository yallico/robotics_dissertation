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

static const char *TAG = "GA";

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

void run_ga(void) {
    // Core logic to evolve your GA
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

uint16_t init_random_seed(void) {
    // Utility to initialize random seed
    ESP_LOGI(TAG, "Fetching random seed from QRNG@ANU");
    http_response_t response = {
        .data = NULL,
        .len = 0
    };
    //TODO: handle error seed
    uint16_t seed = 0; //error code

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

void init_ga(void) {
    // Initialize your GA
    init_population();
    print_population();
}

// A task function that the main application can call
// void ga_task(void *pvParameters) {
//     init_ga();  // Initialize GA
//     while (1) {
//         run_ga();  // Run GA
//         if (/* condition to stop the GA */) {
//             break;
//         }
//         vTaskDelay(100 / portTICK_PERIOD_MS);  // Delay as needed
//     }
//     print_population();  // Print final population
//     vTaskDelete(NULL);  // Delete task when done
// }
