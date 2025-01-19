enum order_type
{
    SELL,
    BUY
};

struct limit_order
{
    order_type type;
    int price;
};

struct market_order
{
    order_type type;
};
