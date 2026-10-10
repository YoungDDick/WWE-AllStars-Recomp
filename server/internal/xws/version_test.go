package xws

import (
	"net/http"
	"strings"
	"testing"
)

// A 1.0.0 client (Title Update 2) adds its game version to the players body; 0.9.0 sends none.
var playerTU2 = strings.Replace(playerA, `"settings":{}`, `"gameVersion":"tu2","settings":{}`, 1)

func newVersionedServer() *Server {
	return New(Settings{RendezVousAddress: "10.0.0.5", RequiredGameVersion: "tu2"})
}

func TestAClientWithTheRequiredGameVersionSignsIn(t *testing.T) {
	if r := do(t, newVersionedServer(), "POST", "/players", "127.0.0.9:1", playerTU2); r.Code != http.StatusCreated {
		t.Fatalf("status %d %s, want 201", r.Code, r.Body.String())
	}
}

func TestAnOlderGameVersionIsRefusedWithThePleaseUpdateMessage(t *testing.T) {
	older := strings.Replace(playerTU2, `"gameVersion":"tu2"`, `"gameVersion":"tu1"`, 1)
	r := do(t, newVersionedServer(), "POST", "/players", "127.0.0.9:1", older)
	if r.Code != http.StatusUpgradeRequired {
		t.Fatalf("status %d, want 426", r.Code)
	}
	var body struct{ Error, Message, GameVersion string }
	decode(t, r, &body)
	if body.Error != "update required" || body.GameVersion != "tu2" || !strings.Contains(strings.ToLower(body.Message), "update the port") {
		t.Fatalf("body %+v, want the update message", body)
	}
}

func TestAClientThatSendsNoGameVersionIsRefused(t *testing.T) {
	// 0.9.0 (base game) registers without a game version.
	s := newVersionedServer()
	if r := do(t, s, "POST", "/players", "127.0.0.9:1", playerA); r.Code != http.StatusUpgradeRequired {
		t.Fatalf("status %d, want 426", r.Code)
	}
	if s.PlayerCount() != 0 {
		t.Fatalf("%d players registered, want 0", s.PlayerCount())
	}
}

func TestWithoutARequiredVersionEveryClientSignsIn(t *testing.T) {
	// Until 1.0.0 ships, the server takes 0.9.0 and 1.0.0 alike (their builds never share a session).
	s := newTestServer()
	for _, p := range []string{playerA, playerTU2} {
		if r := do(t, s, "POST", "/players", "127.0.0.9:1", p); r.Code != http.StatusCreated {
			t.Fatalf("status %d, want 201", r.Code)
		}
	}
}

func TestVersionTellsTheLauncherWhatTheServerRequires(t *testing.T) {
	r := do(t, newVersionedServer(), "GET", "/version", "127.0.0.9:1", "")
	if r.Code != http.StatusOK {
		t.Fatalf("status %d", r.Code)
	}
	var body struct {
		GameVersion string `json:"gameVersion"`
		Message     string `json:"message"`
	}
	decode(t, r, &body)
	if body.GameVersion != "tu2" || body.Message == "" {
		t.Fatalf("body %+v", body)
	}
}
