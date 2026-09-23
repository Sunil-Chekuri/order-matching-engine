#include <gtest/gtest.h>

#include <iostream>
#include <sstream>
#include <string>

#include "utils/logger.h"

TEST(LoggerTest, SetEnabledSuppressesAndRestoresOutput)
{
    std::ostringstream captured;
    std::streambuf *original = std::cout.rdbuf(captured.rdbuf());

    Logger::setEnabled(false);
    Logger::log(LogLevel::INFO, "suppressed entry");
    bool silent_while_disabled = captured.str().empty();

    Logger::setEnabled(true);
    Logger::log(LogLevel::INFO, "visible entry");
    bool written_while_enabled =
        captured.str().find("visible entry") != std::string::npos;

    // Restore before asserting so failure output is not swallowed, and
    // so later tests are unaffected by this one's global state changes.
    std::cout.rdbuf(original);

    EXPECT_TRUE(silent_while_disabled);
    EXPECT_TRUE(written_while_enabled);
}
