#include "services/Scheduler.h"
#include "Common.h"
#include <array>
#include <sstream>
#include <chrono>

Scheduler::Scheduler(TaskCallback callback)
    : m_callback(std::move(callback))
{
    m_thread = std::jthread([this](const std::stop_token& st) { workerLoop(st); });
    WINBOT_INFO("Scheduler: started background thread");
}

#include <charconv>
#include <ranges>

static std::optional<int> parse_cron_int(std::string_view s) noexcept {
    int val = 0;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec == std::errc{} && ptr == s.data() + s.size()) {
        return val;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return std::nullopt;
}

Scheduler::~Scheduler() {
    m_thread.request_stop();
    m_cv.notify_all();
}

void Scheduler::addTask(TaskDef task) {
    std::scoped_lock lock{ m_mutex };
    // Replace if name exists
    auto it = std::ranges::find_if(m_tasks,
        [&](const TaskDef& t) { return t.name == task.name; });
    if (it != m_tasks.end()) {
        *it = std::move(task);
    } else {
        m_tasks.push_back(std::move(task));
    }
}

void Scheduler::removeTask(std::string_view name) {
    std::scoped_lock lock{ m_mutex };
    std::erase_if(m_tasks, [&](const TaskDef& t) { return t.name == name; });
}

std::vector<Scheduler::TaskDef> Scheduler::listTasks() const {
    std::scoped_lock lock{ m_mutex };
    return m_tasks;
}

void Scheduler::workerLoop(const std::stop_token& stopToken) {
    while (!stopToken.stop_requested()) {
        {
            std::unique_lock lock{ m_mutex };
            (void)m_cv.wait_for(lock, stopToken, std::chrono::seconds(30), [&] {
                return stopToken.stop_requested();
            });
        }
        if (stopToken.stop_requested()) {
            break;
        }

        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        (void)localtime_s(&localTime, &tt);

        std::scoped_lock lock{ m_mutex };
        for (const auto& task : m_tasks) {
            if (cronMatches(task.cronExpr, localTime)) {
                WINBOT_INFO("Scheduler: firing task '{}'", task.name);
                try {
                    m_callback(task.prompt);
                } catch (const std::exception& e) {
                    WINBOT_WARN("Scheduler: task '{}' callback threw: {}", task.name, e.what());
                }
            }
        }
    }
}

bool Scheduler::cronMatches(const std::string& expr, const std::tm& t) noexcept {
    try {
        // Parse: "Min Hour Day Month Weekday" (5 space-separated fields)
        std::istringstream ss(expr);
        std::array<std::string, 5> fields{};
        for (auto& f : fields) {
            if (!(ss >> f)) {
                return false;
            }
        }
        const auto& [minute, hour, day, month, weekday] = fields;
        return cronFieldMatches(minute,  t.tm_min,  0, 59) &&
               cronFieldMatches(hour,    t.tm_hour, 0, 23) &&
               cronFieldMatches(day,     t.tm_mday, 1, 31) &&
               cronFieldMatches(month,   t.tm_mon + 1, 1, 12) &&
               cronFieldMatches(weekday, t.tm_wday, 0, 6);
    } catch (...) {
        return false;
    }
}

bool Scheduler::cronFieldMatches(const std::string& fieldStr, int value, int min, [[maybe_unused]] int max) noexcept {
    std::string_view field = fieldStr;
    if (field == "*") {
        return true;
    }

    // Step: */n
    if (field.starts_with("*/")) {
        auto step = parse_cron_int(field.substr(2));
        return step.has_value() && (*step > 0) && ((value - min) % *step == 0);
    }

    // Range: a-b
    if (auto dash = field.find('-'); dash != std::string_view::npos) {
        auto a = parse_cron_int(field.substr(0, dash));
        auto b = parse_cron_int(field.substr(dash + 1));
        return a.has_value() && b.has_value() && value >= *a && value <= *b;
    }

    // List: a,b,c
    if (field.contains(',')) {
        return std::ranges::any_of(std::views::split(field, ','), [&](const auto& part) {
            // NOLINTNEXTLINE(bugprone-dangling-handle)
            std::string_view token(part.begin(), part.end());
            return parse_cron_int(token) == value;
        });
    }

    // Exact value
    return parse_cron_int(field) == value;
}
