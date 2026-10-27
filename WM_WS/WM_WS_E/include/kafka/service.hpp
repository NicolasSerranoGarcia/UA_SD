#pragma once

#include <memory>
#include <string>

class Engine;

namespace kafka {

// Keeps broker waits off the HCP thread and journals undelivered events locally.
class Service {
  public:
    Service(Engine &engine, std::string endpoint, std::string stateFile);
    ~Service();
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    bool startLocal();
    void stopLocal();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kafka
