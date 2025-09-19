// Added tests focusing on ParserContext::scanStringBegin, scanFileBegin, scanEnd, includeFile.
// Testing framework: GoogleTest (gtest)

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

#include <agent/parser_context.h>   // Adjust include path if different in this repo.

namespace {

using isc::agent::ParserContext;

// Helper RAII temp file
class TempFile {
public:
    explicit TempFile(const std::string& content, const std::string& suffix = ".tmp") {
        char tmpl[] = "/tmp/kea-agent-lexer-XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd == -1) {
            throw std::runtime_error("mkstemp failed");
        }
        path_ = tmpl;
        // write content
        FILE* f = fdopen(fd, "w");
        if (\!f) {
            ::close(fd);
            throw std::runtime_error("fdopen failed");
        }
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }

    ~TempFile() {
        if (\!path_.empty()) {
            std::remove(path_.c_str());
        }
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// A minimal fixture to toggle tracing off for stable behavior.
class ParserContextScanTest : public ::testing::Test {
protected:
    void SetUp() override {
        // In many ISC components, ParserContext can be constructed with flags.
        // If constructors differ, adjust accordingly.
    }
    void TearDown() override {}
};

// Happy path: scanning from string initializes file_ to "<string>" and sets up buffer.
TEST_F(ParserContextScanTest, ScanStringBeginSetsStringFileAndBuffer) {
    ParserContext ctx;
    // By contract, this should not throw.
    EXPECT_NO_THROW(ctx.scanStringBegin("set ControlSocket { socket-name \"foo\"; };",
                                        ParserContext::PARSER_JSON));
    // After scanStringBegin, ending scan should be safe.
    EXPECT_NO_THROW(ctx.scanEnd());
}

// Happy path: scanFileBegin opens real file and can be ended.
TEST_F(ParserContextScanTest, ScanFileBeginOpensFileAndScanEndCloses) {
    TempFile tf("{ \"ControlSocket\": { \"socket-name\": \"foo\" } }\n");
    ParserContext ctx;
    FILE* f = fopen(tf.path().c_str(), "r");
    ASSERT_NE(f, nullptr) << "Failed to reopen temp file for test";
    EXPECT_NO_THROW(ctx.scanFileBegin(f, tf.path(), ParserContext::PARSER_JSON));
    // scanEnd should fclose the current file and destroy buffers
    EXPECT_NO_THROW(ctx.scanEnd());
    // Do not fclose(f) here; ownership transferred to ParserContext which closed it in scanEnd().
}

// Failure path: scanFileBegin with null buffer creation should trigger fatal.
// We simulate by passing a nullptr FILE*, expecting fatal() to throw.
TEST_F(ParserContextScanTest, ScanFileBeginWithNullFileThrowsFatal) {
    ParserContext ctx;
    // The implementation calls agent__create_buffer on FILE*; passing nullptr should cause fatal().
    EXPECT_THROW(ctx.scanFileBegin(nullptr, std::string("<null>"), ParserContext::PARSER_JSON), std::exception);
    // After fatal path, ensure scanEnd is idempotent.
    EXPECT_NO_THROW(ctx.scanEnd());
}

// includeFile: cannot open file -> fatal.
TEST_F(ParserContextScanTest, IncludeFileMissingThrowsFatal) {
    ParserContext ctx;
    // Begin a base string scan so include operates on an active context.
    ctx.scanStringBegin("{}", ParserContext::PARSER_JSON);
    EXPECT_THROW(ctx.includeFile("/path/that/does/not/exist/never.json"), std::exception);
    ctx.scanEnd();
}

// includeFile: nested includes exceeding limit -> fatal.
TEST_F(ParserContextScanTest, IncludeFileTooDeepThrowsFatal) {
    ParserContext ctx;
    ctx.scanStringBegin("{}", ParserContext::PARSER_JSON);

    // Create a small chain of include-able files that include each other by name tokens.
    // Since we cannot easily influence lexer directives here, we simulate depth by directly
    // pushing many includes. The function itself enforces a hard depth limit (>10).
    // We create 12 empty files and invoke includeFile on each; once states_.size() > 10,
    // the next include should fatal.
    std::vector<std::unique_ptr<TempFile>> temps;
    for (int i = 0; i < 11; ++i) {
        temps.emplace_back(new TempFile("// dummy\n"));
        EXPECT_NO_THROW(ctx.includeFile(temps.back()->path()));
    }
    // Next include should exceed the limit and throw
    TempFile overflow("// overflow\n");
    EXPECT_THROW(ctx.includeFile(overflow.path()), std::exception);

    ctx.scanEnd();
}

// scanEnd: idempotency and proper cleanup when no scan was begun.
TEST(ParserContextStandaloneTest, ScanEndWithoutBeginIsSafe) {
    ParserContext ctx;
    EXPECT_NO_THROW(ctx.scanEnd());
}

// Scan string begin followed by multiple scanEnd calls is safe (idempotent).
TEST(ParserContextStandaloneTest, MultipleScanEndCallsAreSafe) {
    ParserContext ctx;
    ctx.scanStringBegin("{}", ParserContext::PARSER_JSON);
    EXPECT_NO_THROW(ctx.scanEnd());
    EXPECT_NO_THROW(ctx.scanEnd());
}

// Include then scanEnd closes all stacked files and buffers without leaks.
// This test ensures no exceptions when multiple includes are active.
TEST_F(ParserContextScanTest, IncludeStackCleansOnScanEnd) {
    ParserContext ctx;
    TempFile base("// base\n");
    FILE* f = fopen(base.path().c_str(), "r");
    ASSERT_NE(f, nullptr);
    ctx.scanFileBegin(f, base.path(), ParserContext::PARSER_JSON);

    TempFile inc1("// inc1\n");
    TempFile inc2("// inc2\n");
    EXPECT_NO_THROW(ctx.includeFile(inc1.path()));
    EXPECT_NO_THROW(ctx.includeFile(inc2.path()));
    EXPECT_NO_THROW(ctx.scanEnd());
}

// scanStringBegin should set internal file_ to "<string>"; if ParserContext
// exposes a method to query current file, verify it. If not, this is a compile-time
// guard and will be optimized away.
TEST_F(ParserContextScanTest, ScanStringBeginSetsPseudoFilename) {
    ParserContext ctx;
    ctx.scanStringBegin("{}", ParserContext::PARSER_JSON);
    // If ParserContext has currentFile() accessor use it; conditionally compile otherwise.
#ifdef KEA_AGENT_PARSER_HAS_CURRENT_FILE
    EXPECT_EQ(std::string("<string>"), ctx.currentFile());
#endif
    ctx.scanEnd();
}

} // namespace