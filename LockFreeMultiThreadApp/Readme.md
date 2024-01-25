# Lock Free MultiThread App

Protecting the shared resources is a nightmare in the multi-threaded programming.
This design eliminates the need of sharing the resources across threads. Hence, the use of mutex in the application side too.

Here, each thread is associated with a task runner. Each threads can **PostTask** or **PostDelayedTask** to the other threads.

Under the hood task runner handles the locks to execute the tasks, which helps avoiding the use of locks in the applications.

Note: It doesn't completely elimiate the locks; rather freeing up the app deveoper from the hassles of managing critical section.

eg:

    std::packaged_task<void()> shut_down_task(std::bind(&MyApplication::MyShutDownTask, this));
    TaskRunner::GetTaskRunner(RunnerType::Self)->PostDelayedTask(shut_down_task, 12000);
    

    std::packaged_task<void()> io_task_i1(std::bind(&MyApplication::MyIoTask, this, "Instant Hello 1 From Main"));
    TaskRunner::GetTaskRunner(RunnerType::Io)->PostTask(io_task_i1);


## chatapp

Chat App is a sample implementation with the task runner.

Chat server listens on a Unix domain socket
Multiple chat clients can connect to the server and broadcast messages between the clients.

### Build (tested only on Linux)

> mkdir build
> 
> cd build
> 
> cmake ../
> 
> make


### Run

In one terminal:

> ./chatserver

In other terminals:

> ./chatclient

