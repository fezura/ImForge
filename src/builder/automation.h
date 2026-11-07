#pragma once

#include <string>
#include <vector>

namespace imforge::builder {

struct EditorContext;
struct Platform;

void AutomationAttachConsole();

class Automation {
public:
    bool Load(const std::string& script_path, std::string* err);
    void Step(EditorContext& ctx, Platform& platform);
    bool Done() const { return done_; }
    int ExitCode() const { return exit_code_; }

private:
    enum class Wait { None, Time, Client, Capture };

    bool Execute(EditorContext& ctx, Platform& platform, const std::string& line);
    void Ok(EditorContext& ctx, const std::string& msg);
    bool Fail(EditorContext& ctx, const std::string& msg);
    void Finish(EditorContext& ctx, int code);
    double Now() const;

    std::vector<std::string> lines_;
    std::string current_;
    size_t pc_ = 0;
    int line_no_ = 0;
    int warmup_frames_ = 3;
    bool loaded_ = false;
    bool done_ = false;
    int exit_code_ = 0;
    Wait wait_ = Wait::None;
    double wait_until_ = 0;
    std::string capture_path_;
    int capture_frames_ = 0;
    double start_ = -1;
    double last_step_ = -1;
    double frame_sum_ms_ = 0;
    double frame_max_ms_ = 0;
    int frame_count_ = 0;
};

}  // namespace imforge::builder
