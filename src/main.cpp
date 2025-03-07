#include <iostream>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>

using namespace std;

// Constants for number of tickers and orders per side per ticker.
const int MAX_TICKERS = 1024;
const int MAX_ORDERS_PER_SIDE = 1024; // maximum orders for buy or sell side per ticker

// Order struct using atomics where needed for lock-free updates.
struct Order {
    atomic<bool> active;    // whether order is still active (not fully matched)
    bool isBuy;             // true for Buy, false for Sell
    atomic<int> quantity;   // remaining quantity to be matched
    int price;              // order price (set once at insertion)
    
    Order() : active(false), isBuy(false), quantity(0), price(0) {}
};

// OrderBook for one ticker – separate arrays for buys and sells.
struct OrderBook {
    Order buyOrders[MAX_ORDERS_PER_SIDE];
    atomic<int> buyCount;    // next free index for buy orders
    Order sellOrders[MAX_ORDERS_PER_SIDE];
    atomic<int> sellCount;   // next free index for sell orders

    OrderBook() {
        buyCount.store(0, memory_order_relaxed);
        sellCount.store(0, memory_order_relaxed);
        // orders already initialized by Order’s constructor.
    }
};

// Global array of order books (one per ticker).
OrderBook orderBooks[MAX_TICKERS];

// A simple hash function to map ticker symbols (C-strings) to an index between 0 and MAX_TICKERS-1.
int hashTicker(const char* ticker) {
    unsigned int hash = 0;
    while(*ticker) {
        hash = hash * 31 + static_cast<unsigned int>(*ticker++);
    }
    return hash % MAX_TICKERS;
}

// addOrder: Adds an order to the corresponding ticker's order book in a lock-free way.
void addOrder(const char* orderType, const char* ticker, int quantity, int price) {
    int idx = hashTicker(ticker);
    OrderBook &ob = orderBooks[idx];

    if (strcmp(orderType, "Buy") == 0) {
        int pos = ob.buyCount.fetch_add(1, memory_order_relaxed);
        if (pos >= MAX_ORDERS_PER_SIDE) {
            // Order book for buys is full; order is dropped.
            return;
        }
        ob.buyOrders[pos].active.store(true, memory_order_relaxed);
        ob.buyOrders[pos].isBuy = true;
        ob.buyOrders[pos].quantity.store(quantity, memory_order_relaxed);
        ob.buyOrders[pos].price = price;
    } else if (strcmp(orderType, "Sell") == 0) {
        int pos = ob.sellCount.fetch_add(1, memory_order_relaxed);
        if (pos >= MAX_ORDERS_PER_SIDE) {
            // Order book for sells is full; order is dropped.
            return;
        }
        ob.sellOrders[pos].active.store(true, memory_order_relaxed);
        ob.sellOrders[pos].isBuy = false;
        ob.sellOrders[pos].quantity.store(quantity, memory_order_relaxed);
        ob.sellOrders[pos].price = price;
    }
}

// matchOrder: For a given ticker, scans orders in O(n) time to match the best Buy and Sell orders.
void matchOrder(const char* ticker) {
    int idx = hashTicker(ticker);
    OrderBook &ob = orderBooks[idx];

    int bestBuyIndex = -1;
    int bestSellIndex = -1;
    int bestBuyPrice = 0;
    int bestSellPrice = 1000000; // arbitrarily high initial value

    // Scan through buy orders to find the one with the highest price.
    int currentBuyCount = ob.buyCount.load(memory_order_relaxed);
    for (int i = 0; i < currentBuyCount; i++) {
        if (ob.buyOrders[i].active.load(memory_order_relaxed)) {
            int q = ob.buyOrders[i].quantity.load(memory_order_relaxed);
            if (q > 0 && ob.buyOrders[i].price > bestBuyPrice) {
                bestBuyPrice = ob.buyOrders[i].price;
                bestBuyIndex = i;
            }
        }
    }

    // Scan through sell orders to find the one with the lowest price.
    int currentSellCount = ob.sellCount.load(memory_order_relaxed);
    for (int i = 0; i < currentSellCount; i++) {
        if (ob.sellOrders[i].active.load(memory_order_relaxed)) {
            int q = ob.sellOrders[i].quantity.load(memory_order_relaxed);
            if (q > 0 && ob.sellOrders[i].price < bestSellPrice) {
                bestSellPrice = ob.sellOrders[i].price;
                bestSellIndex = i;
            }
        }
    }

    // Check if there is a match condition: Buy price >= Sell price.
    if (bestBuyIndex != -1 && bestSellIndex != -1 && bestBuyPrice >= bestSellPrice) {
        // Determine the matched quantity (the minimum of both order quantities).
        int buyQty = ob.buyOrders[bestBuyIndex].quantity.load(memory_order_relaxed);
        int sellQty = ob.sellOrders[bestSellIndex].quantity.load(memory_order_relaxed);
        int matchedQuantity = min(buyQty, sellQty);

        // Atomically update quantities. (For demonstration, using relaxed updates.)
        ob.buyOrders[bestBuyIndex].quantity.fetch_sub(matchedQuantity, memory_order_relaxed);
        ob.sellOrders[bestSellIndex].quantity.fetch_sub(matchedQuantity, memory_order_relaxed);

        // If an order’s quantity is reduced to 0, mark it inactive.
        if (ob.buyOrders[bestBuyIndex].quantity.load(memory_order_relaxed) == 0) {
            ob.buyOrders[bestBuyIndex].active.store(false, memory_order_relaxed);
        }
        if (ob.sellOrders[bestSellIndex].quantity.load(memory_order_relaxed) == 0) {
            ob.sellOrders[bestSellIndex].active.store(false, memory_order_relaxed);
        }

        // Output the match for logging purposes.
        cout << "Matched " << matchedQuantity << " shares for ticker " << ticker 
             << " (Buy @ " << bestBuyPrice << " vs. Sell @ " << bestSellPrice << ")" << endl;
    }
}

// Simulation wrapper that randomly executes addOrder (and calls matchOrder) to simulate active trading.
void simulateOrders(int numOrders) {
    // For simulation, we use a small set of example tickers.
    const char* tickers[] = {"AAPL", "GOOG", "MSFT", "AMZN", "FB", "TSLA", "NFLX", "NVDA"};
    int numTickers = sizeof(tickers) / sizeof(tickers[0]);

    for (int i = 0; i < numOrders; i++) {
        // Randomly select order type.
        const char* orderType = (rand() % 2 == 0) ? "Buy" : "Sell";
        // Randomly choose a ticker.
        const char* ticker = tickers[rand() % numTickers];
        // Random quantity between 1 and 1000.
        int quantity = rand() % 1000 + 1;
        // Random price between 10 and 500.
        int price = rand() % 491 + 10;

        addOrder(orderType, ticker, quantity, price);
        // Immediately attempt to match orders for this ticker.
        matchOrder(ticker);
    }
}

int main() {
    srand(static_cast<unsigned int>(time(NULL)));
    const int numThreads = 4;
    const int ordersPerThread = 10000;
    vector<thread> threads;

    // Launch multiple threads to simulate concurrent order insertion and matching.
    for (int i = 0; i < numThreads; i++) {
        threads.push_back(thread(simulateOrders, ordersPerThread));
    }
    for (auto &t : threads) {
        t.join();
    }
    return 0;
}
