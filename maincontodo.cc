#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>
#include <chrono>
#include <getopt.h>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <algorithm>
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>

// Clase para manejar las tareas estadísticas
class StatsTask {
    const std::vector<double>& data;
    int start, end;
    double& global_log_sum;
    double& global_sum;
    double& global_diff_sum;
    std::mutex& mutex;
    bool& has_zero;
    int& global_count;

public:
    StatsTask(const std::vector<double>& d, int s, int e,
              double& gls, double& gs, double& gds, std::mutex& mtx, bool& hz, int& gc)
        : data(d), start(s), end(e),
          global_log_sum(gls), global_sum(gs), global_diff_sum(gds),
          mutex(mtx), has_zero(hz), global_count(gc) {}

    void computeMetrics() {
        double local_log_sum = 0.0;
        double local_sum = 0.0;
        double local_diff_sum = 0.0;
        bool local_has_zero = false;
        int local_count = 0;

        for (int i = start; i < end; ++i) {
            double val = data[i];
            if (val == 0) {
                local_has_zero = true;
            } else {
                local_log_sum += std::log(std::abs(val));
            }
            local_sum += val;
            local_diff_sum += (val - i); // Moda: data[i] - i
            local_count++;
        }

        std::lock_guard<std::mutex> lock(mutex);
        global_log_sum += local_log_sum;
        global_sum += local_sum;
        global_diff_sum += local_diff_sum;
        has_zero = has_zero || local_has_zero;
        global_count += local_count;
    }
};

// Clase para tareas de QThreadPool
class StatsRunnable : public QRunnable {
    StatsTask& task;

public:
    StatsRunnable(StatsTask& t) : task(t) {
        setAutoDelete(true); // QThreadPool elimina la tarea automáticamente
    }

    void run() override {
        task.computeMetrics();
    }
};

// Divide and Conquer strategy
void divideAndConquer(const std::vector<double>& data, int splits,
                      double& mode, double& stddev, double& sum) {
    if (data.empty()) {
        mode = 0;
        stddev = 0;
        sum = 0;
        return;
    }

    int size = data.size();
    if (splits < 0 || splits > 5) {
        std::cerr << "Error: -d VALOR debe estar entre 0 y 5\n";
        return;
    }

    int parts = (splits == 0) ? 1 : (1 << splits);
    double global_log_sum = 0.0, global_sum = 0.0, global_diff_sum = 0.0;
    bool has_zero = false;
    int global_count = 0;
    std::mutex mutex;

    int chunk_size = size / parts;
    if (chunk_size == 0) {
        parts = size;
        chunk_size = 1;
    }

    std::vector<StatsTask> tasks;
    std::vector<std::thread> threads;
    tasks.reserve(parts);

    for (int i = 0; i < parts; ++i) {
        int start = i * chunk_size;
        int end = (i == parts - 1) ? size : start + chunk_size;
        tasks.emplace_back(data, start, end, global_log_sum, global_sum, global_diff_sum, mutex, has_zero, global_count);
        if (parts == 1) {
            tasks.back().computeMetrics();
        } else {
            threads.emplace_back(&StatsTask::computeMetrics, &tasks.back());
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    mode = global_count > 0 ? global_diff_sum / global_count : 0.0; // Moda: promedio de diferencias
    stddev = global_sum / 2.0; // Desviación estándar: suma total / 2
    sum = has_zero ? 0 : std::exp(global_log_sum); // Sumatoria: producto
}

// Thread Pool strategy con QThreadPool
void threadPool(const std::vector<double>& data, int num_threads,
                double& mode, double& stddev, double& sum) {
    if (data.empty()) {
        mode = 0;
        stddev = 0;
        sum = 0;
        return;
    }

    int size = data.size();
    if (num_threads < 1 || num_threads > 32) {
        std::cerr << "Error: -p VALOR debe estar entre 1 y 32\n";
        return;
    }

    double global_log_sum = 0.0, global_sum = 0.0, global_diff_sum = 0.0;
    bool has_zero = false;
    int global_count = 0;
    std::mutex mutex;

    QThreadPool pool;
    pool.setMaxThreadCount(num_threads);
    int chunk_size = size / num_threads;
    if (chunk_size == 0) {
        num_threads = size;
        chunk_size = 1;
    }

    std::vector<StatsTask> tasks;
    tasks.reserve(num_threads);

    // Enqueue tasks for metrics
    for (int i = 0; i < num_threads; ++i) {
        int start = i * chunk_size;
        int end = (i == num_threads - 1) ? size : start + chunk_size;
        tasks.emplace_back(data, start, end, global_log_sum, global_sum, global_diff_sum, mutex, has_zero, global_count);
        pool.start(new StatsRunnable(tasks[i]));
    }

    // Wait for tasks to complete
    pool.waitForDone();

    mode = global_count > 0 ? global_diff_sum / global_count : 0.0; // Moda: promedio de diferencias
    stddev = global_sum / 2.0; // Desviación estándar: suma total / 2
    sum = has_zero ? 0 : std::exp(global_log_sum); // Sumatoria: producto
}
// ... (resto del código sin cambios hasta main) ...

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int opt, d_val = -1, p_val = -1;
    while ((opt = getopt(argc, argv, "d:p:")) != -1) {
        if (opt == 'd') {
            d_val = std::atoi(optarg);
            if (d_val < 0 || d_val > 5) {
                std::cerr << "Error: -d VALOR debe estar entre 0 y 5\n";
                return 1;
            }
        } else if (opt == 'p') {
            p_val = std::atoi(optarg);
            if (p_val < 1 || p_val > 32) {
                std::cerr << "Error: -p VALOR debe estar entre 1 y 32\n";
                return 1;
            }
        }
    }

    if (d_val == -1 && p_val == -1) {
        std::cerr << "Error: debe especificar -d o -p\n";
        return 1;
    }
    if (d_val != -1 && p_val != -1) {
        std::cerr << "Error: no puede usar -d y -p juntos\n";
        return 1;
    }

    std::vector<double> data(100);
    std::srand(42);
    for (int i = 0; i < 100; ++i) {
        data[i] = std::round((std::rand() / (double)RAND_MAX) * 100);
    }

    double mode = 0, stddev = 0, sum = 0;
    const int RUNS = 5;
    long min_duration = std::numeric_limits<long>::max();
    std::string strategy;
    int num_threads = 0;

    for (int i = 0; i < RUNS; ++i) {
        // Barrera de memoria para evitar optimizaciones
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto start = std::chrono::steady_clock::now(); // Usar steady_clock
        if (d_val != -1) {
            strategy = "DivideConquer";
            num_threads = (d_val == 0) ? 1 : (1 << d_val);
            divideAndConquer(data, d_val, mode, stddev, sum);
        } else {
            strategy = "ThreadPool";
            num_threads = p_val;
            threadPool(data, p_val, mode, stddev, sum);
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        min_duration = std::min(min_duration, duration);
    }

    std::cout << "Estrategia: " << strategy << "\n";
    std::cout << "Hilos: " << num_threads << "\n";
    std::cout << "Moda: " << mode << "\n";
    std::cout << "Desviación estándar: " << stddev << "\n";
    std::cout << "Suma: " << sum << "\n";
    std::cout << "Tiempo mínimo: " << min_duration << " microsegundos\n";

    std::ofstream out("results.csv", std::ios::app);
    if (out.is_open()) {
        out << strategy << "," << num_threads << "," << min_duration << "\n";
        out.close();
    } else {
        std::cerr << "Error: no se pudo abrir results.csv\n";
    }

    return 0;
}
