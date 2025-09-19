// Copyright (C) 2014-2025 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>

#include <exceptions/exceptions.h>
#include <cc/data.h>
#include <process/daemon.h>
#include <process/config_base.h>
#include <process/log_parser.h>
#include <log/logger_support.h>

#include <gtest/gtest.h>

#include <sys/wait.h>

using namespace isc;
using namespace isc::process;
using namespace isc::data;

namespace isc {
namespace process {

// @brief Derived Daemon class
class DaemonImpl : public Daemon {
public:
    static std::string getVersion(bool extended);

    using Daemon::makePIDFileName;
};

std::string DaemonImpl::getVersion(bool extended) {
    if (extended) {
        return (std::string("EXTENDED"));
    } else {
        return (std::string("BASIC"));
    }
}

}
}

namespace {

/// @brief Daemon Test test fixture class
class DaemonTest : public ::testing::Test {
public:
    /// @brief Constructor
    DaemonTest() : env_copy_() {
        // Take a copy of KEA_PIDFILE_DIR environment variable value
        char *env_value = getenv("KEA_PIDFILE_DIR");
        if (env_value) {
            env_copy_ = std::string(env_value);
        }
    }

    /// @brief Destructor
    ///
    /// As some of the tests have the side-effect of altering the logging
    /// settings (when configureLogger is called), the logging is reset to
    /// the default after each test completes.
    ~DaemonTest() {
        isc::log::setDefaultLoggingOutput();
        // Restore KEA_PIDFILE_DIR environment variable value
        if (env_copy_.empty()) {
            static_cast<void>(unsetenv("KEA_PIDFILE_DIR"));
        } else {
            static_cast<void>(setenv("KEA_PIDFILE_DIR", env_copy_.c_str(), 1));
        }
    }

private:
    /// @brief copy of KEA_PIDFILE_DIR environment variable value
    std::string env_copy_;
};

// Very simple test. Checks whether Daemon can be instantiated and its
// default parameters are sane
TEST_F(DaemonTest, constructor) {
    // Disable KEA_PIDFILE_DIR
    EXPECT_EQ(0, unsetenv("KEA_PIDFILE_DIR"));

    EXPECT_NO_THROW(Daemon instance1);

    // Check only instance values.
    Daemon instance2;
    EXPECT_TRUE(instance2.getConfigFile().empty());
    EXPECT_EQ(std::string(PIDFILE_DIR), instance2.getPIDFileDir());
    EXPECT_TRUE(instance2.getPIDFileName().empty());
}

// Verify config file accessors
TEST_F(DaemonTest, getSetConfigFile) {
    Daemon instance;

    EXPECT_NO_THROW(instance.setConfigFile("test.txt"));
    EXPECT_EQ("test.txt", instance.getConfigFile());
    EXPECT_NO_THROW(instance.checkConfigFile());
}

// Verify config file checker.
TEST_F(DaemonTest, checkConfigFile) {
    Daemon instance;

    EXPECT_THROW(instance.checkConfigFile(), BadValue);
    EXPECT_NO_THROW(instance.setConfigFile("/tmp/"));
    EXPECT_THROW(instance.checkConfigFile(), BadValue);
    EXPECT_NO_THROW(instance.setConfigFile("/tmp/test.txt"));
    EXPECT_NO_THROW(instance.checkConfigFile());
}

// Verify write config file checker.
TEST_F(DaemonTest, checkWriteConfigFile) {
    Daemon instance;

    std::string file("");
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "/tmp/";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "tmp/";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    instance.setConfigFile("/tmp/foo");
    file = "/foo/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "/tmp/foo/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "/tmp/bar";
    EXPECT_NO_THROW(instance.checkWriteConfigFile(file));
    EXPECT_EQ("/tmp/bar", file);
    file = "bar";
    EXPECT_NO_THROW(instance.checkWriteConfigFile(file));
    EXPECT_EQ("/tmp/bar", file);
    instance.setConfigFile("tmp/foo");
    file = "/tmp/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "/tmp/foo/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "tmp/bar";
    EXPECT_NO_THROW(instance.checkWriteConfigFile(file));
    EXPECT_EQ("tmp/bar", file);
    instance.setConfigFile("foo");
    file = "/tmp/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "tmp/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "foo/bar";
    EXPECT_THROW(instance.checkWriteConfigFile(file), BadValue);
    file = "bar";
    EXPECT_NO_THROW(instance.checkWriteConfigFile(file));
    EXPECT_EQ("bar", file);
}

// Verify process name accessors
TEST_F(DaemonTest, getSetProcName) {
    Daemon instance;

    EXPECT_NO_THROW(instance.setProcName("myproc"));
    EXPECT_EQ("myproc", instance.getProcName());
}

// Verify PID file directory name accessors
TEST_F(DaemonTest, getSetPIDFileDir) {
    Daemon instance;

    EXPECT_NO_THROW(instance.setPIDFileDir("/tmp"));
    EXPECT_EQ("/tmp", instance.getPIDFileDir());
}

// Verify PID file name accessors.
TEST_F(DaemonTest, setPIDFileName) {
    Daemon instance;

    // Verify that PID file name may not be set to empty
    EXPECT_THROW(instance.setPIDFileName(""), BadValue);

    EXPECT_NO_THROW(instance.setPIDFileName("myproc"));
    EXPECT_EQ("myproc", instance.getPIDFileName());

    // Verify that setPIDFileName cannot be called twice on the same instance.
    EXPECT_THROW(instance.setPIDFileName("again"), InvalidOperation);
}

// Test the getVersion() redefinition
TEST_F(DaemonTest, getVersion) {
    EXPECT_THROW(Daemon::getVersion(false), NotImplemented);

    ASSERT_NO_THROW(DaemonImpl::getVersion(false));

    EXPECT_EQ(DaemonImpl::getVersion(false), "BASIC");

    ASSERT_NO_THROW(DaemonImpl::getVersion(true));

    EXPECT_EQ(DaemonImpl::getVersion(true), "EXTENDED");
}

// Verify makePIDFileName method
TEST_F(DaemonTest, makePIDFileName) {
    DaemonImpl instance;

    // Verify that config file cannot be blank
    instance.setProcName("notblank");
    EXPECT_THROW(instance.makePIDFileName(), InvalidOperation);

    // Verify that proc name cannot be blank
    instance.setProcName("");
    instance.setConfigFile("notblank");
    EXPECT_THROW(instance.makePIDFileName(), InvalidOperation);

    // Verify that config file must contain a file name
    instance.setProcName("myproc");
    instance.setConfigFile(".txt");
    EXPECT_THROW(instance.makePIDFileName(), BadValue);
    instance.setConfigFile("/tmp/");
    EXPECT_THROW(instance.makePIDFileName(), BadValue);

    // Given a valid config file name and proc name we should good to go
    instance.setConfigFile("/tmp/test.conf");
    std::string name;
    EXPECT_NO_THROW(name = instance.makePIDFileName());

    // Make sure the name is as we expect
    std::ostringstream stream;
    stream  << instance.getPIDFileDir() << "/test.myproc.pid";
    EXPECT_EQ(stream.str(), name);

    // Verify that the default directory can be overridden
    instance.setPIDFileDir("/tmp");
    EXPECT_NO_THROW(name = instance.makePIDFileName());
    EXPECT_EQ("/tmp/test.myproc.pid", name);
}

// Verifies the creation a PID file and that a pre-existing PID file
// which points to a live PID causes a throw.
TEST_F(DaemonTest, createPIDFile) {
    DaemonImpl instance;

    instance.setConfigFile("test.conf");
    instance.setProcName("daemon_test");
    instance.setPIDFileDir(TEST_DATA_BUILDDIR);

    EXPECT_NO_THROW(instance.createPIDFile());

    std::ostringstream stream;
    stream  << TEST_DATA_BUILDDIR << "/test.daemon_test.pid";
    EXPECT_EQ(stream.str(), instance.getPIDFileName());

    // If we try again, we should see our own PID file and fail
    EXPECT_THROW(instance.createPIDFile(), DaemonPIDExists);
}

// Verifies that a pre-existing PID file which points to a dead PID
// is overwritten.
TEST_F(DaemonTest, createPIDFileOverwrite) {
    DaemonImpl instance;

    // We're going to use fork to generate a PID we KNOW is dead.
    int pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        char name[] = TEST_SCRIPT_SH;
        char* argv[] = { name, 0 };
        char* envp[] = { 0 };
        execve(name, argv, envp);
        _exit(0);
    }

