/***
 * To Build: g++ -g -std=c++17 task_runner.cpp sample_app.cpp -lpthread -o sample_app
 * To Run: ./task_runner
 ***/

#include "task_runner.h"

class MyApplication : public Application{
public:
  bool Initialize() override {
    std::cout << __func__ << " : MyApplication Initializing...\n";
    std::packaged_task<void()> shut_down_task(std::bind(&MyApplication::MyShutDownTask, this));
    TaskRunner::GetTaskRunner(RunnerType::Self)->PostDelayedTask(shut_down_task, 12000);
    
    std::packaged_task<void()> io_task_d1(std::bind(&MyApplication::MyIoTask, this, "Delayed Hello 1 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Io)->PostDelayedTask(io_task_d1, 10000);

    std::packaged_task<void()> io_task_i1(std::bind(&MyApplication::MyIoTask, this, "Instant Hello 1 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Io)->PostTask(io_task_i1);

    std::packaged_task<void()> ut_task_d1(std::bind(&MyApplication::MyUtilsTask, this, "Delayed Hello 2 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Utility)->PostDelayedTask(ut_task_d1, 3000);
    
    std::packaged_task<void()> io_task_d2(std::bind(&MyApplication::MyIoTask, this, "Delayed Hello 3 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Io)->PostDelayedTask(io_task_d2, 5000);
    
    std::packaged_task<void()> io_task_i2(std::bind(&MyApplication::MyIoTask, this, "Instant Hello 2 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Io)->PostTask(io_task_i2);
    return true;
  }

  bool MyShutDownTask() {
    std::cout << __func__ << " : Shutting Down" << std::endl;
    TaskRunner::GetTaskRunner(RunnerType::Main)->ShutDown();
    return true;
  }
  
  bool MyIoTask(std::string message) {
    std::cout << __func__ << " : " << message << std::endl;
    std::packaged_task<void()> main_task(std::bind(&MyApplication::OnMainRcvdResponse, this, "Ack From IO"));
    TaskRunner::GetTaskRunner(RunnerType::Main)->PostTask(main_task);
    return true;
  }

  bool MyUtilsFromUtilsTask(std::string message) {
    std::cout << __func__ << " : " << message << std::endl;
    return true;
  }

  bool MyUtilsTask(std::string message) {
    std::cout << __func__ << " : " << message << std::endl;
    std::packaged_task<void()> main_task(std::bind(&MyApplication::OnMainRcvdResponse, this, "Ack From Utils"));
    TaskRunner::GetTaskRunner(RunnerType::Main)->PostTask(main_task);

    std::packaged_task<void()> buddy_task(std::bind(&MyApplication::MyUtilsFromUtilsTask, this, "Buddy Task in Utils"));
    TaskRunner::GetTaskRunner(RunnerType::Self)->PostDelayedTask(buddy_task, 4000);

    return true;
  }
  
  bool OnMainRcvdResponse(std::string message) {
    std::cout << __func__  << " : "<< message << std::endl;
    return true;
  }
};

int main() {
  MyApplication myApp;
  RunnerManager runner_manager;
  runner_manager.BootStrap(myApp);
  return 0;
}
