#include "opt-synchprobs.h"
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <test.h>
#include <thread.h>

#include "paintshop_driver.h"


/*
 * DEFINE THIS MACRO TO SWITCH ON MORE PRINTING
 *
 * Note: Your solution should work whether printing is on or off
 *
 */

/* #define PRINT_ON */

/* this semaphore is for cleaning up at the end. */
static struct semaphore *alldone;

/*
 * Data type used to track number of doses each tint performs
 */

struct paint_tint {
        int doses;
};

struct paint_tint paint_tints[NCOLOURS];

static int customers;
static struct lock *cust_lock;

/* A function used to manage staff leaving */

static void go_home(void);

/*
 * **********************************************************************
 * CUSTOMERS
 *
 * Customers are rather simple, they arrive with their paint can,
 * write their requested colour on the can, submit their can as an
 * order to the paint shop staff and wait.
 *
 * Eventually their can returns with the requested contents (exactly
 * as requested), they paint until the can is empty, take a short
 * break, and do it all again until they have emptied the desired
 * number of cans.
 *
 * Lastly, they indicate to the paint shop staff that they have
 * finished for the day by calling go_home()
 *
 */

static void customer(void *unusedpointer, unsigned long customernum)
{
        struct paintorder order;
        int i,j;

        (void) unusedpointer; /* avoid compiler warning */

        order.go_home_flag = 0;

        i = 0; /* count number of iterations */
        do {


#ifdef PRINT_ON
                kprintf("C %ld is ordering\n", customernum);
#endif

                /* erase ingredients list on can and select a colour in terms of tints */
                for (j = 0; j < PAINT_COMPLEXITY; j++) {
                        order.requested_tints[j] = 0;
                }
                order.requested_tints[0] = RED;
                /* order the paint, this blocks until the order is filled */
                order_paint(&order);


#ifdef PRINT_ON
                kprintf("C %ld painting with the following %d, %d, %d\n",
                        customernum,
                        order.can.contents[0],
                        order.can.contents[1],
                        order.can.contents[2]);
#endif

                /* empty the paint can */
                for (j = 0; j < PAINT_COMPLEXITY; j++) {
                        order.can.contents[j] = 0;
                }


                /* I needed that break.... */
                thread_yield();

                i++;
        } while (i < 10); /* keep going until .... */

#ifdef PRINT_ON
        kprintf("C %ld going home\n", customernum);
#else
        (void)customernum;
#endif

        /*
         * Now we go home.
         */
        go_home();
        V(alldone);
}


/*
 * **********************************************************************
 * PAINT SHOP STAFF
 *
 * paint shop staff are only slightly more complicated than the customers.
 * They take_orders, and if valid, they fill them and serve them.
 * When all the customers have left, the staff members go home.
 *
 * An invalid (NULL pointer) order signals that the staffer should go
 * home.
 *
 */

static void paintshop_staff(void *unusedpointer, unsigned long staff)
{

        struct paintorder *order;
        int i;
        (void)unusedpointer; /* avoid compiler warning */

        i = 0; /* count orders filled for stats */
        while (1) {

#ifdef PRINT_ON
                kprintf("S %ld taking order\n", staff);
#endif

                order = take_order();

                if (order->go_home_flag == 0) {

#ifdef PRINT_ON
                        kprintf("S %ld filling\n", staff);
#endif


                        i++;
                        fill_order(order);

#ifdef PRINT_ON
                        kprintf("S %ld serving\n", staff);
#endif

                        serve_order(order);
                } else {
                        /* Immediately return the order without filling, and then go home */
                        serve_order(order);
                        break;
                }

        };

        kprintf("S %ld going home after mixing %d orders\n", staff, i);
        V(alldone);
}


/*
 * **********************************************************************
 * RUN THE PAINT SHOP
 *
 * This routine sets up the paint shop prior to opening and cleans up
 * after closing.
 *
 * It calls two routines (paintshop_open() and paintshop_close() in
 * paintshop.c) in which you can insert your own initialisation code.
 *
 * It also prints some statistics at the end.
 *
 */

