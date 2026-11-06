#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace onion::payload_center {

struct Status {
  enum class State { Idle, Running, Ok, Error } state = State::Idle;
  uint32_t task_id = 0;
  std::string phase;
  std::string error;
  std::string item;
  int progress = -1;
  uint64_t completed = 0;
  uint64_t total = 0;
};

class Service {
public:
  enum class StartResult { Started, AlreadyRunning, Rejected };

  static Service &instance();

  StartResult refresh(const std::string &source_id, uint32_t *task_id = nullptr);
  StartResult install(const std::string &source_id,
                      const std::string &item_id, bool replace,
                      uint32_t *task_id = nullptr);
  StartResult install_url(const std::string &url, bool replace,
                          uint32_t *task_id = nullptr);
  bool cancel(uint32_t task_id);
  Status status() const;

  void worker_refresh(std::string source_id, uint32_t task_id);
  void worker_install(std::string source_id, std::string item_id,
                      std::string url, bool replace, uint32_t task_id);
  void note_progress(const char *phase, uint64_t completed, uint64_t total);
  bool cancellation_requested() const;

  Service(const Service &) = delete;
  Service &operator=(const Service &) = delete;

private:
  Service();
  ~Service();

  enum class JobKind { Refresh, Install };
  StartResult start(JobKind kind, std::string source_id, std::string item_id,
                    std::string url, bool replace, uint32_t *task_id);
  void finish(Status::State state, const std::string &error);

  mutable std::mutex mu_;
  Status status_{};
  bool running_ = false;
  bool cancel_requested_ = false;
  uint32_t next_task_id_ = 0;
};

}
