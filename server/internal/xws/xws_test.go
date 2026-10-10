package xws

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strings"
	"testing"
)

func do(t *testing.T, h http.Handler, method, path, remote, body string) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(method, path, strings.NewReader(body))
	request.RemoteAddr = remote
	recorder := httptest.NewRecorder()
	h.ServeHTTP(recorder, request)
	return recorder
}

func decode(t *testing.T, r *httptest.ResponseRecorder, v any) {
	t.Helper()
	if err := json.Unmarshal(r.Body.Bytes(), v); err != nil {
		t.Fatalf("%v: %s", err, r.Body.String())
	}
}

func newTestServer() *Server { return New(Settings{RendezVousAddress: "10.0.0.5"}) }

func TestWhoamiReturnsTheCallersAddress(t *testing.T) {
	r := do(t, newTestServer(), "GET", "/whoami", "127.0.0.3:50123", "")
	if r.Code != http.StatusOK {
		t.Fatalf("status %d", r.Code)
	}
	var body struct{ Address string }
	decode(t, r, &body)
	if body.Address != "127.0.0.3" {
		t.Fatalf("address %q, want 127.0.0.3", body.Address)
	}
}

// The fork's client sends players as these strings (player_object_json.cc).
const playerA = `{"xuid":"0009000000000A01","machineId":"fa00000000000a01","hostAddress":"127.0.0.2",` +
	`"gamertag":"Alpha","macAddress":"02aabbcc0a01","settings":{}}`
const playerB = `{"xuid":"0009000000000B02","machineId":"fa00000000000b02","hostAddress":"127.0.0.3",` +
	`"gamertag":"Bravo","macAddress":"02aabbcc0b02","settings":{}}`

func TestRegisteredPlayersAreFoundByAddress(t *testing.T) {
	s := newTestServer()
	for _, p := range []string{playerA, playerB} {
		if r := do(t, s, "POST", "/players", "127.0.0.9:1", p); r.Code != http.StatusCreated {
			t.Fatalf("register: status %d %s", r.Code, r.Body.String())
		}
	}
	r := do(t, s, "POST", "/players/find", "127.0.0.2:1", `{"hostAddress":"127.0.0.3"}`)
	if r.Code != http.StatusCreated {
		t.Fatalf("find: status %d", r.Code)
	}
	var found map[string]any
	decode(t, r, &found)
	if found["xuid"] != "0009000000000B02" || found["gamertag"] != "Bravo" ||
		found["macAddress"] != "02aabbcc0b02" || found["hostAddress"] != "127.0.0.3" {
		t.Fatalf("found %v, want Bravo's record", found)
	}
	if s.PlayerCount() != 2 {
		t.Fatalf("%d players, want 2", s.PlayerCount())
	}
}

func TestReRegisteringAPlayerReplacesIt(t *testing.T) {
	s := newTestServer()
	do(t, s, "POST", "/players", "127.0.0.9:1", playerA)
	moved := strings.Replace(playerA, "127.0.0.2", "127.0.0.4", 1)
	do(t, s, "POST", "/players", "127.0.0.9:1", moved)
	if s.PlayerCount() != 1 {
		t.Fatalf("%d players, want 1", s.PlayerCount())
	}
	if r := do(t, s, "POST", "/players/find", "1.1.1.1:1", `{"hostAddress":"127.0.0.2"}`); r.Code != http.StatusNotFound {
		t.Fatalf("old address still found (status %d)", r.Code)
	}
}

func TestFindingAnUnknownAddressFails(t *testing.T) {
	r := do(t, newTestServer(), "POST", "/players/find", "1.1.1.1:1", `{"hostAddress":"127.0.0.7"}`)
	if r.Code != http.StatusNotFound {
		t.Fatalf("status %d, want 404", r.Code)
	}
}

func TestTitleServersListTheRendezVousAddress(t *testing.T) {
	r := do(t, newTestServer(), "GET", "/title/54510866/servers", "127.0.0.2:1", "")
	if r.Code != http.StatusOK {
		t.Fatalf("status %d", r.Code)
	}
	var servers []struct {
		Address     string
		Flags       int
		Description string
	}
	decode(t, r, &servers)
	if len(servers) != 1 || servers[0].Address != "10.0.0.5" {
		t.Fatalf("servers %+v, want one at 10.0.0.5", servers)
	}
}

// whoamiFrom asks whoami from remote with the given X-Forwarded-For (none when empty).
func whoamiFrom(t *testing.T, s *Server, remote, forwarded string) string {
	t.Helper()
	request := httptest.NewRequest("GET", "/whoami", nil)
	request.RemoteAddr = remote
	if forwarded != "" {
		request.Header.Set("X-Forwarded-For", forwarded)
	}
	recorder := httptest.NewRecorder()
	s.ServeHTTP(recorder, request)
	var body struct{ Address string }
	decode(t, recorder, &body)
	return body.Address
}

