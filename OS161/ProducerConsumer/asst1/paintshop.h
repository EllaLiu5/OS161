#ifndef PAINTSHOP_H
#define PAINTSHOP_H
#include <synch.h>

#include "paintcan.h"

/*
 * You are free to add anything you think you require to this file,
 * with the exceptions noted below.
 */


/* struct paintorder is the main type referred to in the code. It must
   be preserved as noted for our later testing to work */

struct paintorder {
        unsigned int requested_tints[PAINT_COMPLEXITY]; /* Do not change */
        int go_home_flag;                               /* Do not change */
        struct paintcan can;                            /* Do not change */

        /* This struct can be extended with your own entries below here */ 
		struct semaphore* lock;
};

#endif
