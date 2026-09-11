#pragma once
#include <array>
#include <cstdint>

namespace han {
// Inject monotonic time so pauses, app navigation and wall-clock sync cannot skew elapsed time.
class StudyTimer {
public:
    bool Start(int subject, int64_t now_ms) {
        if (subject < 0 || subject >= 3 || (running_ && subject != subject_))
            return false;
        if (!running_) {
            subject_ = subject;
            started_ms_ = now_ms;
            running_ = true;
            completed_[subject] = false;
        }
        return true;
    }
    bool Select(int subject) {
        if (running_ || subject < 0 || subject >= 3)
            return false;
        subject_ = subject;
        return true;
    }
    void Pause(int64_t now_ms) {
        if (running_)
            totals_ms_[subject_] += now_ms > started_ms_ ? now_ms - started_ms_ : 0;
        running_ = false;
    }
    void Complete(int64_t now_ms) {
        Pause(now_ms);
        completed_[subject_] = true;
    }
    int64_t Elapsed(int subject, int64_t now_ms) const {
        if (subject < 0 || subject >= 3)
            return 0;
        return totals_ms_[subject] +
               (running_ && subject == subject_ && now_ms > started_ms_ ? now_ms - started_ms_ : 0);
    }
    bool running() const { return running_; }
    int subject() const { return subject_; }
    bool completed(int subject) const { return completed_[subject]; }
    void Restore(int subject, int64_t elapsed_ms, bool completed) {
        if (subject >= 0 && subject < 3) {
            totals_ms_[subject] = elapsed_ms > 0 ? elapsed_ms : 0;
            completed_[subject] = completed;
        }
    }

private:
    std::array<int64_t, 3> totals_ms_{};
    std::array<bool, 3> completed_{};
    int subject_ = 0;
    bool running_ = false;
    int64_t started_ms_ = 0;
};
}  // namespace han