int run_paintshop(int nargs, char **args)
{
        int i, result;

        (void) nargs; /* avoid compiler warnings */
        (void) args;

        /* this semaphore indicates everybody has gone home */
        alldone = sem_create("alldone", 0);
        if (alldone==NULL) {
                panic("run_paintshop: out of memory\n");
        }

        /* initialise the tint doses to 0 */
        for (i =0 ; i < NCOLOURS; i++) {
                paint_tints[i].doses = 0;
        }

        /* initialise the count of customers and create a lock to
           facilitate updating the counter by multiple threads */
        customers = NCUSTOMERS;

        cust_lock = lock_create("cust lock");
        if (cust_lock == NULL) {
                panic("not memory");
        }

        /**********************************************************************
         * call your routine that initialises the rest of the paintshop
         */
        paintshop_open();

        /* Start the paint shop staff */
        for (i=0; i<NPAINTSHOPSTAFF; i++) {
                result = thread_fork("paint shop staff thread", NULL,
                                     &paintshop_staff, NULL, i);
                if (result) {
                        panic("run_paintshop: thread_fork failed: %s\n",
                              strerror(result));
                }
        }

        /* Start the customers */

        for (i=0; i<NCUSTOMERS; i++) {
          result = thread_fork("customer thread", NULL,
                               &customer, NULL, i);
                if (result) {
                        panic("run_paintshop: thread_fork failed: %s\n",
                              strerror(result));
                }
        }

        /* Wait for everybody to finish. */
        for (i=0; i< NCUSTOMERS+NPAINTSHOPSTAFF; i++) {
                P(alldone);
        }

        for (i =0 ; i < NCOLOURS; i++) {
                kprintf("Tint %d used for %d doses\n", i+1,
                        paint_tints[i].doses);
        }

        /***********************************************************************
         * Call your paint shop clean up routine
         */
        paintshop_close();

        lock_destroy(cust_lock);
        sem_destroy(alldone);
        kprintf("The paint shop is closed, bye!!!\n");
        return 0;
}



/*
 * **********************************************************************
 * MIX
 *
 * This function takes a can with a tint list and mixes the
 * tints together as required. It does it such that the contents
 * EXACTLY matches the requested tints.
 *
 * Yes, mix counts double and triple servings of the same tint.
 *
 * MIX NEEDS THE ROUTINE THAT CALLS IT TO ENSURE THAT MIX HAS EXCLUSIVE
 * ACCESS TO THE TINTS IT NEEDS. And ideally, only exclusive access to
 * the tints that are required in the mix.
 *
 * YOU MUST USE THIS MIX FUNCTION TO FILL PAINT CANS. We use it for
 * testing when marking.
 */

void mix(struct paintorder *order)
{
        int i;

        /* add tints to can in order given and increment number of
           doses from particular tint */

        for (i = 0; i < PAINT_COMPLEXITY;i++){
                int col;
                col = order->requested_tints[i];
                order->can.contents[i] = col;

                if (col > NCOLOURS) {
                        panic("Unknown colour");
                }
                if (col > 0) {
                        paint_tints[col-1].doses++;
                }
        }
}

/*
 * go_home()
 *
 * This function is called by customers when they go home. It is used
 * to keep track of the number of remaining customers to allow paint
 * shop staff threads to exit when no customers remain.
 */


static void go_home(void)
{

        lock_acquire(cust_lock);
        customers --;
        lock_release(cust_lock);

        /* the last customer to leave tells the staff to go home */
        if (customers == 0) {
                struct paintorder go_home_order;
                int i;

                go_home_order.go_home_flag = 1;

                for (i = 0; i < NPAINTSHOPSTAFF; i++) {
                        order_paint(&go_home_order); /* returns without order being filled */
                }
        }
}


