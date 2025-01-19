#include "order.h"

// Asset structure
struct asset
{
    char *name;
    unsigned int total_quantity;

    // List of limit orders ordered by price (index 0 is next order to be executed), insertion sort for new orders
    limit_order *bid;
    limit_order *ask;
};
