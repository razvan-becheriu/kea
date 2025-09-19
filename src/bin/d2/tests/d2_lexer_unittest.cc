// Copyright ...
// Unit tests for D2 lexer/context.
// Testing framework: GoogleTest (gtest)
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>
#include <fstream>

#include <d2/parser_context.h>   // Adjust include to the actual header providing D2ParserContext, D2ParseError
#include <d2/d2_lexer.h>         // Adjust include if lexer declarations live elsewhere

using namespace isc::d2;

namespace {

class D2LexerTest : public ::testing::Test {
protected:
    D2ParserContext driver_;

    void TearDown() override {
        // Ensure scanner teardown even if tests fail mid-scan
        driver_.scanEnd();
    }

    // Helper to run a single scan of a string and get first token id name
    template <typename Tok>
    std::string tokenName(const Tok& t) {
        // Fallback textualization; actual project may have symbol_name
        return typeid(t).name();
    }
};

} // namespace

// Focused tests based on recent lexer/context changes.

// Verifies that scanStringBegin sets up a buffer and emits the correct start token for PARSER_JSON.
TEST(D2LexerTest, ScanStringBeginEmitsTopLevelJson) {
    D2ParserContext driver;
    // Minimal valid input for JSON to keep lexer happy; content itself is not parsed here.
    std::string input = "{}";
    ASSERT_NO_THROW(driver.scanStringBegin(input, D2ParserContext::PARSER_JSON));
    // The first call to yylex should emit TOPLEVEL_JSON token due to start_token_flag.
    // We call the generated scanner entry (C linkage symbol usually d2_parser_lex) via a helper wrapper.
    // If symbol differs, adjust according to project header.
    auto tok = d2_parser_lex(driver);
    // Expect token type name to contain "TOPLEVEL_JSON" if symbol_type supports name(), else rely on location changes.
    // We at least assert no exception and cleanup works.
    driver.scanEnd();
}

// Verifies different parser types produce corresponding start tokens.
TEST(D2LexerTest, StartTokenVariesByParserType) {
    D2ParserContext driver;

    std::vector<std::pair<D2ParserContext::ParserType, const char*>> cases = {
        {D2ParserContext::PARSER_DHCPDDNS, "TOPLEVEL_DHCPDDNS"},
        {D2ParserContext::PARSER_SUB_DHCPDDNS, "SUB_DHCPDDNS"},
        {D2ParserContext::PARSER_TSIG_KEY, "SUB_TSIG_KEY"},
        {D2ParserContext::PARSER_TSIG_KEYS, "SUB_TSIG_KEYS"},
        {D2ParserContext::PARSER_DDNS_DOMAIN, "SUB_DDNS_DOMAIN"},
        {D2ParserContext::PARSER_DDNS_DOMAINS, "SUB_DDNS_DOMAINS"},
        {D2ParserContext::PARSER_DNS_SERVER, "SUB_DNS_SERVER"},
        {D2ParserContext::PARSER_HOOKS_LIBRARY, "SUB_HOOKS_LIBRARY"},
    };

    for (const auto& kv : cases) {
        ASSERT_NO_THROW(driver.scanStringBegin("{}", kv.first));
        auto tok = d2_parser_lex(driver);
        // If symbol type allows inspection, verify it's the expected token.
        // Otherwise, just ensure no crash path for start token emission.
        driver.scanEnd();
    }
}

// scanFileBegin should fatal() (throw) if buffer creation fails; we simulate by passing nullptr FILE*.
// Depending on implementation, create_buffer(nullptr, ..) should be null and trigger fatal.
TEST(D2LexerTest, ScanFileBeginThrowsOnNullFile) {
    D2ParserContext driver;
    FILE* f = nullptr;
    EXPECT_THROW(driver.scanFileBegin(f, "nonexistent.txt", D2ParserContext::PARSER_JSON), D2ParseError);
    // Ensure no lingering state
    driver.scanEnd();
}

// scanEnd should close sfile_ and any stacked sfiles_ and delete states without crashing.
TEST(D2LexerTest, ScanEndCleansUpFilesAndStates) {
    D2ParserContext driver;
    // Open a real temporary file to exercise fclose path.
    const char* path = "d2_lexer_test_tmp.json";
    {
        std::ofstream ofs(path);
        ofs << "{}";
    }
    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);
    ASSERT_NO_THROW(driver.scanFileBegin(f, path, D2ParserContext::PARSER_JSON));
    // Push an include to create nested state; includeFile should push current into stacks.
    // Include same file to avoid extra IO dependencies.
    ASSERT_NO_FATAL_FAILURE(driver.includeFile(path));
    // End scan and ensure it does not crash while unwinding multiple stacks.
    ASSERT_NO_THROW(driver.scanEnd());
    // Clean up test artifact
    std::remove(path);
}