    // Back in the parent test, we need to wait for the child to die
    // As with debugging waitpid() can be interrupted ignore EINTR.
    int stat;
    int ret;
    do {
        ret = waitpid(pid, &stat, 0);
    } while ((ret == -1) && (errno == EINTR));
    ASSERT_EQ(ret, pid);

    // Ok, so we should now have a PID that we know to be dead.
    // Let's use it to create a PID file.
    instance.setConfigFile("test.conf");
    instance.setProcName("daemon_test");
    instance.setPIDFileDir(TEST_DATA_BUILDDIR);
    EXPECT_NO_THROW(instance.createPIDFile(pid));

    // If we try to create the PID file again, this should work.
    EXPECT_NO_THROW(instance.createPIDFile());
}

// Verifies that Daemon destruction deletes the PID file
TEST_F(DaemonTest, PIDFileCleanup) {
    boost::shared_ptr<DaemonImpl> instance;
    instance.reset(new DaemonImpl());

    instance->setConfigFile("test.conf");
    instance->setProcName("daemon_test");
    instance->setPIDFileDir(TEST_DATA_BUILDDIR);
    EXPECT_NO_THROW(instance->createPIDFile());

    // If we try again, we should see our own PID file
    EXPECT_THROW(instance->createPIDFile(), DaemonPIDExists);

    // Save the pid file name
    std::string pid_file_name = instance->getPIDFileName();

    // Now delete the Daemon instance.  This should remove the
    // PID file.
    instance.reset();

    struct stat stat_buf;
    ASSERT_EQ(-1, stat(pid_file_name.c_str(), &stat_buf));
    EXPECT_EQ(errno, ENOENT);
}

