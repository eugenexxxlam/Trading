
#ifndef ORDERMATCH_MARKET_H
#define ORDERMATCH_MARKET_H

#include "Order.h"
#include <functional>
#include <map>
#include <queue>
#include <string>

class Market {
public:
  bool insert(const Order &order);
  void erase(const Order &order);
  Order &find(Order::Side side, std::string id);
  bool match(std::queue<Order> &);
  void display() const;

private:
  typedef std::multimap<double, Order, std::greater<double>> BidOrders;
  typedef std::multimap<double, Order, std::less<double>> AskOrders;

  void match(Order &bid, Order &ask);

  std::queue<Order> m_orderUpdates;
  BidOrders m_bidOrders;
  AskOrders m_askOrders;
};

#endif
