#include "task_runner.h"

namespace { // anonymous namespace - start
  std::vector<std::thread> g_runner_threads;

  std::vector<std::unique_ptr<TaskRunner>> g_task_runners;

  thread_local TaskRunner *self_task_runner_tls = nullptr;
} // anonymous namespace - end

TaskRunner::TaskRunner(const std::string &name)
    : name_(name),
      running_(true) {
}

TaskRunner::~TaskRunner() {
  std::cout << "Destroying TaskRunner[" << name_ << "]\n";
  ShutDown();
}

void TaskRunner::PostTask(std::packaged_task<void()> &task) {
  PostDelayedTask(task, 0);
}

void TaskRunner::PostDelayedTask(std::packaged_task<void()> &task,
                                 unsigned int delay) {
  TimePoint task_start_time = std::chrono::steady_clock::now()
                                            + std::chrono::milliseconds(delay);
  const std::lock_guard<std::mutex> lock(mtx_);
  pending_tasks_.insert(
      std::lower_bound(
          pending_tasks_.begin(), pending_tasks_.end(), task_start_time,
          [](const TaskObject &listed_task, const TimePoint &new_start_time) {
            return listed_task.first < new_start_time;
          }),
      TaskObject(task_start_time, std::move(task)));

  cv_.notify_one();
}

void TaskRunner::Run() {
  while (running_) {
    TaskObject task_to_run;
    {
      // std::cout << "Thread[" << name_ <<"] looking for task to execute.\n";
      std::unique_lock lock(mtx_);

      size_t task_cnt = pending_tasks_.size();

      if (task_cnt == 0) {
        cv_.wait(lock, [&]() {
                    return (pending_tasks_.size() > task_cnt) || !running_; });
        continue;
      } else {
        TimePoint now = std::chrono::steady_clock::now();
        TimePoint first_task_time = pending_tasks_.front().first;

        if (first_task_time > now) {
          cv_.wait_until(lock, first_task_time, [&]() {
                    return (pending_tasks_.size() > task_cnt) || !running_; });
          continue;
        }
      }
      
      task_to_run.swap(pending_tasks_.front());
      pending_tasks_.pop_front();
    }
    // std::cout << "Running task in Thread[" << name_ <<"]\n";
    task_to_run.second();
  }
}

void TaskRunner::ShutDown() {
  if (running_) {
    std::cout << "Shutting down TaskRunner[" << name_ << "]\n";
    running_ = false;
    {
      const std::lock_guard<std::mutex> lock(mtx_);
      cv_.notify_one();
    }
  }
}

TaskRunner *TaskRunner::GetTaskRunner(RunnerType type) {
  if (RunnerType::Self == type)
    return self_task_runner_tls;

  return g_task_runners[static_cast<int>(type)].get();
}

bool TaskRunner::Compare::operator()(TaskObject &below,
                                                     TaskObject &above) const {
  if (below.first >= above.first) {
    return true;
  }

  return false;
}

RunnerManager::~RunnerManager() {
  g_task_runners.clear();
  for (auto &run_thread : g_runner_threads) {
    if (run_thread.joinable())
      run_thread.join();
  }
}

void RunnerManager::BootStrap(Application &app) {
  int runner = static_cast<int>(RunnerType::Main);
  for (; runner < static_cast<int>(RunnerType::Self); runner++) {
    g_task_runners.emplace_back(
                             std::make_unique<TaskRunner>(RunnerName[runner]));
  }

  runner = static_cast<int>(RunnerType::Main);
  self_task_runner_tls = g_task_runners[runner].get(); // Main thread Self
  pthread_setname_np(pthread_self(), RunnerName[runner].c_str());

  for (runner++; runner < static_cast<int>(RunnerType::Self); runner++) {
    g_runner_threads.emplace_back(
                       std::thread(&RunnerManager::StartThread, this, runner));
  }

  std::packaged_task<void()> init_task(
                                    std::bind(&Application::Initialize, &app));
  self_task_runner_tls->PostTask(init_task);

  self_task_runner_tls->Run();

  // ShutDown
  runner = static_cast<int>(RunnerType::Io);
  for (; runner < static_cast<int>(RunnerType::Self); runner++) {
    g_task_runners[runner]->ShutDown();
  }

  for (auto &run_thread : g_runner_threads) {
    run_thread.join();
  }
}

void RunnerManager::StartThread(int type) {
  self_task_runner_tls = g_task_runners[type].get();
  pthread_setname_np(pthread_self(), RunnerName[type].c_str());
  self_task_runner_tls->Run();
  std::cout << "Thread[" << RunnerName[type] <<"] exiting\n";
}
