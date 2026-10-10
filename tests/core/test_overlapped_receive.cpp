#include <catch_amalgamated.hpp>

#include <rex/system/overlapped_receive.h>

#include <chrono>
#include <cstdint>
#include <deque>

using rex::system::net::OverlappedReceives;
using namespace std::chrono_literals;

namespace {
struct Request {
  uint32_t socket;
  int tag;
};

// A host socket: the datagrams waiting on it, by size.
struct FakeSocket {
  std::deque<int> datagrams;
  int attempts = 0;
  int operator()(const Request&) {
    ++attempts;
    if (datagrams.empty()) return -1;
    const int size = datagrams.front();
    datagrams.pop_front();
    return size;
  }
};

// A clock that only moves when the poll sleeps.
struct FakeTime {
  std::chrono::steady_clock::time_point now{};
  int sleeps = 0;
};

constexpr uint32_t kOverlapped = 0x7000A000;
}  // namespace

TEST_CASE("a waiting datagram completes the receive at once", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket{{120}};
  auto outcome = receives.Start(kOverlapped, {1, 0}, socket);
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kCompleted);
  CHECK(outcome.bytes == 120);
  CHECK(receives.pending() == 0);
}

TEST_CASE("an overlapped receive with nothing waiting stays pending", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  auto outcome = receives.Start(kOverlapped, {1, 0}, socket);
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kPending);
  CHECK(receives.pending() == 1);
}

TEST_CASE("a plain receive with nothing waiting would block", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  auto outcome = receives.Start(0, {1, 0}, socket);
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kWouldBlock);
  CHECK(receives.pending() == 0);
}

TEST_CASE("polling completes a pending receive once its datagram arrives", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  FakeTime time;
  auto now = [&] { return time.now; };
  auto sleep = [&] { ++time.sleeps; time.now += 1ms; };
  receives.Start(kOverlapped, {1, 0}, socket);

  auto first = receives.Poll(kOverlapped, 0ms, socket, now, sleep);
  CHECK(first.status == OverlappedReceives<Request>::Status::kIncomplete);
  CHECK(receives.pending() == 1);

  socket.datagrams.push_back(64);
  auto second = receives.Poll(kOverlapped, 0ms, socket, now, sleep);
  CHECK(second.status == OverlappedReceives<Request>::Status::kCompleted);
  CHECK(second.bytes == 64);
  CHECK(receives.pending() == 0);
}

TEST_CASE("an overlapped with nothing outstanding reads its stored result", "[net]") {
  // A receive that completed at once, or a send (which always does).
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  FakeTime time;
  auto outcome =
      receives.Poll(kOverlapped, 5s, socket, [&] { return time.now; }, [&] { time.now += 1ms; });
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kNotPending);
  CHECK(socket.attempts == 0);
}

TEST_CASE("a wait gives up as incomplete when no datagram comes", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  FakeTime time;
  receives.Start(kOverlapped, {1, 0}, socket);
  auto outcome = receives.Poll(kOverlapped, 5s, socket, [&] { return time.now; },
                               [&] { ++time.sleeps; time.now += 1ms; });
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kIncomplete);
  CHECK(time.now - std::chrono::steady_clock::time_point{} >= 5s);
  CHECK(receives.pending() == 1);
}

TEST_CASE("a wait returns as soon as the datagram arrives", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  FakeTime time;
  receives.Start(kOverlapped, {1, 0}, socket);
  auto outcome = receives.Poll(kOverlapped, 5s, socket, [&] { return time.now; }, [&] {
    if (++time.sleeps == 3) socket.datagrams.push_back(32);
    time.now += 1ms;
  });
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kCompleted);
  CHECK(outcome.bytes == 32);
  CHECK(time.sleeps == 3);
}

TEST_CASE("a new receive on the same overlapped replaces the pending one", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  receives.Start(kOverlapped, {1, 1}, socket);
  receives.Start(kOverlapped, {1, 2}, socket);
  CHECK(receives.pending() == 1);
  int seen_tag = 0;
  auto outcome = receives.Poll(
      kOverlapped, 0ms,
      [&](const Request& r) {
        seen_tag = r.tag;
        return 8;
      },
      [] { return std::chrono::steady_clock::time_point{}; }, [] {});
  CHECK(outcome.status == OverlappedReceives<Request>::Status::kCompleted);
  CHECK(seen_tag == 2);
}

TEST_CASE("closing a socket forgets only its pending receives", "[net]") {
  OverlappedReceives<Request> receives;
  FakeSocket socket;
  receives.Start(0x100, {1, 0}, socket);
  receives.Start(0x200, {2, 0}, socket);
  receives.Forget(1);
  CHECK(receives.pending() == 1);
  FakeTime time;
  auto gone = receives.Poll(0x100, 0ms, socket, [&] { return time.now; }, [] {});
  CHECK(gone.status == OverlappedReceives<Request>::Status::kNotPending);
}