func newProxiedServer() *Server {
	return New(Settings{RendezVousAddress: "10.0.0.5",
		TrustedProxies: []netip.Prefix{netip.MustParsePrefix("172.29.48.1/32")}})
}

func TestWhoamiTakesTheForwardedAddressFromTheTrustedProxy(t *testing.T) {
	if got := whoamiFrom(t, newProxiedServer(), "172.29.48.1:40000", "203.0.113.7"); got != "203.0.113.7" {
		t.Fatalf("address %q, want 203.0.113.7", got)
	}
}

func TestWhoamiIgnoresForwardedAddressesFromOtherPeers(t *testing.T) {
	if got := whoamiFrom(t, newProxiedServer(), "198.51.100.4:40000", "203.0.113.7"); got != "198.51.100.4" {
		t.Fatalf("address %q, want the peer 198.51.100.4", got)
	}
	if got := whoamiFrom(t, newTestServer(), "172.29.48.1:40000", "203.0.113.7"); got != "172.29.48.1" {
		t.Fatalf("address %q, want the peer when no proxy is trusted", got)
	}
}

// A proxy that appends puts the peer it saw last, so a client's own X-Forwarded-For stays
// on the left (the nginx site replaces the header; this keeps an appending proxy safe).
func TestWhoamiSkipsAddressesTheClientForged(t *testing.T) {
	got := whoamiFrom(t, newProxiedServer(), "172.29.48.1:40000", "10.9.9.9, 203.0.113.7")
	if got != "203.0.113.7" {
		t.Fatalf("address %q, want the address the proxy saw (203.0.113.7)", got)
	}
}

func TestWhoamiFallsBackToThePeerOnABadForwardedAddress(t *testing.T) {
	if got := whoamiFrom(t, newProxiedServer(), "172.29.48.1:40000", "not-an-ip"); got != "172.29.48.1" {
		t.Fatalf("address %q, want the peer 172.29.48.1", got)
	}
}

// Two copies behind one router: one public address, each its own local address.
const playerHomeA = `{"xuid":"0009000000000C01","machineId":"fa00000000000c01","hostAddress":"203.0.113.7",` +
	`"localAddress":"192.168.1.10","gamertag":"Home A","macAddress":"02aabbcc0c01"}`
const playerHomeB = `{"xuid":"0009000000000C02","machineId":"fa00000000000c02","hostAddress":"203.0.113.7",` +
	`"localAddress":"192.168.1.11","gamertag":"Home B","macAddress":"02aabbcc0c02"}`

func registerAll(t *testing.T, s *Server, players ...string) {
	t.Helper()
	for _, p := range players {
		if r := do(t, s, "POST", "/players", "203.0.113.7:1", p); r.Code != http.StatusCreated {
			t.Fatalf("register: status %d %s", r.Code, r.Body.String())
		}
	}
}

func findFrom(t *testing.T, s *Server, remote, address string) (int, string) {
	t.Helper()
	r := do(t, s, "POST", "/players/find", remote, `{"hostAddress":"`+address+`"}`)
	if r.Code != http.StatusCreated {
		return r.Code, ""
	}
	var found struct{ Gamertag string }
	decode(t, r, &found)
	return r.Code, found.Gamertag
}

func TestPlayersWithOnePublicAddressStayDistinct(t *testing.T) {
	s := newTestServer()
	registerAll(t, s, playerHomeA, playerHomeB)
	if s.PlayerCount() != 2 {
		t.Fatalf("%d players, want 2", s.PlayerCount())
	}
	for address, want := range map[string]string{"192.168.1.10": "Home A", "192.168.1.11": "Home B"} {
		if _, got := findFrom(t, s, "203.0.113.7:1", address); got != want {
			t.Fatalf("find %s from the same router: %q, want %q", address, got, want)
		}
	}
}

func TestAPlayerIsFoundByItsPublicAddressFromElsewhere(t *testing.T) {
	s := newTestServer()
	registerAll(t, s, playerHomeA)
	if _, got := findFrom(t, s, "198.51.100.4:1", "203.0.113.7"); got != "Home A" {
		t.Fatalf("find by public address: %q, want Home A", got)
	}
}

// Every home network has a 192.168.1.10: a local address only names a player behind the
// asker's own router.
func TestALocalAddressIsNotFoundFromAnotherNetwork(t *testing.T) {
	s := newTestServer()
	registerAll(t, s, playerHomeA)
	if code, _ := findFrom(t, s, "198.51.100.4:1", "192.168.1.10"); code != http.StatusNotFound {
		t.Fatalf("find a local address from another network: status %d, want 404", code)
	}
}

