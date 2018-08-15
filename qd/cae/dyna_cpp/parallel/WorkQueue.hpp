
#ifndef WORKQUEUE_HPP
#define WORKQUEUE_HPP

#include <cassert>
#include <deque>
#include <functional>
#include <future>
#include <thread>
#include <vector>

namespace qd {

// Source:
// https://codereview.stackexchange.com/questions/60363/thread-pool-worker-implementation

template<typename ReturnType, typename... Args>
using PromiseFunctionPair =
  std::pair<std::promise<ReturnType>, std::function<ReturnType(Args...)>>;

template<typename ReturnType, typename... Args>
using DataPointer = std::shared_ptr<PromiseFunctionPair<ReturnType, Args...>>;

/** A typical thread worker queue that can execute arbitrary jobs.
 *
 */
class WorkQueue
{

private:
  std::deque<std::function<void()>> m_work;
  std::mutex m_mutex;
  std::condition_variable m_signal;
  std::atomic<bool> m_exit{ false };
  std::atomic<bool> m_finish_work{ true };
  std::vector<std::thread> m_workers;

  void do_work();
  void join_all();

  void operator=(const WorkQueue&) = delete;
  WorkQueue(const WorkQueue&) = delete;

public:
  explicit WorkQueue();
  virtual ~WorkQueue();
  void reset();
  void init_workers(size_t num_workers = 0);
  void abort();
  void stop();
  void wait_for_completion();

  template<typename FunctionObject, typename... Args>
  auto submit(FunctionObject&& function, Args&&... args)
    -> std::future<decltype(function(args...))>;
};

/** Helper function for non-void/void return functions
 *
 * @param data : pair of promise and function
 * @param args : forwarded arguments to called function
 */
template<typename ReturnType,
         typename... Args,
         typename std::enable_if<
           !std::is_same<ReturnType, void>::value>::type* = nullptr>
inline void
execute_and_set_data(
  const std::shared_ptr<PromiseFunctionPair<ReturnType, Args...>>& data,
  Args&&... args)
{
  // data->first.set_value(data->second());
  data->first.set_value(data->second(std::forward<Args>(args)...));
}

template<typename ReturnType,
         typename... Args,
         typename std::enable_if<std::is_same<ReturnType, void>::value>::type* =
           nullptr>
inline void
execute_and_set_data(
  const std::shared_ptr<PromiseFunctionPair<ReturnType, Args...>>& data,
  Args&&... args)
{
  data->second(std::forward<Args>(args)...);
  data->first.set_value();
}

/** Executes the given function asynchronously.
 *
 * @param function : the function to execute
 * @return result : future of result that will be generated by
 *                  the function argument. Exceptions from the
 *                  function will be thrown by get() on the future.
 *
 * @throw std::runtime_error : if attempting to submit a job
 *                             to a work queue that is terminating
 */
/*
template<typename ReturnType, typename ...Args>
std::future<ReturnType> WorkQueue::submit(std::function<ReturnType(Args...)>&&
function, Args && ...args)
*/
template<typename FunctionObject, typename... Args>
auto
WorkQueue::submit(FunctionObject&& function, Args&&... args)
  -> std::future<decltype(function(args...))>
{
  using ReturnType = decltype(function(args...));

  if (m_exit) {
    throw std::runtime_error(
      "Caught work submission to work queue that is desisting.");
  }

  // auto data = std::make_shared<
  //  std::pair<std::promise<ReturnType>, std::function<ReturnType(Args...)>>>(
  //  std::promise<ReturnType>(), std::move(function));

  auto data = std::make_shared<PromiseFunctionPair<ReturnType, Args...>>(
    std::promise<ReturnType>(), std::move(function));

  std::future<ReturnType> future = data->first.get_future();

  {
    std::lock_guard<std::mutex> lg(m_mutex);
    m_work.emplace_back([data, &args...]() {
      try {

        execute_and_set_data<ReturnType, Args...>(data, args...);

        /* C++17 only
        if constexpr (std::is_same<ReturnType, void>::value) {
          // data->second(std::forward<Args>(args)...);
          data->second(std::forward<Args>(args)...);
          data->first.set_value();
        } else {
          data->first.set_value(data->second(std::forward<Args>(args)...));
        }
        */

      } catch (...) {
        data->first.set_exception(std::current_exception());
      }
    });
  }
  m_signal.notify_one();
  return std::move(future);
}

} // namepspace:qd

#endif