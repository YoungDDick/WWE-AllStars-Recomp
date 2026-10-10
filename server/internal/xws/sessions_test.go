package xws

import (
	"fmt"
	"net/http"
	"strings"
	"testing"
)

const (
	buildA   = "a1b2c3"
	buildB   = "ffffff"
	gameType = 0x800A
	gameMode = 0x800B
)

func createSession(t *testing.T, s *Server, id, xuid, host, build string, public int) {
	t.Helper()
	body := fmt.Sprintf(`{"sessionId":%q,"xuid":%q,"flags":45,"publicSlotsCount":%d,`+
		`"privateSlotsCount":0,"userIndex":0,"hostAddress":%q,"macAddress":"02aabbcc0a01",`+
		`"port":36000,"buildId":%q}`, id, xuid, public, host, build)
	if r := do(t, s, "POST", "/title/54510866/sessions", host+":1", body); r.Code != http.StatusCreated {
		t.Fatalf("create: status %d %s", r.Code, r.Body.String())
	}
}

func setProperties(t *testing.T, s *Server, id string, mode, typ int) {
	t.Helper()
	body := fmt.Sprintf(`{"contexts":[{"id":%d,"value":%d},{"id":%d,"value":%d}],`+
		`"properties":[{"id":268468225,"type":1,"int":2},{"id":1073774593,"type":6,"bytes":"AQID"}]}`,
		gameMode, mode, gameType, typ)
	path := "/title/54510866/sessions/" + id + "/properties"
	if r := do(t, s, "POST", path, "1.1.1.1:1", body); r.Code != http.StatusCreated {
		t.Fatalf("properties: status %d %s", r.Code, r.Body.String())
	}
}

type searchResult struct {
	SessionID            string `json:"sessionId"`
	HostAddress          string `json:"hostAddress"`
	MacAddress           string `json:"macAddress"`
	OpenPublicSlotsCount int    `json:"openPublicSlotsCount"`
	Contexts             []struct{ ID, Value uint32 }
	Properties           []struct {
		ID    uint32
		Type  int
		Int   *int64
		Bytes string
	}
}

func search(t *testing.T, s *Server, searcher, build string, mode, typ int) []searchResult {
	t.Helper()
	body := fmt.Sprintf(`{"searchIndex":0,"resultsCount":10,"numUsers":1,"searcher_xuid":%q,`+
		`"buildId":%q,"contexts":[{"id":%d,"value":%d},{"id":%d,"value":%d}]}`,
		searcher, build, gameMode, mode, gameType, typ)
	r := do(t, s, "POST", "/title/54510866/sessions/search", "1.1.1.1:1", body)
	if r.Code != http.StatusCreated {
		t.Fatalf("search: status %d %s", r.Code, r.Body.String())
	}
	var results []searchResult
	decode(t, r, &results)
	return results
}

func TestASearchFindsAnAdvertisedSessionWithItsProperties(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)

	results := search(t, s, "0009000000000B02", buildA, 1, 0)
	if len(results) != 1 {
		t.Fatalf("%d results, want 1", len(results))
	}
	got := results[0]
	if got.SessionID != "80000000000000a1" || got.HostAddress != "127.0.0.2" ||
		got.MacAddress != "02aabbcc0a01" || got.OpenPublicSlotsCount != 2 {
		t.Fatalf("result %+v", got)
	}
	if len(got.Contexts) != 2 || len(got.Properties) != 2 {
		t.Fatalf("%d contexts, %d properties, want 2 and 2", len(got.Contexts), len(got.Properties))
	}
	if got.Properties[0].Int == nil || *got.Properties[0].Int != 2 || got.Properties[1].Bytes != "AQID" {
		t.Fatalf("properties %+v", got.Properties)
	}
}

func TestASearchSkipsOtherBuilds(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	if results := search(t, s, "0009000000000B02", buildB, 1, 0); len(results) != 0 {
		t.Fatalf("a different build found %d sessions", len(results))
	}
}

func TestASearchSkipsTheSearchersOwnSession(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	if results := search(t, s, "0009000000000A01", buildA, 1, 0); len(results) != 0 {
		t.Fatalf("the host found its own session")
	}
}

func TestASearchNeedsMatchingModeAndType(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	if results := search(t, s, "0009000000000B02", buildA, 2, 0); len(results) != 0 {
		t.Fatal("another game mode matched")
	}
	if results := search(t, s, "0009000000000B02", buildA, 1, 1); len(results) != 0 {
		t.Fatal("another game type (ranked) matched")
	}
}

func TestASearchSkipsSessionsWithoutPropertiesOrOpenSlots(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatal("a session without contexts or properties was advertised")
	}
	createSession(t, s, "80000000000000a2", "0009000000000A02", "127.0.0.4", buildA, 0)
	setProperties(t, s, "80000000000000a2", 1, 0)
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatal("a full session was found")
	}
}

