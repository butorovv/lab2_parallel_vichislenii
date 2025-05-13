#include <iostream>
#include <thread>
#include <queue>
#include <random>
#include <semaphore>
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>


const int SERVERS_COUNT = 5;
const int TASKS_COUNT = 20;
const double LOAD_THRESHOLD = 0.8;

struct Task {
    int id;
    int priority; 
    int duration; 
};

struct ComparePriority {
    bool operator()(const Task& t1, const Task& t2) {
        return t1.priority > t2.priority;
    }
};

std::priority_queue<Task, std::vector<Task>, ComparePriority> task_queue;
std::counting_semaphore<SERVERS_COUNT> servers(SERVERS_COUNT);
std::mutex queue_mutex;
std::mutex cout_mutex;
std::atomic<int> active_servers(SERVERS_COUNT);
std::atomic<int> total_load(0); // текущая нагрузка в %
std::atomic<int> processed_tasks(0);
std::condition_variable cv;
std::atomic<bool> shutdown(false);

void process_task(Task task) {
    servers.acquire();

    {
        std::lock_guard<std::mutex> cout_lock(cout_mutex);
        std::cout << "Сервер обрабатывает задачу " << task.id
            << " (приоритет " << task.priority
            << ", время: " << task.duration << " сек)\n";
    }

    int new_load = (++total_load * 100) / SERVERS_COUNT;
    if (new_load > LOAD_THRESHOLD * 100 && active_servers == SERVERS_COUNT) {
        std::lock_guard<std::mutex> cout_lock(cout_mutex);
        std::cout << "!!! ВЫСОКАЯ НАГРУЗКА (" << new_load << "%). ДОБАВЛЯЕМ РЕЗЕРВНЫЙ СЕРВЕР !!!\n";
        active_servers++;
        servers.release(); 
    }

    std::this_thread::sleep_for(std::chrono::seconds(task.duration));

    {
        std::lock_guard<std::mutex> cout_lock(cout_mutex);
        std::cout << "Задача " << task.id << " завершена\n";
    }

    total_load--;
    processed_tasks++;
    servers.release();
}

void generate_tasks() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> priority_dist(1, 5); // приоритеты от 1 (высокий) до 5 (низкий)
    std::uniform_int_distribution<> duration_dist(1, 5); // время выполнения 1-5 сек

    for (int i = 1; i <= TASKS_COUNT; ++i) {
        Task task = { i, priority_dist(gen), duration_dist(gen) };

        {
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            task_queue.push(task);
        }

        {
            std::lock_guard<std::mutex> cout_lock(cout_mutex);
            std::cout << "Сгенерирована задача " << task.id
                << " (приоритет " << task.priority
                << ", время: " << task.duration << " сек)\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    shutdown = true;
    cv.notify_all();
}

void server_worker() {
    while (!shutdown || !task_queue.empty()) {
        Task task;
        bool has_task = false;

        {
            std::unique_lock<std::mutex> queue_lock(queue_mutex);
            cv.wait_for(queue_lock, std::chrono::milliseconds(100), [&]() {
                return !task_queue.empty() || shutdown;
                });

            if (!task_queue.empty()) {
                task = task_queue.top();
                task_queue.pop();
                has_task = true;
            }
        }

        if (has_task) {
            process_task(task);
        }
    }
}

int main() {
    std::thread generator(generate_tasks);
    std::vector<std::thread> servers;

    for (int i = 0; i < SERVERS_COUNT; ++i) {
        servers.emplace_back(server_worker);
    }

    generator.join();

    for (auto& server : servers) {
        server.join();
    }

    if (active_servers > SERVERS_COUNT) {
        std::thread reserve_server(server_worker);
        reserve_server.join();
    }

    std::cout << "Все задачи обработаны. Итого: " << processed_tasks.load() << "/" << TASKS_COUNT << "\n";
    return 0;
}