// Checks that configureLogger method is behaving properly.
// More dedicated tests are available for LogConfigParser class.
// See logger_unittest.cc
TEST_F(DaemonTest, parsingConsoleOutput) {
    Daemon::setVerbose(false);

    // Storage - parsed configuration will be stored here
    ConfigPtr storage(new ConfigBase());

    const char* config_txt =
    "{ \"loggers\": ["
    "    {"
    "        \"name\": \"kea\","
    "        \"output-options\": ["
    "            {"
    "                \"output\": \"stdout\""
    "            }"
    "        ],"
    "        \"debuglevel\": 99,"
    "        \"severity\": \"DEBUG\""
    "    }"
    "]}";
    ConstElementPtr config = Element::fromJSON(config_txt);

    // Spawn a daemon and tell it to configure logger
    Daemon x;
    EXPECT_NO_THROW(x.configureLogger(config, storage));

    // The parsed configuration should be processed by the daemon and
    // stored in configuration storage.
    ASSERT_EQ(1, storage->getLoggingInfo().size());

    EXPECT_EQ("kea", storage->getLoggingInfo()[0].name_);
    EXPECT_EQ(99, storage->getLoggingInfo()[0].debuglevel_);
    EXPECT_EQ(isc::log::DEBUG, storage->getLoggingInfo()[0].severity_);

    ASSERT_EQ(1, storage->getLoggingInfo()[0].destinations_.size());
    EXPECT_EQ("stdout" , storage->getLoggingInfo()[0].destinations_[0].output_);
}

TEST_F(DaemonTest, exitValue) {
    DaemonImpl instance;

    EXPECT_EQ(EXIT_SUCCESS, instance.getExitValue());
    instance.setExitValue(77);
    EXPECT_EQ(77, instance.getExitValue());
}

// More tests will appear here as we develop Daemon class.

}
// Added unit tests for isc::process::Daemon public interface.
// Test framework: Google Test (gtest).
// These tests focus on logic exercised in the recent diff of Daemon methods.

#include <gtest/gtest.h>
#include <process/daemon.h>
#include <cc/data.h>
#include <util/filesystem.h>

#include <cstdlib>
#include <string>
#include <fstream>

using namespace isc;
using namespace isc::process;
using namespace isc::data;
using namespace isc::util;
using namespace isc::util::file;

namespace {

// Minimal concrete Daemon for testing (Daemon may have pure virtuals in header).
class TestDaemon : public Daemon {
public:
    TestDaemon() : Daemon() {}
    // Provide no-op overrides for any pure virtuals if they exist.
    // If Daemon has pure virtual run/init/shutdown methods, keep them trivial.
    // In the provided snippet shutdown() and cleanup() are concrete; still, be safe:
    virtual ~TestDaemon() {}

    // Some Kea daemons expose version; base throws NotImplemented, we keep base behavior.
};

// Helper to temporarily set/unset an environment variable.
class EnvVarGuard {
public:
    EnvVarGuard(const char* key, const char* value, bool overwrite)
        : key_(key) {
        const char* cur = std::getenv(key_);
        if (cur) {
            old_.assign(cur);
            had_old_ = true;
        }
        // Use setenv for portability; overwrite flag as provided.
        setenv(key_, value, overwrite ? 1 : 0);
    }
    ~EnvVarGuard() {
        if (had_old_) {
            setenv(key_, old_.c_str(), 1);
        } else {
            unsetenv(key_);
        }
    }
private:
    const char* key_;
    std::string old_;
    bool had_old_{false};
};

// Unique filename helper in CWD to avoid external dependencies.
static std::string uniqueFile(const std::string& base, const std::string& ext) {
    // Use process ID and a counter from address to add entropy.
    std::ostringstream os;
    os << base << "-" << getpid() << "-" << reinterpret_cast<uintptr_t>(&os) << ext;
    return os.str();
}

} // namespace

