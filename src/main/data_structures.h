#ifndef _DATA_STRUCTURES_H
#define _DATA_STRUCTURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> 

#define MAX_ROBOTS 20  //maximum number of robots

typedef struct {
    int num_robots;
    uint16_t robot_ids[MAX_ROBOTS];
    char *data_link;    // Nullable string
    char *routing;      // Nullable string
    int msg_limit;  
    char *com_type;     // Nullable string
    int msg_size_bytes;
    int robot_speed;
} experiment_metadata_t;



#ifdef __cplusplus
}
#endif

#endif 
