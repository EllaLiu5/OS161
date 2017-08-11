#ifndef PAINTCAN_H
#define PAINTCAN_H
/*
 * **********************************************************************
 *
 * YOU SHOULD NOT RELY ON ANY CHANGES YOU MAKE TO THIS FILE
 *
 * We will use our own version of this file for testing
 */



/*
 * Define the number of paint tinting colours available and their
 * symbolic constants. 
 *
 */
#define BLUE      1
#define GREEN     2
#define YELLOW    3
#define MAGENTA   4
#define ORANGE    5
#define CYAN      6
#define BLACK     7
#define RED       8
#define WHITE     9
#define BROWN    10
#define NCOLOURS 10


/*
 * The maximum number of tints that can be mixed in a single can
 */

#define PAINT_COMPLEXITY 3

/*
 * The data type representing a paintcan 
 */ 
struct paintcan {
        /* the actual contents of the can */
        unsigned int contents[PAINT_COMPLEXITY];
};

#endif
