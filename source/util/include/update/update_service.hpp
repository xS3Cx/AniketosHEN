#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace onion::update {

struct Status {
  enum class State { Idle, Running, Ok, Error } state = State::Idle;
  std::uint32_t task_id = 0;
  std::string phase;
  std::string error;
  std::string version;
  std::string installed;
  std::string summary;
  std::string channel;
  int progress = -1;
  std::uint64_t size = 0;
  bool newer = false;
  bool downloaded = false;
};

class Service {
public:
  enum class StartResult { Started, AlreadyRunning, Rejected };

  static Service &instance();

  StartResult check(const std::string &channel, bool notify,
                    std::uint32_t *task_id = nullptr);
  StartResult download(const std::string &channel,
                       std::uint32_t *task_id = nullptr);
  bool cancel(std::uint32_t task_id);
  Status status() const;

  void worker_check(std::string channel, bool notify, std::uint32_t task_id);
  void worker_download(std::string channel, std::uint32_t task_id);
  void note_progress(const char *phase, std::uint64_t completed,
                     std::uint64_t total);
  bool cancellation_requested() const;

  Service(const Service &) = delete;
  Service &operator=(const Service &) = delete;

private:
  Service();
  ~Service();

  enum class JobKind { Check, Download };
  StartResult start(JobKind kind, std::string channel, bool notify,
                    std::uint32_t *task_id);
  void finish(Status::State state, const std::string &error);

  mutable std::mutex mu_;
  Status status_{};
  bool running_ = false;
  bool cancel_requested_ = false;
  std::uint32_t next_task_id_ = 0;
};

void start_boot_check();

} // namespace onion::update