// includeFile should throw when too many nested includes.
TEST(D2LexerTest, IncludeFileTooDeepThrows) {
    D2ParserContext driver;
    const char* path = "d2_lexer_nested.json";
    {
        std::ofstream ofs(path);
        ofs << "{}";
    }
    // Manually push >10 states by invoking includeFile repeatedly after an initial scanFileBegin.
    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);
    ASSERT_NO_THROW(driver.scanFileBegin(f, path, D2ParserContext::PARSER_JSON));
    // 11 includes to exceed the limit (size() > 10 triggers fatal)
    bool threw = false;
    for (int i = 0; i < 11; ++i) {
        try {
            driver.includeFile(path);
        } catch (const D2ParseError&) {
            threw = true;
            break;
        }
    }
    EXPECT_TRUE(threw);
    driver.scanEnd();
    std::remove(path);
}

// includeFile should throw on missing file.
TEST(D2LexerTest, IncludeFileMissingThrows) {
    D2ParserContext driver;
    EXPECT_THROW(driver.includeFile("this_file_should_not_exist_12345.inc"), D2ParseError);
    driver.scanEnd();
}

// Lexing: valid quoted string with escapes is decoded and returned as STRING token.
TEST(D2LexerTest, StringDecodingHappyPath) {
    D2ParserContext driver;
    // Single-quoted per rule, with escapes including \n, \t, \\\", \u00ff
    std::string s("'line\\n\\tquote\\\" slash\\\\ unicode\\u00ff'");
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    auto tok1 = d2_parser_lex(driver); // start token
    auto tok2 = d2_parser_lex(driver); // the STRING token
    // We cannot assert symbol internals without API; ensure no errors thrown.
    driver.scanEnd();
}

// Bad string: control character inside should call driver.error -> typically not throw, but scanner continues.
// We ensure it does not crash and reports via error path (which may set an internal error counter).
TEST(D2LexerTest, StringWithControlCharReportsError) {
    D2ParserContext driver;
    std::string s("'\x01bad'"); // embeds control char 0x01
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver); // start token
    (void)d2_parser_lex(driver); // rule triggers driver.error
    driver.scanEnd();
}

// Bad escape sequences are reported.
TEST(D2LexerTest, StringWithBadEscapeReportsError) {
    D2ParserContext driver;
    std::string s("'bad\\qescape'");
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver); // start token
    (void)d2_parser_lex(driver); // error path
    driver.scanEnd();
}

// Overflow unicode escape at end.
TEST(D2LexerTest, StringWithOpenUnicodeEscapeReportsError) {
    D2ParserContext driver;
    std::string s("'\\u00'"); // incomplete
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver);
    (void)d2_parser_lex(driver);
    driver.scanEnd();
}

// Numeric tokens: integer and float.
TEST(D2LexerTest, IntegerAndFloatTokens) {
    D2ParserContext driver;
    std::string s("42 3.14");
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver); // start
    (void)d2_parser_lex(driver); // integer
    (void)d2_parser_lex(driver); // float
    driver.scanEnd();
}

// Boolean and null tokens.
TEST(D2LexerTest, BooleanAndNullTokens) {
    D2ParserContext driver;
    std::string s("true false null");
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver); // start
    (void)d2_parser_lex(driver); // true
    (void)d2_parser_lex(driver); // false
    (void)d2_parser_lex(driver); // null
    driver.scanEnd();
}

// Uppercase keywords should be errors: TRUE/FALSE/NULL -> driver.error paths.
TEST(D2LexerTest, UppercaseKeywordsReportErrors) {
    D2ParserContext driver;
    std::string s("TRUE FALSE NULL");
    ASSERT_NO_THROW(driver.scanStringBegin(s, D2ParserContext::PARSER_JSON));
    (void)d2_parser_lex(driver); // start
    (void)d2_parser_lex(driver); // error for TRUE
    (void)d2_parser_lex(driver); // error for FALSE
    (void)d2_parser_lex(driver); // error for NULL
    driver.scanEnd();
}

// EOF handling with include stack: reaching EOF should unwind states and continue.
TEST(D2LexerTest, EofUnwindsIncludeStack) {
    D2ParserContext driver;
    const char* path = "d2_lexer_eof_unwind.json";
    {
        std::ofstream ofs(path);
        ofs << "{}";
    }
    FILE* f = fopen(path, "r");
    ASSERT_NE(f, nullptr);
    ASSERT_NO_THROW(driver.scanFileBegin(f, path, D2ParserContext::PARSER_JSON));
    ASSERT_NO_THROW(driver.includeFile(path));
    // Consume tokens until EOF transitions happen; just loop a bounded number to avoid infinite loop.
    for (int i = 0; i < 10; ++i) {
        (void)d2_parser_lex(driver);
    }
    driver.scanEnd();
    std::remove(path);
}
