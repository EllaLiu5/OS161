#include <types.h>
#include <lib.h>
#include <synch.h>
#include <test.h>
#include <thread.h>

#include "paintshop.h"
#include "paintshop_driver.h"


/*
 * **********************************************************************
 * YOU ARE FREE TO CHANGE THIS FILE BELOW THIS POINT AS YOU SEE FIT
 *
 */

/* Declare any globals you need here (e.g. locks, etc...) */
#define ORDER_BUFFER_SIZE 10


#define TRUE 1
#define FALSE 0
#define MAX 10

//extern static int customers;

static struct semaphore *order_mutex, *order_empty, *order_full;

static struct lock *tints_lock[MAX];

static const char* colours[10] = {"blue","green","yellow","magenta","orange","cyan","black","red","white","brown"};

struct{
	struct paintorder* elements[ORDER_BUFFER_SIZE];
	int first; /* buf[first%BUFF_SIZE] is the first empty slot  */
	int last; //buf[last%BUFF_SIZE] is the first full slot
}order_buffer;


/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY CUSTOMER THREADS
 * **********************************************************************
 */

/*
 * order_paint()
 *
 * Takes one argument referring to the order to be filled. The
 * function makes the order available to staff threads and then blocks
 * until the staff have filled the can with the appropriately tinted
 * paint.
 */
void order_paint(struct paintorder *order)
{

	P(order_empty);
	P(order_mutex);
	
//	order_buffer.elements[order_buffer.first] = order;
	struct paintorder* po = kmalloc (sizeof (struct paintorder));
	memcpy (po, order, sizeof (struct paintorder));
	order_buffer.elements [order_buffer.first] = po;
	po->lock = order->go_home_flag?NULL:
		sem_create ("DAVE SENDS REGARDS",0); 
	order_buffer.first = (order_buffer.first + 1) % ORDER_BUFFER_SIZE;
	V(order_mutex);
	V(order_full);


	if((*order).go_home_flag == 1)
		return;
	P(po->lock);
	sem_destroy (po->lock);	
	memcpy (&order->can, &po->can, sizeof (struct paintcan));
	kfree (po);
}



/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY PAINT SHOP STAFF THREADS
 * **********************************************************************
 */

/*
 * take_order()
 *
 * This function waits for a new order to be submitted by
 * customers. When submitted, it returns a pointer to the order.
 *
 */



struct paintorder *take_order(void)
{
	P(order_full);
	P(order_mutex);

	struct paintorder *ret = order_buffer.elements[order_buffer.last];
	order_buffer.last = (order_buffer.last + 1) % ORDER_BUFFER_SIZE;

	V(order_mutex);
	V(order_empty);
	return ret;

}


/*
 * fill_order()
 *
 * This function takes an order provided by take_order and fills the
 * order using the mix() function to tint the paint.
 *
 * NOTE: IT NEEDS TO ENSURE THAT MIX HAS EXCLUSIVE ACCESS TO THE
 * REQUIRED TINTS (AND, IDEALLY, ONLY THE TINTS) IT NEEDS TO USE TO
 * FILL THE ORDER.
 */

void fill_order(struct paintorder *order)
{
    /* add any sync primitives you need to ensure mutual exclusion
           holds as described */
        unsigned *list = order->requested_tints;
        /*sorting ascending order*/
		
        int temp;
        int i,j;
        bool swapped = false;
		


        for(i = 0; i < PAINT_COMPLEXITY-1; i++) 
        { 
            swapped = false;
            for(j = 0; j < PAINT_COMPLEXITY-1-i; j++) {           
                if(list[j] > list[j+1]) {
                    temp = list[j];
                    list[j] = list[j+1];
                    list[j+1] = temp;
                    swapped = true;
                }    
            }
            if(!swapped) {
                break;
            }
        }

        /* try to get the tint from 1 to 10 */
        for (int i = 0; i < PAINT_COMPLEXITY; ++i)
        {
            if (list[i]!=0)
		{lock_acquire(tints_lock[list[i]-1]);}
        }
        /* release tints after use */
        mix(order);
        for (int i = 0; i < PAINT_COMPLEXITY; ++i)
        {
            if (list[i]!=0)
		{lock_release(tints_lock[list[i]-1]);}
        }
}


/*
 * serve_order()
 *
 * Takes a filled order and makes it available to (unblocks) the
 * waiting customer.
 */

void serve_order(struct paintorder *order)
{
	if((*order).go_home_flag){	
		return;
	}

	V (order->lock);	
}



/*
 * **********************************************************************
 * INITIALISATION AND CLEANUP FUNCTIONS
 * **********************************************************************
 */


/*
 * paintshop_open()
 *
 * Perform any initialisation you need prior to opening the paint shop
 * to staff and customers. Typically, allocation and initialisation of
 * synch primitive and variable.
 */


void paintshop_open(void)
{
	order_mutex = sem_create("order_mutex", 1);
	order_empty = sem_create("order_empty", ORDER_BUFFER_SIZE);
	order_full = sem_create("order_full", 0);

	for (int i = 0; i < MAX; ++i){
    	tints_lock[i] = lock_create(colours[i]);
	}

}

/*
 * paintshop_close()
 *
 * Perform any cleanup after the paint shop has closed and everybody
 * has gone home.
 */

void paintshop_close(void)
{
	memset (&order_buffer, 0, sizeof (order_buffer));
	sem_destroy(order_mutex);
	sem_destroy(order_empty);
	sem_destroy(order_full);
	for (int i = 0; i < MAX; ++i)
	{
		lock_destroy(tints_lock[i]);
	}
}
