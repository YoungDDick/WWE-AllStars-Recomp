package xws

import (
	"fmt"
	"net/http"
	"testing"
	"time"
)

// A host that crashes or leaves a match on an error path never deletes its session. Issue
// #2: such sessions stayed listed for days, unjoinable, and filled the lobby list.

type fakeClock struct{ now time.Time }

func (c *fakeClock) Now() time.Time          { return c.now }
func (c *fakeClock) advance(d time.Duration) { c.now = c.now.Add(d) }

func newClockedServer() (*Server, *fakeClock) {
	clock := &fakeClock{now: time.Date(2026, 10, 14, 3, 18, 50, 0, time.UTC)}
	return New(Settings{RendezVousAddress: "10.0.0.5", Now: clock.Now}), clock
}

func signIn(t *testing.T, s *Server, xuid, gamertag string) {
	t.Helper()
	body := fmt.Sprintf(`{"xuid":%q,"machineId":"fa00000000000a01","hostAddress":"198.51.100.4",`+
		`"gamertag":%q,"macAddress":"02aabbcc0a01"}`, xuid, gamertag)
	if r := do(t, s, "POST", "/players", "198.51.100.4:1", body); r.Code != http.StatusCreated {
		t.Fatalf("register: status %d %s", r.Code, r.Body.String())
	}
}

func hostSession(t *testing.T, s *Server, id, xuid string) {
	t.Helper()
	createSession(t, s, id, xuid, "198.51.100.4", buildA, 2)
	setProperties(t, s, id, 1, 0)
}

func TestASessionGoesWhenItsHostSignsInAgain(t *testing.T) {
	s, _ := newClockedServer()
	signIn(t, s, "0009000000000A01", "Host")
	hostSession(t, s, "80000000000000a1", "0009000000000A01")

	signIn(t, s, "0009000000000A01", "Host")

	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatalf("%d results, want the abandoned session gone", len(results))
	}
}

func TestAnotherPlayersSignInKeepsTheSession(t *testing.T) {
	s, _ := newClockedServer()
	hostSession(t, s, "80000000000000a1", "0009000000000A01")

	signIn(t, s, "0009000000000B02", "Searcher")

	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 1 {
		t.Fatalf("%d results, want 1", len(results))
	}
}

func TestAnIdleSessionExpires(t *testing.T) {
	s, clock := newClockedServer()
	hostSession(t, s, "80000000000000a1", "0009000000000A01")

	clock.advance(SessionIdleLimit - time.Minute)
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 1 {
		t.Fatalf("%d results before the limit, want 1", len(results))
	}
	clock.advance(2 * time.Minute)
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatalf("%d results after the limit, want 0", len(results))
	}
	path := "/title/54510866/sessions/80000000000000a1"
	if r := do(t, s, "GET", path, "1.1.1.1:1", ""); r.Code != http.StatusNotFound {
		t.Fatalf("details of an expired session: status %d, want 404", r.Code)
	}
}

func TestActivityKeepsASessionAlive(t *testing.T) {
	s, clock := newClockedServer()
	hostSession(t, s, "80000000000000a1", "0009000000000A01")

	clock.advance(SessionIdleLimit - time.Minute)
	path := "/title/54510866/sessions/80000000000000a1/join"
	if r := do(t, s, "POST", path, "1.1.1.1:1", `{"xuids":["0009000000000A01"]}`); r.Code != http.StatusCreated {
		t.Fatalf("join: status %d", r.Code)
	}
	clock.advance(SessionIdleLimit - time.Minute)

	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 1 {
		t.Fatalf("%d results, want the active session listed", len(results))
	}
}

func TestSearchersLookingAtASessionDoNotKeepItAlive(t *testing.T) {
	s, clock := newClockedServer()
	hostSession(t, s, "80000000000000a1", "0009000000000A01")

	for range 3 {
		clock.advance(SessionIdleLimit / 3)
		search(t, s, "0009000000000B02", buildA, 1, 0)
		do(t, s, "GET", "/title/54510866/sessions/80000000000000a1/qos", "1.1.1.1:1", "")
		do(t, s, "GET", "/title/54510866/sessions/80000000000000a1/details", "1.1.1.1:1", "")
	}

	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatalf("%d results, want the session expired", len(results))
	}
}

func TestRelayedTrafficKeepsAMatchsSessionAlive(t *testing.T) {
	s, clock := newClockedServer()
	hostSession(t, s, "80000000000000a1", "0009000000000A01")
	path := "/title/54510866/sessions/80000000000000a1/join"
	body := `{"xuids":["0009000000000A01","0009000000000B02"]}`
	if r := do(t, s, "POST", path, "1.1.1.1:1", body); r.Code != http.StatusCreated {
		t.Fatalf("join: status %d", r.Code)
	}

	for range 4 {
		clock.advance(SessionIdleLimit / 2)
		search(t, s, "0009000000000C03", buildA, 1, 0) // other players' searches expire sessions
		if !s.MayRelay(0x0009000000000A01, 0x0009000000000B02) {
			t.Fatal("the relay refused two players in one match")
		}
	}
}
