#pragma once
#include <cstddef>
#include <string>

constexpr const char* kThemes[] = {"Magenta", "Blue", "Black", "White"};

struct Snap {
  bool busy;
  std::string status, log;
  size_t next;
};

Snap snapshot(size_t since);
bool busy();
void start_job(const std::string& action, const std::string& host, const std::string& pw, bool fan, const std::string& source);
std::string saved(const char* file);
void save_theme(const std::string& name);
int ui_run();