// Verbose flag behavior.
TEST(DaemonBasicTest, VerboseFlagSetGet) {
    // Ensure we don't leak state between tests.
    Daemon::setVerbose(false);
    EXPECT_FALSE(Daemon::getVerbose());

    Daemon::setVerbose(true);
    EXPECT_TRUE(Daemon::getVerbose());

    Daemon::setVerbose(false);
    EXPECT_FALSE(Daemon::getVerbose());
}

// loggerInit should set KEA_LOGGER_DESTINATION=stdout if it's unset, and not override if set.
TEST(DaemonBasicTest, LoggerInitSetsEnvWhenUnset) {
    TestDaemon d;
    // Ensure var is unset for this scope.
    unsetenv("KEA_LOGGER_DESTINATION");
    ASSERT_EQ(nullptr, std::getenv("KEA_LOGGER_DESTINATION"));

    d.loggerInit("test-logger", /*verbose*/false);
    const char* dest = std::getenv("KEA_LOGGER_DESTINATION");
    ASSERT_NE(nullptr, dest);
    EXPECT_STREQ("stdout", dest);
}

TEST(DaemonBasicTest, LoggerInitDoesNotOverrideExistingEnv) {
    TestDaemon d;
    EnvVarGuard g("KEA_LOGGER_DESTINATION", "file", /*overwrite*/1);
    ASSERT_STREQ("file", std::getenv("KEA_LOGGER_DESTINATION"));

    // loggerInit uses setenv(..., 0) so it must not override "file".
    d.loggerInit("test-logger", /*verbose*/true);
    ASSERT_STREQ("file", std::getenv("KEA_LOGGER_DESTINATION"));
}

// Config file getters/setters and validation.
TEST(DaemonConfigTest, GetSetConfigFile) {
    TestDaemon d;
    EXPECT_TRUE(d.getConfigFile().empty());
    d.setConfigFile("/etc/kea/kea-dhcp4.conf");
    EXPECT_EQ("/etc/kea/kea-dhcp4.conf", d.getConfigFile());
}

TEST(DaemonConfigTest, CheckConfigFileThrowsWhenUnset) {
    TestDaemon d;
    EXPECT_THROW(d.checkConfigFile(), isc::BadValue);
}

TEST(DaemonConfigTest, CheckConfigFileThrowsWhenMissingFilename) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/"); // Path with no file name (stem empty) should throw.
    EXPECT_THROW(d.checkConfigFile(), isc::BadValue);
}

TEST(DaemonConfigTest, CheckConfigFileAcceptsValidPath) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp6.conf");
    EXPECT_NO_THROW(d.checkConfigFile());
}

// checkWriteConfigFile behavior: same dir, empty parent, different parent, empty stem.
TEST(DaemonConfigTest, CheckWriteConfigFileSameDirectory) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp4.conf");
    std::string out = "/etc/kea/kea-dhcp4-backup.conf";
    EXPECT_NO_THROW(d.checkWriteConfigFile(out));
    EXPECT_EQ("/etc/kea/kea-dhcp4-backup.conf", out);
}

TEST(DaemonConfigTest, CheckWriteConfigFileEmptyParentGetsPrepended) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp6.conf");
    std::string out = "kea-dhcp6-backup.conf"; // no parent directory
    EXPECT_NO_THROW(d.checkWriteConfigFile(out));
    EXPECT_EQ("/etc/kea/kea-dhcp6-backup.conf", out);
}

TEST(DaemonConfigTest, CheckWriteConfigFileDifferentParentThrows) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp4.conf");
    std::string out = "/var/tmp/kea-dhcp4-backup.conf"; // different parent
    EXPECT_THROW(d.checkWriteConfigFile(out), isc::BadValue);
}

TEST(DaemonConfigTest, CheckWriteConfigFileEmptyStemThrows) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp4.conf");
    std::string out = "/etc/kea/"; // missing filename
    EXPECT_THROW(d.checkWriteConfigFile(out), isc::BadValue);
}

// Process name get/set.
TEST(DaemonProcNameTest, GetSetProcName) {
    EXPECT_TRUE(Daemon::getProcName().empty());
    Daemon::setProcName("kea-dhcp4");
    EXPECT_EQ("kea-dhcp4", Daemon::getProcName());
    Daemon::setProcName(""); // allow empty assignment
    EXPECT_TRUE(Daemon::getProcName().empty());
}

