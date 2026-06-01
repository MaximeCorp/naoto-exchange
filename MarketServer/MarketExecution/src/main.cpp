#include <MatchingEngine.hpp>
#include <Order.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace MarketExecution;

std::string ToEscapedString(const void *data, size_t size)
{
    std::stringstream ss;
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i)
    {
        ss << "\\x" << std::hex << std::setw(2) << std::setfill('0')
           << (int)bytes[i];
    }
    return ss.str();
}

int main(void)
{
    MatchingEngine<16, 10, 100, 1000> engine(16, 100, 0, 5, 8080, 16, 16, 128,
                                             128);

    char key[25] = { 'a' };

    Order order1(key, OrderType::LIMIT, OrderSide::BUY, 100, 1, 60, 0, 1);

    Order order13(key, OrderType::LIMIT, OrderSide::BUY, 90, 1, 60, 0, 1);

    Order order12(key, OrderType::LIMIT, OrderSide::BUY, 200, 1, 60, 0, 1);

    Order order2(key, OrderType::MARKET, OrderSide::SELL, 2, 50, 0, 1);

    Order order3(key, OrderType::MARKET, OrderSide::SELL, 2, 60, 0, 1);

    std::cout << ToEscapedString(&order1, sizeof(Order)) << "\n";
    std::cout << ToEscapedString(&order13, sizeof(Order)) << "\n";
    std::cout << ToEscapedString(&order12, sizeof(Order)) << "\n";
    std::cout << ToEscapedString(&order2, sizeof(Order)) << "\n";
    std::cout << ToEscapedString(&order3, sizeof(Order)) << "\n";

    engine.StartMatchingEngine();
}
