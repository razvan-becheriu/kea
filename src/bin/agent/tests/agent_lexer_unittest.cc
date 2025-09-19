
// -----------------------------------------------------------------------------
// Additional tests for ParserContext scanning and include handling
// Framework: Google Test (gtest)
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>

using isc::agent::ParserContext;

namespace {

// Helper to write a small temporary file and return its path.
static std::string writeTempFile(const std::string& basename,
                                 const std::string& contents) {
    // Use a deterministic temp under current working directory to avoid platform differences.
    std::string path = std::string("test_tmp_") + basename;
    std::ofstream ofs(path.c_str(), std::ios::out | std::ios::trunc);
    ofs << contents;
    ofs.close();
    return path;
}

// RAII cleanup for files created in tests.
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& p) : path(p) {}
    ~TempFile() {
        if (\!path.empty()) {
            std::remove(path.c_str());
        }
    }
};

} // end anonymous namespace

// Verifies that scanStringBegin sets the start token flag and value.
TEST(AgentLexerParserContextTest, ScanStringBeginSetsStartToken) {
    // Arrange
    ParserContext ctx;
    // Precondition: clear globals the lexer consults
    start_token_flag = false;
    // Choose an arbitrary parser type; if enum changes, adjust accordingly.
    // We prefer a value that is valid in this codebase; fall back to 0.
    isc::agent::ParserContext::ParserType ptype =
        static_cast<isc::agent::ParserContext::ParserType>(0);

    // Act
    ctx.scanStringBegin("option foo 42;", ptype);

    // Assert
    EXPECT_TRUE(start_token_flag) << "scanStringBegin must set start_token_flag";
    EXPECT_EQ(ptype, start_token_value) << "scanStringBegin must propagate parser_type";

    // Cleanup
    ctx.scanEnd();
}

// Verifies that scanFileBegin sets start token fields and can be ended safely.
TEST(AgentLexerParserContextTest, ScanFileBeginSetsStartTokenAndAllowsScanEnd) {
    // Arrange
    ParserContext ctx;
    start_token_flag = false;

    const std::string content = "option bar 7;";
    const std::string path = writeTempFile("scan_file_begin.conf", content);
    TempFile guard(path);

    FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(nullptr, f) << "Failed to open temp file for scanFileBegin";

    isc::agent::ParserContext::ParserType ptype =
        static_cast<isc::agent::ParserContext::ParserType>(0);

    // Act
    ctx.scanFileBegin(f, path, ptype);

    // Assert
    EXPECT_TRUE(start_token_flag);
    EXPECT_EQ(ptype, start_token_value);

    // Cleanup should close file and release buffers without crashing.
    EXPECT_NO_THROW(ctx.scanEnd());
}

// includeFile on a missing file should trigger a fatal path (exception).
TEST(AgentLexerParserContextTest, IncludeFileMissingTriggersFatal) {
    ParserContext ctx;
    // Ensure scanner is initialized from a string first to have a valid buffer/state.
    ctx.scanStringBegin("valid { }", static_cast<ParserContext::ParserType>(0));

    // Act + Assert
    EXPECT_ANY_THROW({
        ctx.includeFile("this_file_should_not_exist_12345.conf");
    });

    ctx.scanEnd();
}

// includeFile should succeed for an existing file and allow subsequent scanEnd.
TEST(AgentLexerParserContextTest, IncludeFileSuccessAndCleanup) {
    ParserContext ctx;
    ctx.scanStringBegin("start { }", static_cast<ParserContext::ParserType>(0));

    const std::string path = writeTempFile("include_ok.conf", "subnet 10.0.0.0/24 {}");
    TempFile guard(path);

    EXPECT_NO_THROW({
        ctx.includeFile(path);
    });

    // Even after include, scanEnd should clean up all buffers/files.
    EXPECT_NO_THROW(ctx.scanEnd());
}

// Excessive nesting of includeFile should trigger "Too many nested include." fatal path.
TEST(AgentLexerParserContextTest, IncludeFileTooManyNested) {
    ParserContext ctx;
    // Initialize scanner to set up an initial buffer so that YY_CURRENT_BUFFER is valid.
    ctx.scanStringBegin("root {}", static_cast<ParserContext::ParserType>(0));

    const std::string path = writeTempFile("deep_include.conf", "foo=1;");
    TempFile guard(path);

    // Nest includes 10 times should be okay, 11th should throw.
    for (int i = 0; i < 10; ++i) {
        EXPECT_NO_THROW(ctx.includeFile(path)) << "include depth " << i << " should not throw";
    }
    EXPECT_ANY_THROW(ctx.includeFile(path)) << "depth > 10 must throw fatal";

    // Cleanup after fatal attempt; scanEnd should still work.
    EXPECT_NO_THROW(ctx.scanEnd());
}

// Ensure scanEnd closes any open top-level file too (idempotent behavior).
TEST(AgentLexerParserContextTest, ScanEndClosesTopLevelFileIdempotent) {
    ParserContext ctx;

    const std::string content = "param X;";
    const std::string path = writeTempFile("scan_end_close.conf", content);
    TempFile guard(path);

    FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(nullptr, f);

    ctx.scanFileBegin(f, path, static_cast<ParserContext::ParserType>(0));

    // First scanEnd should clean resources; a second call should be a no-op and not crash.
    ctx.scanEnd();
    EXPECT_NO_THROW(ctx.scanEnd());
}
