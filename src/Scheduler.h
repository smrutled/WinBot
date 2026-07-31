#ifndef WINBOT_SCHEDULER_H
#define WINBOT_SCHEDULER_H
#include "Common.h"
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>

// ── Scheduler
// ───────────────────────────────────────────────────────────────── Parses cron
// expressions and fires tasks at the right time. Uses std::jthread (C++20) for
// the background worker — auto-joins on destruction.
class Scheduler {
public:
  struct TaskDef {
    std::string name;
    std::string cronExpr; // "Min Hour Day Month Weekday" (5 fields)
    std::string prompt;   // The task prompt to inject into the agent
  };

  using TaskCallback = std::function<void(const std::string &prompt)>;

  // callback is called on any thread when a task fires
  explicit Scheduler(TaskCallback callback);
  ~Scheduler();

  void addTask(TaskDef task);
  void removeTask(std::string_view name);
  [[nodiscard]] std::vector<TaskDef> listTasks() const;

private:
  std::vector<TaskDef> m_tasks;
  mutable std::mutex m_mutex;
  TaskCallback m_callback;
  std::jthread m_thread; // C++20 — auto-joins on destruction

  void workerLoop(std::stop_token stopToken);

  // Returns true if the given time matches the cron expression
  [[nodiscard]] static bool cronMatches(const std::string &expr,
                                        const std::tm &t) noexcept;

  // Parse a cron field (e.g. "*/5", "8", "*") against a value
  [[nodiscard]] static bool cronFieldMatches(const std::string &field,
                                             int value, int min,
                                             int max) noexcept;
};

#endif // WINBOT_SCHEDULER_H
