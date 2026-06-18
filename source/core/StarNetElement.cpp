#include "StarNetElement.hpp"


namespace Star {

uint64_t NetElementVersion::current() const {
  return m_version;
}

uint64_t NetElementVersion::increment() {
  return ++m_version;
}

void NetElementVersion::markChanged() const {
  m_latestChange = m_version;
}

uint64_t NetElementVersion::latestChange() const {
  return m_latestChange;
}

namespace NetElementEarlyOut {
  std::atomic<bool> enabled{false};
  std::atomic<bool> validate{false};
}

void NetElement::enableNetInterpolation(float) {}

void NetElement::disableNetInterpolation() {}

void NetElement::tickNetInterpolation(float) {}

void NetElement::blankNetDelta(float) {}

}
