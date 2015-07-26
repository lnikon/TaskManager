#include "Scheduler.hh"

Scheduler::Scheduler(unsigned int nbThreads,
                     ThreadManager& manager) :
manager(manager)
, status(state::START)
{
  std::shared_ptr<Worker> worker = manager.getWorker();
  Task task;

  task.assign(&Scheduler::mainFunction, this);
  this->manager.startTask(worker, task);
}

Scheduler::~Scheduler() {
  if (this->status.load(std::memory_order_seq_cst) != state::STOP) {
    // this->stop();
  }
}

void
Scheduler::mainFunction() {
  while (this->status.load() != state::STOP) {

  }
}

void
Scheduler::runAt(const Task& task,
                 const std::chrono::steady_clock::time_point& timePoint) {
  if (this->status.load(std::memory_order_release) == state::STOP)
    throw std::runtime_error("Can't add task on stopped Scheduler");
  std::lock_guard<std::mutex> guard(this->taskMutex);
  this->taskContainer.emplace_back(task, timePoint);
}

void
Scheduler::runEvery(const Task& task,
                    const std::chrono::steady_clock::duration& duration) {
  if (this->status.load(std::memory_order_release) == state::STOP)
    throw std::runtime_error("Can't add task on stopped Scheduler");

  auto cuNow = std::chrono::steady_clock::now() + duration;
  this->runAt(Task([this, task(task), duration]() mutable {
      task();
      auto now = std::chrono::steady_clock::now();
      this->runAt(task, now + duration);
    }), cuNow);
}

std::pair<bool, std::string>
Scheduler::start() {
  if (this->status.load(std::memory_order_seq_cst) != state::STOP) {
      return std::make_pair(false, "Scheduler has already been started");
  }

  status.store(state::START, std::memory_order_seq_cst);       // we can now exectue tasks
  return std::make_pair(true, "");
}

std::pair<bool, std::string>
Scheduler::pause() {
  if (this->status.load(std::memory_order_seq_cst) != state::START) {
      return std::make_pair(false, "Scheduler is not started");
  }
  this->status.store(state::PAUSE, std::memory_order_acquire);

  std::lock_guard<std::mutex> guardWorker(this->workerMutex);
  for (auto& worker : this->workers) {
    worker->pauseTask();
  }
  return std::make_pair(true, "");
}

std::pair<bool, std::string>
Scheduler::unpause() {
  if (this->status.load(std::memory_order_seq_cst) != state::PAUSE) {
      return std::make_pair(false, "Scheduler is not paused");
  }
  status.store(state::START, std::memory_order_acquire);

  {
    std::lock_guard<std::mutex> guardWorker(this->workerMutex);
    for (auto& worker : this->workers) {
      worker->unpauseTask();
    }
  }
  return std::make_pair(true, "");
}

std::pair<bool, std::string>
Scheduler::stop() {
  bool waitCondition;
  if (this->status.load(std::memory_order_seq_cst) == state::STOP) {
      return std::make_pair(false, "Scheduler is already stopped");
  }
  this->status.store(state::STOP, std::memory_order_acquire);

  {
    std::lock_guard<std::mutex> guardWorker(this->workerMutex);
    for (auto& worker : this->workers) {
      worker->stopTask();
    }
  }
  do {
    std::lock_guard<std::mutex> guardWorker(this->workerMutex);
    waitCondition = (not this->workers.empty() || not this->taskContainer.empty());
    if (waitCondition)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } while (waitCondition);
  return std::make_pair(true, "");
};