// PID file dir get/set and makePIDFileName success/failure.
TEST(DaemonPidTest, MakePIDFileNameHappyPath) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp4.conf");
    Daemon::setProcName("kea-dhcp4");
    d.setPIDFileDir("/tmp"); // override deterministic dir
    const std::string expected = "/tmp/kea-dhcp4.kea-dhcp4.pid";
    EXPECT_EQ(expected, d.makePIDFileName());
}

TEST(DaemonPidTest, MakePIDFileNameThrowsWhenConfigUnset) {
    TestDaemon d;
    Daemon::setProcName("kea-dhcp4");
    d.setPIDFileDir("/tmp");
    EXPECT_THROW(d.makePIDFileName(), isc::InvalidOperation);
}

TEST(DaemonPidTest, MakePIDFileNameThrowsWhenConfigMissingStem) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/"); // no filename
    Daemon::setProcName("kea-dhcp4");
    d.setPIDFileDir("/tmp");
    EXPECT_THROW(d.makePIDFileName(), isc::BadValue);
}

TEST(DaemonPidTest, MakePIDFileNameThrowsWhenProcNameUnset) {
    TestDaemon d;
    d.setConfigFile("/etc/kea/kea-dhcp6.conf");
    Daemon::setProcName("");
    d.setPIDFileDir("/tmp");
    EXPECT_THROW(d.makePIDFileName(), isc::InvalidOperation);
}

// PID file name management without touching filesystem.
TEST(DaemonPidTest, SetPIDFileNameValidations) {
    TestDaemon d;
    // Empty name is invalid.
    EXPECT_THROW(d.setPIDFileName(""), isc::BadValue);

    // Set once is OK; retrieving name should match and lock name non-empty.
    EXPECT_NO_THROW(d.setPIDFileName("/tmp/test-daemon.pid"));
    EXPECT_EQ("/tmp/test-daemon.pid", d.getPIDFileName());
    EXPECT_FALSE(d.getPIDLockName().empty());

    // Setting again should throw InvalidOperation.
    EXPECT_THROW(d.setPIDFileName("/tmp/another.pid"), isc::InvalidOperation);
}

// getVersion should throw NotImplemented in base Daemon.
TEST(DaemonVersionTest, GetVersionThrowsNotImplemented) {
    TestDaemon d;
    EXPECT_THROW(d.getVersion(false), isc::NotImplemented);
}

// Redaction API: default jsonPathsToRedact is empty; redactConfig returns input unchanged.
TEST(DaemonRedactionTest, JsonPathsToRedactEmptyAndNoopRedaction) {
    TestDaemon d;
    // jsonPathsToRedact is non-static in class; call through instance to compile on all setups.
    auto paths = d.jsonPathsToRedact();
    EXPECT_TRUE(paths.empty());

    // Prepare a simple config.
    ConstElementPtr cfg = Element::fromJSON("{\"a\":1,\"b\":{\"c\":\"x\"}}");
    ConstElementPtr redacted = d.redactConfig(cfg);
    // Expect pointer equality due to implementation returning input when no paths.
    EXPECT_EQ(cfg.get(), redacted.get());
}

// writeConfigFile: happy path writes valid JSON, returns >0; failure paths throw.
TEST(DaemonWriteConfigFileTest, WriteConfigFileHappyPath) {
    TestDaemon d;
    const std::string path = uniqueFile("daemon-write-ok", ".json");
    ConstElementPtr cfg = Element::fromJSON("{\"answer\":42}");
    size_t bytes = d.writeConfigFile(path, cfg);
    EXPECT_GT(bytes, 0u);

    // Verify file exists and contains JSON.
    std::ifstream in(path.c_str());
    ASSERT_TRUE(in.good());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();
    EXPECT_NE(std::string::npos, content.find("answer"));
    // Cleanup
    std::remove(path.c_str());
}

TEST(DaemonWriteConfigFileTest, WriteConfigFileNullConfigThrows) {
    TestDaemon d;
    const std::string path = uniqueFile("daemon-write-null", ".json");
    ConstElementPtr null_cfg;
    EXPECT_THROW(d.writeConfigFile(path, null_cfg), isc::Unexpected);
}

TEST(DaemonWriteConfigFileTest, WriteConfigFileNonexistentDirectoryThrows) {
    TestDaemon d;
    const std::string path = std::string("nonexistent_dir_") + std::to_string(getpid()) + "/out.json";
    ConstElementPtr cfg = Element::fromJSON("{\"x\":1}");
    EXPECT_THROW(d.writeConfigFile(path, cfg), isc::Unexpected);
}

