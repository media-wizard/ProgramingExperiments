
#ifndef TASK_RUNNER_H_
#define TASK_RUNNER_H_

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <list>

// Keep the order of enum types.
// Add new types above `Self` and update RunnerName in the same order.
enum class RunnerType : int {
  Main = 0,     //Identifier of main threads's TaskRunner - no separate thread.
  Io,
  Utility,
  Self          //Identifier of own TaskRunner - no separate thread.
};

const std::string RunnerName[] = {
    "Main",
    "Io",
    "Utility"
};

class Application {
  public:
    virtual bool Initialize() = 0;
};

class TaskRunner {
public:
  TaskRunner(const std::string &name);

  ~TaskRunner();

  void PostTask(std::packaged_task<void()> &task);

  void PostDelayedTask(std::packaged_task<void()> &task, unsigned int delayMs);

  void Run();

  void ShutDown();

  static TaskRunner* GetTaskRunner(RunnerType type);

private:
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using TaskObject = std::pair<TimePoint, std::packaged_task<void()>>;

  class Compare {
    public:
      bool operator()(TaskObject &below, TaskObject &above) const;
  };

  std::string name_;
  bool running_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::list<TaskObject> pending_tasks_;
};

class RunnerManager {
  public:
    ~RunnerManager();
    void BootStrap(Application &app);

  private:
    void StartThread(int type);
};

#endif // TASK_RUNNER_H_
