#include "Scheduler.h"
#include <sstream>
#include <chrono>

Scheduler::Scheduler(TaskCallback callback)
    : m_callback(std::move(callback))
{
    m_thread = std::jthread([this](std::stop_token st) { workerLoop(st); });
    WINBOT_INFO("Scheduler: started background thread");
}

Scheduler::~Scheduler() {
    // m_thread (jthread) auto-joins and respects the stop_token on destruction
}

void Scheduler::addTask(TaskDef task) {
    std::lock_guard lock{ m_mutex };
    // Replace if name exists
    auto it = std::ranges::find_if(m_tasks,
        [&](const TaskDef& t) { return t.name == task.name; });
    if (it != m_tasks.end()) *it = std::move(task);
    else m_tasks.push_back(std::move(task));
}

void Scheduler::removeTask(std::string_view name) {
    std::lock_guard lock{ m_mutex };
    std::erase_if(m_tasks, [&](const TaskDef& t) { return t.name == name; });
}

std::vector<Scheduler::TaskDef> Scheduler::listTasks() const {
    std::lock_guard lock{ m_mutex };
    return m_tasks;
}

void Scheduler::workerLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        // Sleep for 30s between checks
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (stopToken.stop_requested()) break;

        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &tt);

        std::lock_guard lock{ m_mutex };
        for (const auto& task : m_tasks) {
            if (cronMatches(task.cronExpr, localTime)) {
                WINBOT_INFO("Scheduler: firing task '{}'", task.name);
                try { m_callback(task.prompt); }
                catch (const std::exception& e) {
                    WINBOT_WARN("Scheduler: task '{}' callback threw: {}", task.name, e.what());
                }
            }
        }
    }
}

bool Scheduler::cronMatches(const std::string& expr, const std::tm& t) noexcept {
    // Parse: "Min Hour Day Month Weekday" (5 space-separated fields)
    std::istringstream ss(expr);
    std::string fields[5];
    for (auto& f : fields) {
        if (!(ss >> f)) return false;
    }
    return cronFieldMatches(fields[0], t.tm_min,  0, 59) &&
           cronFieldMatches(fields[1], t.tm_hour, 0, 23) &&
           cronFieldMatches(fields[2], t.tm_mday, 1, 31) &&
           cronFieldMatches(fields[3], t.tm_mon + 1, 1, 12) &&
           cronFieldMatches(fields[4], t.tm_wday, 0, 6);
}

bool Scheduler::cronFieldMatches(const std::string& field, int value, int min, int max) noexcept {
    try {
        if (field == "*") return true;

        // Step: */n
        if (field.starts_with("*/")) {
            int step = std::stoi(field.substr(2));
            return (step > 0) && ((value - min) % step == 0);
        }

        // Range: a-b
        if (auto dash = field.find('-'); dash != std::string::npos) {
            int a = std::stoi(field.substr(0, dash));
            int b = std::stoi(field.substr(dash + 1));
            return value >= a && value <= b;
        }

        // List: a,b,c
        if (field.contains(',')) {
            std::istringstream ss(field);
            std::string token;
            while (std::getline(ss, token, ','))
                if (std::stoi(token) == value) return true;
            return false;
        }

        // Exact value
        return std::stoi(field) == value;
    } catch (...) {
        return false;
    }
}
