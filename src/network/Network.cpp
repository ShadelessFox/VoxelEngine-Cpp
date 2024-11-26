#include "Network.hpp"

#pragma comment(lib, "Ws2_32.lib")

#define NOMINMAX
#include <curl/curl.h>
#include <stdexcept>
#include <limits>
#include <queue>

#ifdef _WIN32
/// included in curl.h
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using SOCKET = int;
#endif // _WIN32

#include <chrono>
#include <thread>

#include "debug/Logger.hpp"
#include "util/stringutil.hpp"

using namespace network;

static debug::Logger logger("network");

static size_t write_callback(
    char* ptr, size_t size, size_t nmemb, void* userdata
) {
    auto& buffer = *reinterpret_cast<std::vector<char>*>(userdata);
    size_t psize = buffer.size();
    buffer.resize(psize + size * nmemb);
    std::memcpy(buffer.data() + psize, ptr, size * nmemb);
    return size * nmemb;
}

struct Request {
    std::string url;
    OnResponse onResponse;
    OnReject onReject;
    long maxSize;
    bool followLocation = false;
};

class CurlRequests : public Requests {
    CURLM* multiHandle;
    CURL* curl;

    size_t totalUpload = 0;
    size_t totalDownload = 0;

    OnResponse onResponse;
    OnReject onReject;
    std::vector<char> buffer;
    std::string url;

    std::queue<Request> requests;
public:
    CurlRequests(CURLM* multiHandle, CURL* curl)
        : multiHandle(multiHandle), curl(curl) {
    }

    virtual ~CurlRequests() {
        curl_easy_cleanup(curl);
        curl_multi_remove_handle(multiHandle, curl);
        curl_multi_cleanup(multiHandle);
    }

    void get(
        const std::string& url,
        OnResponse onResponse,
        OnReject onReject,
        long maxSize
    ) override {
        Request request {url, onResponse, onReject, maxSize};
        if (url.empty()) {
            processRequest(request);
        } else {
            requests.push(request);
        }
    }

    void processRequest(const Request& request) {
        onResponse = request.onResponse;
        onReject = request.onReject;
        url = request.url;

        buffer.clear();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.followLocation);
        if (request.maxSize == 0) {
            curl_easy_setopt(
                curl, CURLOPT_MAXFILESIZE, std::numeric_limits<long>::max()
            );
        } else {
            curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, request.maxSize);
        }
        curl_multi_add_handle(multiHandle, curl);
        int running;
        CURLMcode res = curl_multi_perform(multiHandle, &running);
        if (res != CURLM_OK) {
            auto message = curl_multi_strerror(res);
            logger.error() << message << " (" << url << ")";
            if (onReject) {
                onReject(message);
            }
            url = "";
        }
    }

    void update() override {
        int messagesLeft;
        int running;
        CURLMsg* msg;
        CURLMcode res = curl_multi_perform(multiHandle, &running);
        if (res != CURLM_OK) {
            auto message = curl_multi_strerror(res);
            logger.error() << message << " (" << url << ")";
            if (onReject) {
                onReject(message);
            }
            curl_multi_remove_handle(multiHandle, curl);
            url = "";
            return;
        }
        if ((msg = curl_multi_info_read(multiHandle, &messagesLeft)) != NULL) {
            if(msg->msg == CURLMSG_DONE) {
                curl_multi_remove_handle(multiHandle, curl);
            }
            int response;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &response);
            if (response == 200) {
                long size;
                if (!curl_easy_getinfo(curl, CURLINFO_REQUEST_SIZE, &size)) {
                    totalUpload += size;
                }
                if (!curl_easy_getinfo(curl, CURLINFO_HEADER_SIZE, &size)) {
                    totalDownload += size;
                }
                totalDownload += buffer.size();
                if (onResponse) {
                    onResponse(std::move(buffer));
                }
            } else {
                logger.error() << "response code " << response << " (" << url << ")";
                if (onReject) {
                    onReject(std::to_string(response).c_str());
                }
            }
            url = "";
        }
        if (url.empty() && !requests.empty()) {
            auto request = std::move(requests.front());
            requests.pop();
            processRequest(request);
        }
    }

    size_t getTotalUpload() const override {
        return totalUpload;
    }

    size_t getTotalDownload() const override {
        return totalDownload;
    }

    static std::unique_ptr<CurlRequests> create() {
        auto curl = curl_easy_init();
        if (curl == nullptr) {
            throw std::runtime_error("could not initialzie cURL");
        }
        auto multiHandle = curl_multi_init();
        if (multiHandle == nullptr) {
            curl_easy_cleanup(curl);
            throw std::runtime_error("could not initialzie cURL-multi");
        }
        return std::make_unique<CurlRequests>(multiHandle, curl);
    }
};

#ifndef _WIN32
static inline int closesocket(int descriptor) noexcept {
    return close(descriptor);
}
static inline std::runtime_error handle_socket_error(const std::string& message) {
    int err = errno;
    return std::runtime_error(
        message+" [errno=" + std::to_string(err) + "]: " + 
        std::string(strerror(err))
    );
}
#else
static inline std::runtime_error handle_socket_error(const std::string& message) {
    int errorCode = WSAGetLastError();
    wchar_t* s = nullptr;
    size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&s,
        0,
        nullptr
    );
    assert(s != nullptr);
    while (size && isspace(s[size-1])) {
        s[--size] = 0;
    }
    auto errorString = util::wstr2str_utf8(std::wstring(s));
    LocalFree(s);
    return std::runtime_error(message+" [WSA error=" + 
           std::to_string(errorCode) + "]: "+errorString);
}
#endif

static inline int connectsocket(
    int descriptor, const sockaddr* addr, socklen_t len
) noexcept {
    return connect(descriptor, addr, len);
}

