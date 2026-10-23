#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

std::string runResourceProgram(const std::string& source, bool checkLeaks = false) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_resource_cleanup");
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
    if (checkLeaks) args = {input.string(), "-c", "-o", object.string()};
    const auto compiled = runFinc(args, {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return "compile failed: " + stripAnsi(compiled.err);
    if (checkLeaks) {
        auto command = fin::linkCommand({object.string()}, executable.string());
        command.push_back("-fsanitize=leak");
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

const char* kCdkLifecycle = R"FIN(
import { calc_compile, calc_eval, cdk_lower, cdk_emit_text, cdk_eval_ir } from cdk::std;
import { Outcome } from outcome::std;
import { IrFunction, free_ir_function } from ir::std;
import { equals, free_str } from strings::std;
fun main() <noret> {
    for (let round <int> = 0; round < 16; round++) {
        blame calc_eval("(2 + 3) * 4 - 6 / 2") == 17;
        let built <Outcome<string>> = calc_compile("2 + 3 * 4");
        blame built.is_ok();
        blame equals(built.value, "%t0 = const_int 2\n%t1 = const_int 3\n%t2 = const_int 4\n%t3 = mul %t1 %t2\n%t4 = add %t0 %t3\nret %t4\n");
        free_str(built.value);
        let lowered <Outcome<IrFunction>> = cdk_lower("1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16+17+18");
        blame lowered.is_ok();
        blame cdk_eval_ir(lowered.value) == 171;
        let emitted <string> = cdk_emit_text(lowered.value);
        free_ir_function(lowered.value);
        blame !equals(emitted, "");
        free_str(emitted);
    }
    printf("cdk cleanup ok\n");
}
)FIN";

const char* kCdkErrors = R"FIN(
import { calc_compile, cdk_lower } from cdk::std;
import { Outcome } from outcome::std;
import { IrFunction } from ir::std;
import { equals, free_str } from strings::std;
fun expect_error(source: string, message: string) <noret> {
    let lowered <Outcome<IrFunction>> = cdk_lower(source);
    blame lowered.is_err();
    blame equals(lowered.message, message);
    let compiled <Outcome<string>> = calc_compile(source);
    blame compiled.is_err();
    blame equals(compiled.message, message);
    free_str(compiled.message);
}
fun main() <noret> {
    for (let round <int> = 0; round < 16; round++) {
        expect_error("", "calc: expected an expression");
        expect_error("name", "calc: expected an expression");
        expect_error("let true int", "calc: expected an expression");
        expect_error("`", "calc: expected an expression");
        expect_error("1 -", "calc: expected an expression");
        expect_error("1 *", "calc: expected an expression");
        expect_error("1 + (2 *", "calc: expected an expression");
        expect_error("(1 + 2", "calc: expected ')'");
        expect_error("1 2", "calc: unexpected input after the expression");
        expect_error("\"text\" 'c' 1.2 @", "calc: expected an expression");
        expect_error("/*", "calc: expected an expression");
        expect_error("\"unfinished", "calc: expected an expression");
    }
    printf("cdk errors ok\n");
}
)FIN";

const char* kIrOwnership = R"FIN(
import { IrFunction, IrBlock, new_ir_function, make_ir_const_int, free_ir_function } from ir::std;
import { clone, equals, free_str } from strings::std;
fun main() <noret> {
    let supplied <string> = clone("caller owned");
    let function <IrFunction> = new_ir_function(supplied, supplied);
    function.add_block(supplied);
    let block <IrBlock> = function.blocks[0];
    block.emit(make_ir_const_int(supplied, 7));
    for (let i <int> = 0; i < 40; i++) {
        let temp <string> = function.fresh_temp();
        block.emit(make_ir_const_int(temp, i));
    }
    function.blocks[0] = block;
    blame equals(block.instructions[1].dest, "%t0");
    blame equals(block.instructions[40].dest, "%t39");
    free_ir_function(function);
    blame equals(supplied, "caller owned");
    free_str(supplied);
    printf("ir ownership ok\n");
}
)FIN";

const char* kScannerLifecycle = R"FIN(
import { Scanner } from scanner::std;
import { equals, free_str } from strings::std;
fun main() <noret> {
    for (let round <int> = 0; round < 16; round++) {
        let scanner <Scanner> = Scanner{};
        scanner.destroy();
        scanner.destroy();
        scanner.init("");
        blame scanner.is_at_end();
        scanner.destroy();
        scanner.destroy();
        scanner.init("7");
        let token <auto> = scanner.scan_token();
        blame equals(token.text, "7");
        free_str(token.text);
        scanner.destroy();
    }
    printf("scanner cleanup ok\n");
}
)FIN";

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

