#include "home_video_internal.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

/* The player's core runs outside ShellUI and cannot open files from our folder,
   but it can stream: this hands it the one file over loopback HTTP. */
namespace HomeVideo {
namespace {

std::atomic<int> s_port{0};
std::atomic<int> s_requests{0};
std::atomic<int> s_connections{0};
int s_listen_fd = -1;
/* The one file the server hands out; set by the UI thread. */
pthread_mutex_t s_served_lock = PTHREAD_MUTEX_INITIALIZER;
char s_served[512] = "";

bool send_all(int fd, const char *data, size_t size) {
  while (size > 0) {
#ifdef MSG_NOSIGNAL
    const ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
#else
    const ssize_t sent = send(fd, data, size, 0);
#endif
    if (sent <= 0)
      return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

/* "bytes=first-last", "bytes=first-" or "bytes=-suffix" */
bool parse_range(const char *header, off_t size, off_t *first, off_t *last) {
  const char *value = std::strstr(header, "bytes=");
  if (!value)
    return false;
  value += 6;
  char *end = nullptr;
  if (*value == '-') {
    const long long suffix = std::strtoll(value + 1, &end, 10);
    if (suffix <= 0)
      return false;
    *first = suffix >= size ? 0 : size - suffix;
    *last = size - 1;
    return true;
  }
  const long long from = std::strtoll(value, &end, 10);
  if (!end || *end != '-' || from < 0 || from >= size)
    return false;
  *first = from;
  *last = size - 1;
  if (end[1] >= '0' && end[1] <= '9') {
    const long long to = std::strtoll(end + 1, nullptr, 10);
    if (to >= from && to < size)
      *last = to;
  }
  return true;
}

void serve(int client) {
  /* The request head; nothing past it matters. */
  char request[2048];
  size_t used = 0;
  while (used < sizeof(request) - 1) {
    const ssize_t got = recv(client, request + used,
                             sizeof(request) - 1 - used, 0);
    if (got <= 0)
      return;
    used += static_cast<size_t>(got);
    request[used] = '\0';
    if (std::strstr(request, "\r\n\r\n"))
      break;
  }
  request[used] = '\0';
  const bool head = !std::strncmp(request, "HEAD ", 5);
  if (!head && std::strncmp(request, "GET ", 4)) {
    static const char kBad[] =
        "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send_all(client, kBad, sizeof(kBad) - 1);
    return;
  }
  ++s_requests;

  char served[sizeof(s_served)];
  pthread_mutex_lock(&s_served_lock);
  std::memcpy(served, s_served, sizeof(served));
  pthread_mutex_unlock(&s_served_lock);
  const int file = served[0] ? open(served, O_RDONLY) : -1;
  struct stat st {};
  if (file < 0 || fstat(file, &st) != 0 || st.st_size <= 0) {
    static const char kMissing[] =
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send_all(client, kMissing, sizeof(kMissing) - 1);
    if (file >= 0)
      close(file);
    return;
  }

  off_t first = 0;
  off_t last = st.st_size - 1;
  bool partial = false;
  /* header names are case-insensitive */
  for (char *line = request; (line = std::strstr(line, "\r\n")) != nullptr;) {
    line += 2;
    if (!strncasecmp(line, "Range:", 6)) {
      char *line_end = std::strstr(line, "\r\n");
      if (line_end)
        *line_end = '\0';
      partial = parse_range(line + 6, st.st_size, &first, &last);
      if (line_end)
        *line_end = '\r';
      break;
    }
  }

  char reply[512];
  int length;
  if (partial)
    length = std::snprintf(
        reply, sizeof(reply),
        "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\nContent-Range: bytes %lld-%lld/%lld\r\n"
        "Content-Length: %lld\r\nConnection: close\r\n\r\n",
        static_cast<long long>(first), static_cast<long long>(last),
        static_cast<long long>(st.st_size),
        static_cast<long long>(last - first + 1));
  else
    length = std::snprintf(
        reply, sizeof(reply),
        "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
        "Accept-Ranges: bytes\r\nContent-Length: %lld\r\n"
        "Connection: close\r\n\r\n",
        static_cast<long long>(st.st_size));
  bool ok = length > 0 && send_all(client, reply, static_cast<size_t>(length));

  if (ok && !head && lseek(file, first, SEEK_SET) == first) {
    static constexpr size_t kChunk = 64 * 1024;
    char *buffer = static_cast<char *>(std::malloc(kChunk));
    off_t left = last - first + 1;
    while (buffer && ok && left > 0) {
      const size_t want =
          left > static_cast<off_t>(kChunk) ? kChunk : static_cast<size_t>(left);
      const ssize_t got = read(file, buffer, want);
      if (got <= 0)
        break;
      ok = send_all(client, buffer, static_cast<size_t>(got));
      left -= got;
    }
    std::free(buffer);
  }
  close(file);
}

void *client_thread(void *argument) {
  const int client = static_cast<int>(reinterpret_cast<intptr_t>(argument));
  /* A client that sends nothing must not keep its slot for good. */
  const timeval timeout{5, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  serve(client);
  shutdown(client, SHUT_RDWR);
  close(client);
  --s_connections;
  return nullptr;
}

void *accept_thread(void *) {
  for (;;) {
    const int client = accept(s_listen_fd, nullptr, nullptr);
    if (client < 0) {
      usleep(100 * 1000);
      continue;
    }
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    /* The player holds a stream open and seeks with a second request. */
    if (s_connections.load() >= 8) {
      close(client);
      continue;
    }
    ++s_connections;
    pthread_t thread{};
    if (pthread_create(&thread, nullptr, client_thread,
                       reinterpret_cast<void *>(
                           static_cast<intptr_t>(client))) != 0) {
      --s_connections;
      close(client);
      continue;
    }
    pthread_detach(thread);
  }
  return nullptr;
}

} // namespace

void set_served(const std::string &path) {
  pthread_mutex_lock(&s_served_lock);
  std::snprintf(s_served, sizeof(s_served), "%s", path.c_str());
  pthread_mutex_unlock(&s_served_lock);
}

int server_port() { return s_port.load(); }

int server_requests() { return s_requests.load(); }

/* Bound to 127.0.0.1 on a port the system picks; serves nothing but the
   video file, whatever path is asked for. */
bool start_server() {
  if (s_port.load() > 0)
    return true;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  socklen_t size = sizeof(address);
  if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      listen(fd, 8) != 0 ||
      getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
    close(fd);
    return false;
  }
  s_listen_fd = fd;
  pthread_t thread{};
  if (pthread_create(&thread, nullptr, accept_thread, nullptr) != 0) {
    close(fd);
    s_listen_fd = -1;
    return false;
  }
  pthread_detach(thread);
  s_port = ntohs(address.sin_port);
  return true;
}

} // namespace HomeVideo