static inline int recvsocket(
    int descriptor, char* buf, size_t len
) noexcept {
    return recv(descriptor, buf, len, 0);
}

static inline int sendsocket(
    int descriptor, const char* buf, size_t len, int flags
) noexcept {
    return send(descriptor, buf, len, flags);
}

static std::string to_string(const addrinfo* addr) {
    if (addr->ai_family == AF_INET) {
        auto psai = reinterpret_cast<sockaddr_in*>(addr->ai_addr);
        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(addr->ai_family, &(psai->sin_addr), ip, INET_ADDRSTRLEN)) {
            return std::string(ip);
        }
    } else if (addr->ai_family == AF_INET6) {
        auto psai = reinterpret_cast<sockaddr_in6*>(addr->ai_addr);
        char ip[INET6_ADDRSTRLEN];
        if (inet_ntop(addr->ai_family, &(psai->sin6_addr), ip, INET6_ADDRSTRLEN)) {
            return std::string(ip);
        }
    }
    return "";
}

class SocketImpl : public Socket {
    SOCKET descriptor;
    bool open = true;
    addrinfo* addr;
    size_t totalUpload = 0;
    size_t totalDownload = 0;
public:
    SocketImpl(SOCKET descriptor, addrinfo* addr)
        : descriptor(descriptor), addr(addr) {
    }

    ~SocketImpl() {
        closesocket(descriptor);
        freeaddrinfo(addr);
    }

    int recv(char* buffer, size_t length) override {
        int len = recvsocket(descriptor, buffer, length);
        if (len == 0) {
            int err = errno;
            close();
            throw std::runtime_error(
                "Read failed [errno=" + std::to_string(err) +
                "]: " + std::string(strerror(err))
            );
        } else if (len == -1) {
            return 0;
        }
        totalDownload += len;
        return len;
    }

    int send(const char* buffer, size_t length) override {
        int len = sendsocket(descriptor, buffer, length, 0);
        if (len == -1) {
            int err = errno;
            close();
            throw std::runtime_error(
                "Send failed [errno=" + std::to_string(err) +
                "]: " + std::string(strerror(err))
            );
        }
        totalUpload += len;
        return len;
    }

    void close() override {
        closesocket(descriptor);
        open = false;
    }

    bool isOpen() const override {
        return open;
    }

    size_t getTotalUpload() const override {
        return totalUpload;
    }

    size_t getTotalDownload() const override {
        return totalDownload;
    }

    static std::shared_ptr<SocketImpl> connect(
        const std::string& address, int port
    ) {
        addrinfo hints {};

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* addrinfo;
        if (int res = getaddrinfo(
            address.c_str(), std::to_string(port).c_str(), &hints, &addrinfo
        )) {
            throw std::runtime_error(gai_strerror(res));
        }
        SOCKET descriptor = socket(
            addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol
        );
        if (descriptor == -1) {
            freeaddrinfo(addrinfo);
            throw std::runtime_error("Could not create socket");
        }
#ifdef _WIN32
        u_long mode = 1;
        auto err = ioctlsocket(descriptor, FIONBIO, &mode);
        if (err != NO_ERROR) {
            throw std::runtime_error(
                "Could not set to non-blocking mode [errno=" + std::to_string(err) +
                "]: " + std::string(strerror(err))
            );
        }
#else
        if (fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0) {
            freeaddrinfo(addrinfo);
            closesocket(descriptor);
            throw std::runtime_error("Failed to make socket non-blocking");
        }
#endif

        int res = connectsocket(descriptor, addrinfo->ai_addr, addrinfo->ai_addrlen);
        if (res == -1) {
#           ifdef _WIN32
                if (WSAGetLastError() != WSAEWOULDBLOCK) {
                    auto error = handle_socket_error("Connect failed");
                    closesocket(descriptor);
                    freeaddrinfo(addrinfo);
                    throw error;
                }
#           else
                if (errno != EINPROGRESS) {
                    auto error = handle_socket_error("Connect failed");
                    closesocket(descriptor);
                    freeaddrinfo(addrinfo);
                    throw error;
                }
#           endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        logger.info() << "connected to " << address << " ["
                      << to_string(addrinfo) << ":" << port << "]";
        return std::make_shared<SocketImpl>(descriptor, addrinfo);
    }
};

Network::Network(std::unique_ptr<Requests> requests)
    : requests(std::move(requests)) {
}

Network::~Network() = default;

void Network::get(
    const std::string& url,
    OnResponse onResponse,
    OnReject onReject,
    long maxSize
) {
    requests->get(url, onResponse, onReject, maxSize);
}

Socket* Network::getConnection(u64id_t id) const {
    const auto& found = connections.find(id);
    if (found == connections.end()) {
        return nullptr;
    }
    return found->second.get();
}

u64id_t Network::connect(const std::string& address, int port) {
    auto socket = SocketImpl::connect(address, port);
    u64id_t id = nextConnection++;
    connections[id] = std::move(socket);
    return id;
}

size_t Network::getTotalUpload() const {
    size_t totalUpload = 0;
    for (const auto& [_, socket] : connections) {
        totalUpload += socket->getTotalUpload();
    }
    return requests->getTotalUpload() + totalUpload;
}

size_t Network::getTotalDownload() const {
    size_t totalDownload = 0;
    for (const auto& [_, socket] : connections) {
        totalDownload += socket->getTotalDownload();
    }
    return requests->getTotalDownload() + totalDownload;
}

void Network::update() {
    requests->update();
}

std::unique_ptr<Network> Network::create(const NetworkSettings& settings) {
    auto requests = CurlRequests::create();
    return std::make_unique<Network>(std::move(requests));
}