func TestModifyAndDelete(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	path := "/title/54510866/sessions/80000000000000a1"
	if r := do(t, s, "POST", path+"/modify", "1.1.1.1:1",
		`{"flags":45,"publicSlotsCount":0,"privateSlotsCount":0}`); r.Code != http.StatusCreated {
		t.Fatalf("modify: status %d", r.Code)
	}
	if results := search(t, s, "0009000000000B02", buildA, 1, 0); len(results) != 0 {
		t.Fatal("a session modified to no slots is still found")
	}
	r := do(t, s, "GET", path, "1.1.1.1:1", "")
	if r.Code != http.StatusOK || !strings.Contains(r.Body.String(), `"publicSlotsCount":0`) {
		t.Fatalf("get after modify: %d %s", r.Code, r.Body.String())
	}
	if r := do(t, s, "DELETE", path, "1.1.1.1:1", ""); r.Code != http.StatusOK {
		t.Fatalf("delete: status %d", r.Code)
	}
	if r := do(t, s, "GET", path, "1.1.1.1:1", ""); r.Code != http.StatusNotFound {
		t.Fatalf("get after delete: status %d", r.Code)
	}
}

func TestDetailsGivesTheSession(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	r := do(t, s, "GET", "/title/54510866/sessions/80000000000000a1/details", "1.1.1.1:1", "")
	if r.Code != http.StatusOK || !strings.Contains(r.Body.String(), `"hostAddress":"127.0.0.2"`) {
		t.Fatalf("details: %d %s", r.Code, r.Body.String())
	}
}

func TestJoinAndLeaveChangeTheOpenSlots(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	setProperties(t, s, "80000000000000a1", 1, 0)
	path := "/title/54510866/sessions/80000000000000a1"
	if r := do(t, s, "POST", path+"/join", "1.1.1.1:1",
		`{"xuids":["0009000000000A01"],"privateSlots":[false]}`); r.Code != http.StatusCreated {
		t.Fatalf("join: status %d", r.Code)
	}
	results := search(t, s, "0009000000000B02", buildA, 1, 0)
	if len(results) != 1 || results[0].OpenPublicSlotsCount != 1 {
		t.Fatalf("after the host joined: %+v", results)
	}
	do(t, s, "POST", path+"/join", "1.1.1.1:1", `{"xuids":["0009000000000B02"],"privateSlots":[false]}`)
	if results := search(t, s, "0009000000000C03", buildA, 1, 0); len(results) != 0 {
		t.Fatal("a full session was found")
	}
	if r := do(t, s, "POST", path+"/leave", "1.1.1.1:1", `{"xuids":["0009000000000B02"]}`); r.Code != http.StatusCreated {
		t.Fatalf("leave: status %d", r.Code)
	}
	if results := search(t, s, "0009000000000C03", buildA, 1, 0); len(results) != 1 {
		t.Fatal("the slot did not open again")
	}
}

func TestQoSDataIsStoredForSearchers(t *testing.T) {
	s := newTestServer()
	createSession(t, s, "80000000000000a1", "0009000000000A01", "127.0.0.2", buildA, 2)
	path := "/title/54510866/sessions/80000000000000a1/qos"
	if r := do(t, s, "GET", path, "1.1.1.1:1", ""); r.Code != http.StatusNoContent {
		t.Fatalf("QoS before any data: status %d", r.Code)
	}
	if r := do(t, s, "POST", path, "1.1.1.1:1", "\x01\x02\x00\xff"); r.Code != http.StatusCreated {
		t.Fatalf("QoS post: status %d", r.Code)
	}
	r := do(t, s, "GET", path, "1.1.1.1:1", "")
	if r.Code != http.StatusOK || r.Body.String() != "\x01\x02\x00\xff" {
		t.Fatalf("QoS get: %d %q", r.Code, r.Body.String())
	}
}

func TestASessionKeepsTheHostsLocalAddress(t *testing.T) {
	s := newTestServer()
	body := `{"sessionId":"80000000000000a1","xuid":"0009000000000A01","flags":1,` +
		`"publicSlotsCount":2,"hostAddress":"203.0.113.7","localAddress":"192.168.1.10"}`
	if r := do(t, s, "POST", "/title/54510866/sessions", "1.1.1.1:1", body); r.Code != http.StatusCreated {
		t.Fatalf("create: status %d", r.Code)
	}
	r := do(t, s, "GET", "/title/54510866/sessions/80000000000000a1", "1.1.1.1:1", "")
	if !strings.Contains(r.Body.String(), `"localAddress":"192.168.1.10"`) {
		t.Fatalf("session %s lacks the local address", r.Body.String())
	}
}