class HttpPeer {
public:
    HttpPeer() {
#ifdef _WIN32
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!initialized_) return;
#endif
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ == kInvalidSocket) return;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return;
        SocketLength length = sizeof(address);
        if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) != 0) return;
        if (::listen(socket_, 8) != 0) return;
        port_ = ntohs(address.sin_port);
    }
    ~HttpPeer() {
        if (socket_ != kInvalidSocket) closeSocket(socket_);
#ifdef _WIN32
        if (initialized_) WSACleanup();
#endif
    }
    int port() const { return port_; }
    std::string serve() {
        for (int round = 0; round < 4; ++round) {
            for (int response = 0; response < 3; ++response) {
                fd_set ready;
                FD_ZERO(&ready);
                FD_SET(socket_, &ready);
                timeval timeout{};
                timeout.tv_sec = 15;
                if (::select(static_cast<int>(socket_ + 1), &ready, nullptr, nullptr, &timeout) <= 0)
                    return "HTTP peer timed out waiting for a request";
                const SocketHandle client = ::accept(socket_, nullptr, nullptr);
                if (client == kInvalidSocket) return "HTTP peer could not accept";
                struct CloseClient {
                    SocketHandle socket;
                    ~CloseClient() { closeSocket(socket); }
                } close{client};
#ifdef _WIN32
                DWORD receiveTimeout = 15000;
#else
                timeval receiveTimeout{};
                receiveTimeout.tv_sec = 15;
#endif
                ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
#ifdef SO_NOSIGPIPE
                int one = 1;
                ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                std::string request;
                while (request.find("\r\n\r\n") == std::string::npos && request.size() < 4096) {
                    char data[256];
                    const int count = static_cast<int>(::recv(client, data, sizeof(data), 0));
                    if (count <= 0) return "HTTP peer could not read the request";
                    request.append(data, static_cast<size_t>(count));
                }
                if (request != "GET /probe HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n")
                    return "HTTP peer received the wrong request: " + request;
                const std::string answer = response == 0 ? "HTTP/1.0 200 OK\r\n\r\nhello" :
                                           response == 1 ? "" : std::string(5000, 'x');
                size_t sent = 0;
                while (sent < answer.size()) {
                    int flags = 0;
#ifdef MSG_NOSIGNAL
                    flags = MSG_NOSIGNAL;
#endif
                    const int count = static_cast<int>(::send(client, answer.data() + sent,
                        static_cast<int>(answer.size() - sent), flags));
                    if (count <= 0) return "HTTP peer could not send its response";
                    sent += static_cast<size_t>(count);
                }
            }
        }
        return "";
    }

private:
    SocketHandle socket_ = kInvalidSocket;
    int port_ = 0;
#ifdef _WIN32
    bool initialized_ = false;
#endif
};

std::string runHttpProgram(bool checkLeaks = false) {
    HttpPeer peer;
    if (peer.port() == 0) return "HTTP peer could not listen";
    const std::string source = R"FIN(
import { http_get } from networking::std;
import { equals, len, free_str } from strings::std;
fun main() <noret> {
    for (let round <int> = 0; round < 4; round++) {
        let normal <string> = http_get("127.0.0.1", PORT, "/probe");
        blame equals(normal, "HTTP/1.0 200 OK\r\n\r\nhello");
        free_str(normal);
        let empty <string> = http_get("127.0.0.1", PORT, "/probe");
        blame equals(empty, "");
        free_str(empty);
        let capped <string> = http_get("127.0.0.1", PORT, "/probe");
        blame len(capped) == 4096;
        free_str(capped);
    }
    printf("http cleanup ok\n");
}
)FIN";
    std::string program = source;
    size_t position = 0;
    while ((position = program.find("PORT", position)) != std::string::npos) {
        program.replace(position, 4, std::to_string(peer.port()));
    }
    std::string peerResult;
    std::thread worker([&] { peerResult = peer.serve(); });
    const std::string result = runResourceProgram(program, checkLeaks);
    worker.join();
    return peerResult.empty() ? result : peerResult + "\n" + result;
}

}

TEST(Soundness_StdlibResourceCleanup, CdkCompileAndEvalKeepReturnedValuesAlive) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kCdkLifecycle), "cdk cleanup ok\n");
}

TEST(Soundness_StdlibResourceCleanup, CdkErrorsRetainStageMessagesAcrossRepeatedCalls) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kCdkErrors), "cdk errors ok\n");
}

TEST(Soundness_StdlibResourceCleanup, IrCleanupKeepsSuppliedStringsCallerOwned) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kIrOwnership), "ir ownership ok\n");
}

TEST(Soundness_StdlibResourceCleanup, ScannerCleanupAcceptsDefaultAndEmptyScannersAndReuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kScannerLifecycle), "scanner cleanup ok\n");
}

TEST(Soundness_StdlibResourceCleanup, HttpGetReturnsOwnedResponsesAndPreservesTheSizeCap) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runHttpProgram(), "http cleanup ok\n");
}

TEST(Soundness_StdlibResourceCleanup, CdkDivisionByZeroRefusesWithAStageMessage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    const auto result = runResourceProgram(R"FIN(
import { calc_eval } from cdk::std;
fun main() <noret> { calc_eval("1 / (3 - 3)"); }
)FIN");
    EXPECT_EQ(result.find("run failed:"), 0u) << result;
    EXPECT_NE(result.find("calc: division by zero"), std::string::npos) << result;
}

TEST(Soundness_StdlibResourceCleanup, CdkUnrepresentableDivisionRefusesWithAStageMessage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    const auto result = runResourceProgram(R"FIN(
import { calc_eval } from cdk::std;
fun main() <noret> { calc_eval("(0 - 2147483647 - 1) / (0 - 1)"); }
)FIN");
    EXPECT_EQ(result.find("run failed:"), 0u) << result;
    EXPECT_NE(result.find("calc: integer division overflow"), std::string::npos) << result;
}

#if defined(FIN_TESTS_SANITIZE) && defined(__linux__)
TEST(Soundness_StdlibResourceCleanup, CdkCompileAndEvalAreLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kCdkLifecycle, true), "cdk cleanup ok\n");
}

TEST(Soundness_StdlibResourceCleanup, CdkErrorCleanupIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kCdkErrors, true), "cdk errors ok\n");
}

TEST(Soundness_StdlibResourceCleanup, IrNameGrowthAndCleanupAreLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kIrOwnership, true), "ir ownership ok\n");
}

TEST(Soundness_StdlibResourceCleanup, RepeatedHttpGetIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runHttpProgram(true), "http cleanup ok\n");
}

TEST(Soundness_StdlibResourceCleanup, ScannerEmptySourceCleanupIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable resource checks need the LLVM backend";
#endif
    EXPECT_EQ(runResourceProgram(kScannerLifecycle, true), "scanner cleanup ok\n");
}
#endif
