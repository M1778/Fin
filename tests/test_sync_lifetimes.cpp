#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string runSyncProgram(const std::string& source, bool checkLeaks = false,
                           bool wrapDestroy = false) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_sync_lifetime");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
    }
    auto args = std::vector<std::string>{input.string(), "-o", executable.string()};
    if (checkLeaks || wrapDestroy) args = {input.string(), "-c", "-o", object.string()};
    const auto compiled = runFinc(args, {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return "compile failed: " + stripAnsi(compiled.err);
    if (checkLeaks || wrapDestroy) {
        std::vector<std::string> inputs{object.string()};
        if (wrapDestroy) {
            const fs::path native = dir / "destroy.c";
            std::ofstream file(native, std::ios::binary);
            file << R"C(#include <pthread.h>
int __real_pthread_mutex_destroy(pthread_mutex_t* mutex);
static int destroyed;
int __wrap_pthread_mutex_destroy(pthread_mutex_t* mutex) {
    int result = __real_pthread_mutex_destroy(mutex);
    if (result == 0) ++destroyed;
    return result;
}
int fin_destroyed_locks(void) { return destroyed; }
)C";
            file.close();
            inputs.push_back(native.string());
        }
        auto command = fin::linkCommand(inputs, executable.string());
        if (checkLeaks) command.push_back("-fsanitize=leak");
        if (wrapDestroy) command.push_back("-Wl,--wrap=pthread_mutex_destroy");
        const int linked = fin::runProcess(command, output.string(), output.string());
        if (linked != 0) return "link failed: " + readProcessOutput(output.string());
    }
    const int status = fin::runProcess({executable.string()}, output.string(), output.string(),
        checkLeaks ? std::vector<std::pair<std::string, std::string>>{
                         {"LSAN_OPTIONS", "detect_leaks=1:exitcode=23"}}
                   : std::vector<std::pair<std::string, std::string>>{});
    const auto text = readProcessOutput(output.string());
    return status == 0 ? text : "run failed: " + std::to_string(status) + "\n" + text;
}

const char* kMutexLifecycle = R"FIN(
import { Mutex } from sync::std;
fun main() <noret> {
    let empty <Mutex<int>> = Mutex::<int>{};
    empty.free_mutex();
    empty.free_mutex();
    for (let round <int> = 0; round < 16; round++) {
        let mutex <Mutex<int>> = Mutex::<int>::create(round);
        let value <&int> = mutex.lock();
        *value = *value + 3;
        mutex.unlock();
        value = mutex.lock();
        blame *value == round + 3;
        mutex.unlock();
        mutex.free_mutex();
        mutex.free_mutex();
        let created <Mutex<int>> = Mutex::<int>::create(round);
        let heap <&Mutex<int>> = new Mutex::<int>{m: created.m, v: created.v};
        value = heap.lock();
        blame *value == round;
        heap.unlock();
        heap.free_mutex();
        heap.free_mutex();
        delete heap;
    }
    printf("mutex cleanup ok\n");
}
)FIN";

const char* kChannelLifecycle = R"FIN(
import { Channel } from sync::std;
fun main() <noret> {
    let empty <Channel<int>> = Channel::<int>{};
    empty.free_channel();
    empty.free_channel();
    for (let round <int> = 0; round < 16; round++) {
        let channel <Channel<int>> = Channel::<int>::create(3);
        blame channel.send(1);
        blame channel.send(2);
        blame channel.send(3);
        blame !channel.send(99);
        blame channel.recv().unwrap_or(-1) == 1;
        blame channel.send(4);
        blame !channel.send(99);
        blame channel.recv().unwrap_or(-1) == 2;
        blame channel.recv().unwrap_or(-1) == 3;
        blame channel.send(5);
        blame channel.send(6);
        blame channel.recv().unwrap_or(-1) == 4;
        blame channel.recv().unwrap_or(-1) == 5;
        blame channel.recv().unwrap_or(-1) == 6;
        for (let i <int> = 0; i < 128; i++) {
            blame channel.send(i);
            blame channel.recv().unwrap_or(-1) == i;
        }
        blame channel.send(77);
        channel.free_channel();
        channel.free_channel();
        let single <Channel<int>> = Channel::<int>::create(1);
        for (let i <int> = 0; i < 8; i++) {
            blame single.send(i);
            blame !single.send(99);
            blame single.recv().unwrap_or(-1) == i;
        }
        single.free_channel();
        let heap <&Channel<int>> = Channel::<int>::shared(2);
        blame heap.send(42);
        blame heap.recv().unwrap_or(-1) == 42;
        heap.free_channel();
        heap.free_channel();
        delete heap;
    }
    printf("channel cleanup ok\n");
}
)FIN";

