/*
 * **********************************************************************
 *
 * Define function prototypes, types, and constants needed by both the
 * driver (paintshop_driver.c) and the code you need to write
 * (paintshop.c)
 *
 * YOU SHOULD NOT RELY ON ANY CHANGES YOU MAKE TO THIS FILE
 *
 * We will use our own version of this file for testing
 */

#include "paintcan.h"
#include "paintshop.h"


extern int run_paintshop(int, char **);

/*
 * FUNCTION PROTOTYPES FOR THE FUNCTIONS YOU MUST WRITE
 *
 * YOU CANNOT MODIFY THESE PROTOTYPES
 *  
 */

/* Customer functions */
extern void order_paint(struct paintorder *);


/* Paintshop staff functions */ 
extern struct paintorder * take_order(void);
extern void fill_order(struct paintorder *);
extern void serve_order(struct paintorder *);


/* Paintshop  opening and closing functions */
extern void paintshop_open(void);
extern void paintshop_close(void);


/*
 * Function prototype for the supplied routine that mixes the various
 * paint tints into a can.
 *
 * YOU MUST USE THIS FUNCTION FOR MIXING
 *
 */
extern void mix(struct paintorder *);


/*
 * THE FOLLOWING PARAMETERS WILL BE CHANGED BY US, so you should test
 * various combinations. NOTE: We will only ever set these to
 * something greater than zero.
 */ 


#define NCUSTOMERS 5     /* The number of customers painting today*/
#define NPAINTSHOPSTAFF 3 /* The number of paint shop staff working today */

