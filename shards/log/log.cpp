#include "log.hpp"
#include <spdlog/spdlog.h>
#include <vector>
#include <SDL_stdinc.h>
#include <magic_enum.hpp>
#include <boost/filesystem.hpp>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif

namespace shards::logging {
void init(Logger logger) {
  initLogLevel(logger);
  initLogFormat(logger);
  initSinks(logger);
}

void initLogLevel(Logger logger) {
  std::string varName = fmt::format("LOG_{}", logger->name());
  if (const char *val = SDL_getenv(varName.c_str())) {
    auto enumVal = magic_enum::enum_cast<spdlog::level::level_enum>(val);
    if (enumVal.has_value()) {
      logger->set_level(enumVal.value());
    }
  }
}

void initLogFormat(Logger logger) {
  std::string varName = fmt::format("LOG_{}_FORMAT", logger->name());
  if (const char *val = SDL_getenv(varName.c_str())) {
    logger->set_pattern(val);
  }
}

void initSinks(Logger logger) { logger->sinks() = spdlog::default_logger()->sinks(); }

Logger get(const std::string &name) {
  auto logger = spdlog::get(name);
  if (!logger) {
    logger = std::make_shared<spdlog::logger>(name);
    init(logger);
    spdlog::register_logger(logger);
  }
  return logger;
}

void redirectAll(const std::vector<spdlog::sink_ptr> &sinks) {
  spdlog::apply_all([&](Logger logger) { logger->sinks() = sinks; });
}

static void setupDefaultLogger() {
  auto dist_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();

  auto sink1 = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  dist_sink->add_sink(sink1);

  // Setup log file
#ifdef SHARDS_LOG_FILE
  std::string logFilePath = boost::filesystem::absolute("shards.log").string();
  auto sink2 = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath.c_str(), 1048576, 3, false);
  dist_sink->add_sink(sink2);
#endif

  auto logger = std::make_shared<spdlog::logger>("shards", dist_sink);
  logger->flush_on(spdlog::level::err);
  spdlog::set_default_logger(logger);

  // Setup android logcat output
#ifdef __ANDROID__
  auto android_sink = std::make_shared<spdlog::sinks::android_sink_mt>("shards");
  logger->sinks().push_back(android_sink);
#endif

#ifdef __ANDROID
  // Logcat already countains timestamps & log level
  spdlog::set_pattern("[T-%t] [%s::%#] %v");
#else
  spdlog::set_pattern("%^[%l]%$ [%Y-%m-%d %T.%e] [T-%t] [%s::%#] %v");
#endif

  // Set default log level
  spdlog::set_level(spdlog::level::level_enum(SHARDS_DEFAULT_LOG_LEVEL));

  // Init log level from environment variable
  initLogLevel(logger);
  // Init log format from environment variable
  initLogFormat(logger);

  // Redirect all existing loggers to the default sink
  redirectAll(logger->sinks());
}

void setupDefaultLoggerConditional() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    setupDefaultLogger();
  }
}
} // namespace shards::logging