func TestRegisteringGivesARelayToken(t *testing.T) {
	s := New(Settings{RendezVousAddress: "10.0.0.5", RelayAddress: "10.0.0.5:32482"})
	r := do(t, s, "POST", "/players", "127.0.0.9:1", playerA)
	var body struct{ RelayToken, RelayAddress string }
	decode(t, r, &body)
	if body.RelayAddress != "10.0.0.5:32482" || len(body.RelayToken) != 32 {
		t.Fatalf("registration gave relay %q token %q", body.RelayAddress, body.RelayToken)
	}
	token, from, ok := s.RelayToken(0x0009000000000A01)
	if !ok || fmt.Sprintf("%x", token) != body.RelayToken {
		t.Fatalf("RelayToken %x (ok %v), want the registered %s", token, ok, body.RelayToken)
	}
	if from != netip.MustParseAddr("127.0.0.9") {
		t.Fatalf("registered from %v, want the request's address 127.0.0.9", from)
	}
	if _, _, ok := s.RelayToken(0x0009000000000B02); ok {
		t.Fatal("an unregistered player has a relay token")
	}
}

func TestPlayersShareASessionOnceBothJoined(t *testing.T) {
	s := newTestServer()
	const a, b uint64 = 0x0009000000000A01, 0x0009000000000B02
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", "build", 2)
	join := `{"xuids":["%016X"]}`
	do(t, s, "POST", "/title/54510866/sessions/80000000000000a1/join", "1.1.1.1:1", fmt.Sprintf(join, a))
	if s.MayRelay(a, b) {
		t.Fatal("players may relay before the second found or joined the session")
	}
	do(t, s, "POST", "/title/54510866/sessions/80000000000000a1/join", "1.1.1.1:1", fmt.Sprintf(join, b))
	if !s.MayRelay(a, b) || !s.MayRelay(b, a) {
		t.Fatal("the host and the joiner may not relay")
	}
}

// The game reaches the host it found before it joins the session.
func TestASearcherMayReachTheHostItFound(t *testing.T) {
	s := newTestServer()
	const host, searcher, other uint64 = 0x0009000000000A01, 0x0009000000000B02, 0x0009000000000C03
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 1 {
		t.Fatalf("%d results, want the host's session", len(results))
	}
	if !s.MayRelay(searcher, host) || !s.MayRelay(host, searcher) {
		t.Fatal("the searcher and the host it found may not relay")
	}
	if s.MayRelay(searcher, other) || s.MayRelay(other, host) {
		t.Fatal("a player who found nothing may relay")
	}
}

func TestPlayersAreFoundByMACOrXUID(t *testing.T) {
	s := newTestServer()
	registerAll(t, s, playerHomeA, playerHomeB)
	for _, query := range []string{`{"macAddress":"02aabbcc0c02"}`, `{"xuid":"0009000000000c02"}`} {
		r := do(t, s, "POST", "/players/find", "198.51.100.4:1", query)
		var found struct{ Gamertag string }
		decode(t, r, &found)
		if r.Code != http.StatusCreated || found.Gamertag != "Home B" {
			t.Fatalf("find %s: %d %q, want Home B", query, r.Code, found.Gamertag)
		}
	}
}

// The server checks a UPnP player's mapped probe port at the address it sees the player at,
// never at an address the player claims.
func TestARegistrationWithMappedPortsIsProbedFromOutside(t *testing.T) {
	var probed []netip.AddrPort
	s := New(Settings{RendezVousAddress: "10.0.0.5", ProbePlayer: func(at netip.AddrPort) bool {
		probed = append(probed, at)
		return true
	}})
	mapped := strings.Replace(playerHomeA, `"gamertag"`, `"mappedPorts":{"1000":2000,"9101":10101},"gamertag"`, 1)
	r := do(t, s, "POST", "/players", "198.51.100.4:50000", mapped)
	var body struct{ Reachable *bool }
	decode(t, r, &body)
	if len(probed) != 1 || probed[0] != netip.MustParseAddrPort("198.51.100.4:10101") {
		t.Fatalf("probed %v, want 198.51.100.4:10101", probed)
	}
	if body.Reachable == nil || !*body.Reachable {
		t.Fatalf("registration said reachable %v, want true", body.Reachable)
	}
}

func TestARegistrationWithoutMappedPortsIsNotProbed(t *testing.T) {
	called := false
	s := New(Settings{ProbePlayer: func(netip.AddrPort) bool { called = true; return true }})
	r := do(t, s, "POST", "/players", "198.51.100.4:50000", playerHomeA)
	if called || strings.Contains(r.Body.String(), "reachable") {
		t.Fatalf("a player without mapped ports was probed (%s)", r.Body.String())
	}
}
