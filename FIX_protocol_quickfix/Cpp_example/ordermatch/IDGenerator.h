
#ifndef ORDERMATCH_IDGENERATOR_H
#define ORDERMATCH_IDGENERATOR_H

#include <sstream>
#include <string>

class IDGenerator {
public:
  IDGenerator()
      : m_orderID(0),
        m_executionID(0) {}

  std::string genOrderID() { return std::to_string(++m_orderID); }

  std::string genExecutionID() { return std::to_string(++m_executionID); }

private:
  long m_orderID;
  long m_executionID;
};

#endif
