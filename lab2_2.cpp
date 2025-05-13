#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <semaphore>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>

const int INTERSECTIONS_COUNT = 10;
const double TRAFFIC_THRESHOLD = 0.7;


enum class TrafficLight { RED, GREEN };
struct Intersection {
    int id;
    TrafficLight light;
    std::atomic<int> car_count;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> emergency;
};

std::vector<Intersection> intersections(INTERSECTIONS_COUNT);
std::atomic<bool> shutdown(false);
std::mutex cout_mutex;

void traffic_light_controller(int id) {
    auto& intersection = intersections[id];
    intersection.id = id;
    intersection.light = TrafficLight::RED;
    intersection.emergency = false;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(3, 8); // время цикла светофора

    while (!shutdown) {
        int cycle_time = dist(gen);
        // адаптация к трафику
        if (intersection.car_count.load() > TRAFFIC_THRESHOLD * 100) {
            cycle_time = std::min(10, cycle_time + 2);

            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Перекресток " << id << ": высокая загрузка ("
                << intersection.car_count.load() << " машин), увеличиваем цикл\n";
        }
        // зеленый свет
        {
            std::lock_guard<std::mutex> lock(intersection.mtx);
            intersection.light = TrafficLight::GREEN;
        }
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Перекресток " << id << ": ЗЕЛЕНЫЙ свет (длительность: "
                << cycle_time << " сек)\n";
        }

        intersection.cv.notify_all();
        std::this_thread::sleep_for(std::chrono::seconds(cycle_time / 2));
        // красный свет
        {
            std::lock_guard<std::mutex> lock(intersection.mtx);
            intersection.light = TrafficLight::RED;
        }

        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Перекресток " << id << ": КРАСНЫЙ свет\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(cycle_time / 2));
    }

// функция автомобиля
void car(int id, int intersection_id) {
    auto& intersection = intersections[intersection_id % INTERSECTIONS_COUNT];
    intersection.car_count++;

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Автомобиль " << id << " подъехал к перекрестку "
            << intersection.id << "\n";
    }
    // ожидание зеленого света
    {
        std::unique_lock<std::mutex> lock(intersection.mtx);
        intersection.cv.wait(lock, [&]() {
            return intersection.light == TrafficLight::GREEN || intersection.emergency.load();
            });
    }
    // проезд перекрестка
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Автомобиль " << id << " проезжает перекресток "
            << intersection.id << "\n";
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    intersection.car_count--;
}
    // функция экстренной службы
void emergency_service(int id, int intersection_id) {
    auto& intersection = intersections[intersection_id % INTERSECTIONS_COUNT];
    intersection.emergency = true;
    intersection.cv.notify_all();

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "!!! ЭКСТРЕННАЯ СЛУЖБА " << id << " на перекрестке "
            << intersection.id << " !!!\n";
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
    intersection.emergency = false;
}

int main() {
    for (int i = 0; i < INTERSECTIONS_COUNT; ++i) {
        intersections[i].id = i;
        intersections[i].car_count = 0;
        intersections[i].emergency = false;
    }
        std::vector<std::thread> lights;
    for (int i = 0; i < INTERSECTIONS_COUNT; ++i) {
        lights.emplace_back(traffic_light_controller, i);
    }

    std::vector<std::thread> cars;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, INTERSECTIONS_COUNT - 1);

    for (int i = 0; i < 50; ++i) {
        cars.emplace_back(car, i, dist(gen));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::thread ambulance(emergency_service, 1, dist(gen));
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::thread fire_truck(emergency_service, 2, dist(gen));

    // ожидание завершения
    for (auto& car_thread : cars) {
        car_thread.join();
    }

    ambulance.join();
    fire_truck.join();
    shutdown = true;

    for (auto& light : lights) {
        light.join();
    }

    std::cout << "Моделирование завершено\n";
    return 0;
}