const char* kSharedLifecycle = R"FIN(
import { Channel, Thread } from sync::std;
let shared <&Channel<int>>;
fun produce() <noret> {
    for (let i <int> = 0; i < 257; i++) {
        while (!shared.send(i)) {}
    }
}
fun main() <noret> {
    for (let round <int> = 0; round < 4; round++) {
        let channel <&Channel<int>> = Channel::<int>::shared(4);
        shared = channel;
        let producer <Thread> = Thread::spawn(() <noret> => { produce(); });
        for (let i <int> = 0; i < 257; i++) {
            blame channel.recv().unwrap_or(-1) == i;
        }
        producer.join();
        channel.free_channel();
        channel.free_channel();
        delete channel;
    }
    printf("shared cleanup ok\n");
}
)FIN";

}

TEST(Soundness_SyncLifetimes, MutexCleanupIsExplicitAndRepeatedCleanupIsSafe) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kMutexLifecycle), "mutex cleanup ok\n");
}

TEST(Soundness_SyncLifetimes, ChannelCleanupPreservesFifoAcrossFullWrapAndReuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kChannelLifecycle), "channel cleanup ok\n");
}

TEST(Soundness_SyncLifetimes, SharedChannelCanBeFreedAfterItsWorkerJoins) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kSharedLifecycle), "shared cleanup ok\n");
}

TEST(Soundness_SyncLifetimes, CleanupLeavesElementStorageWithItsCaller) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(R"FIN(
import { Mutex, Channel } from sync::std;
fun main() <noret> {
    let value <&int> = new int(73);
    let mutex <Mutex<&int>> = Mutex::<&int>::create(value);
    let channel <Channel<&int>> = Channel::<&int>::create(2);
    blame channel.send(value);
    blame channel.send(value);
    mutex.free_mutex();
    channel.free_channel();
    blame *value == 73;
    delete value;
    printf("caller ownership ok\n");
}
)FIN"), "caller ownership ok\n");
}

TEST(Soundness_SyncLifetimes, ReleasedMutexOperationsRefuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    for (const std::string operation : {"lock()", "unlock()"}) {
        const auto result = runSyncProgram(
            "import { Mutex } from sync::std;\nfun main() <noret> {\n"
            "let value <Mutex<int>> = Mutex::<int>::create(7);\n"
            "value.free_mutex();\nvalue." + operation + ";\n}\n");
        EXPECT_EQ(result.find("run failed:"), 0u) << result;
        EXPECT_NE(result.find("cannot use a released mutex"), std::string::npos) << result;
    }
}

TEST(Soundness_SyncLifetimes, ReleasedChannelOperationsRefuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    for (const std::string operation : {"send(7)", "recv()"}) {
        const auto result = runSyncProgram(
            "import { Channel } from sync::std;\nfun main() <noret> {\n"
            "let value <Channel<int>> = Channel::<int>::create(2);\n"
            "value.free_channel();\nvalue." + operation + ";\n}\n");
        EXPECT_EQ(result.find("run failed:"), 0u) << result;
        EXPECT_NE(result.find("cannot use a released channel"), std::string::npos) << result;
    }
}

#ifdef __linux__
TEST(Soundness_SyncLifetimes, CleanupDestroysEachNativeMutexOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(R"FIN(
import { Mutex, Channel } from sync::std;
@define fin_destroyed_locks() <int>;
fun main() <noret> {
    let mutex <Mutex<int>> = Mutex::<int>::create(7);
    mutex.free_mutex();
    mutex.free_mutex();
    let channel <Channel<int>> = Channel::<int>::create(2);
    blame channel.send(8);
    channel.free_channel();
    channel.free_channel();
    let heap <&Channel<int>> = Channel::<int>::shared(2);
    heap.free_channel();
    heap.free_channel();
    delete heap;
    blame fin_destroyed_locks() == 3;
    printf("native cleanup ok\n");
}
)FIN", false, true), "native cleanup ok\n");
}
#endif

#if defined(FIN_TESTS_SANITIZE) && defined(__linux__)
TEST(Soundness_SyncLifetimes, MutexCleanupIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kMutexLifecycle, true), "mutex cleanup ok\n");
}

TEST(Soundness_SyncLifetimes, ChannelCleanupIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kChannelLifecycle, true), "channel cleanup ok\n");
}

TEST(Soundness_SyncLifetimes, JoinedSharedChannelCleanupIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable sync checks need the LLVM backend";
#endif
    EXPECT_EQ(runSyncProgram(kSharedLifecycle, true), "shared cleanup ok\n");
}
#endif
