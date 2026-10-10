// Package xws is the subset of the Xenia Web Services REST API that the SDK's online client
// (ported from the Xenia netplay fork) calls. State lives in memory.
package xws

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/netip"
	"strings"
	"sync"
)

// Settings configures the API.
type Settings struct {
	// RendezVousAddress is the IPv4 address the title server list gives (the LSP the game
	// logs in to).
	RendezVousAddress string
	// TrustedProxies are the reverse proxies whose X-Forwarded-For gives the client's
	// address; from any other peer the header is ignored.
	TrustedProxies []netip.Prefix
	// RelayAddress is the relay's "address:port" a registration gives (internal/relay).
	RelayAddress string
	// ProbePlayer checks a UPnP player's mapped probe port from outside (relay.ProbeDirect).
	ProbePlayer func(netip.AddrPort) bool
	// Logf, when set, logs requests.
	Logf func(format string, args ...any)
	// RequiredGameVersion, when set, is the game version a player must send to sign in
	// ("tu2": the 1.0.0 port, built on Title Update 2). Others get 426 and UpdateMessage.
	// Empty takes every client.
	RequiredGameVersion string
}

// UpdateMessage is what a client of another game version is told.
func UpdateMessage(required string) string {
	return fmt.Sprintf("This game server needs a newer version of WWE All Stars Recomp (game version %q). "+
		"Update the port (the launcher shows the new version) to play online.", required)
}

// Player is a registered player, as the client sends it.
type Player struct {
	XUID        string `json:"xuid"`
	BuildID     string `json:"buildId,omitempty"`
	// GameVersion is the game the port is built on ("tu2"); 0.9.0 (the base game) sends none.
	GameVersion string `json:"gameVersion,omitempty"`
	MachineID   string `json:"machineId"`
	HostAddress string `json:"hostAddress"` // public: the address whoami gave
	// LocalAddress is the address on its own network (XNADDR ina), for players behind
	// the same router.
	LocalAddress string `json:"localAddress,omitempty"`
	// MappedPorts are the router ports a UPnP player mapped, by game port.
	MappedPorts map[string]int  `json:"mappedPorts,omitempty"`
	Gamertag    string          `json:"gamertag"`
	MacAddress  string          `json:"macAddress"`
	Settings    json.RawMessage `json:"settings,omitempty"`
}

// relayGrant is a registration's relay token and the address the registration came from.
type relayGrant struct {
	token [16]byte
	from  netip.Addr
}

// Server serves the API.
type Server struct {
	settings Settings
	mux      *http.ServeMux

	mu       sync.Mutex
	players  map[string]Player     // by XUID
	tokens   map[string]relayGrant // by XUID
	sessions map[string]*session   // by title/session ID
}

// New creates the API.
func New(settings Settings) *Server {
	if settings.Logf == nil {
		settings.Logf = func(string, ...any) {}
	}
	s := &Server{settings: settings, mux: http.NewServeMux(), players: map[string]Player{},
		tokens: map[string]relayGrant{}, sessions: map[string]*session{}}
	s.mux.HandleFunc("GET /whoami", s.whoami)
	s.mux.HandleFunc("GET /version", s.version)
	s.mux.HandleFunc("POST /players", s.registerPlayer)
	s.mux.HandleFunc("POST /players/find", s.findPlayer)
	s.mux.HandleFunc("GET /title/{title}/servers", s.titleServers)
	s.mux.HandleFunc("POST /title/{title}/sessions", s.createSession)
	s.mux.HandleFunc("POST /title/{title}/sessions/search", s.searchSessions)
	s.mux.HandleFunc("GET /title/{title}/sessions/{id}", s.getSession)
	s.mux.HandleFunc("GET /title/{title}/sessions/{id}/details", s.getSession)
	s.mux.HandleFunc("POST /title/{title}/sessions/{id}/properties", s.setProperties)
	s.mux.HandleFunc("POST /title/{title}/sessions/{id}/modify", s.modifySession)
	s.mux.HandleFunc("DELETE /title/{title}/sessions/{id}", s.deleteSession)
	s.mux.HandleFunc("POST /title/{title}/sessions/{id}/join", s.joinSession)
	s.mux.HandleFunc("POST /title/{title}/sessions/{id}/leave", s.leaveSession)
	s.mux.HandleFunc("POST /title/{title}/sessions/{id}/qos", s.setQoS)
	s.mux.HandleFunc("GET /title/{title}/sessions/{id}/qos", s.getQoS)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mux.ServeHTTP(w, r)
}

// PlayerCount is the number of registered players.
func (s *Server) PlayerCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.players)
}

func (s *Server) trusted(addr netip.Addr) bool {
	for _, prefix := range s.settings.TrustedProxies {
		if prefix.Contains(addr.Unmap()) {
			return true
		}
	}
	return false
}

// clientIP is the peer's address or, when the peer is a trusted proxy, the rightmost
// X-Forwarded-For entry that is not one: entries to its left come from the client and
// can be forged.
func (s *Server) clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	peer, err := netip.ParseAddr(host)
	if err != nil || !s.trusted(peer) {
		return host
	}
	hops := strings.Split(strings.Join(r.Header.Values("X-Forwarded-For"), ","), ",")
	for i := len(hops) - 1; i >= 0; i-- {
		hop, err := netip.ParseAddr(strings.TrimSpace(hops[i]))
		if err != nil {
			return host
		}
		if !s.trusted(hop) {
			return hop.Unmap().String()
		}
	}
	return host
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

func (s *Server) whoami(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"address": s.clientIP(r)})
}

// version tells the launcher which game version the server takes (empty: any).
func (s *Server) version(w http.ResponseWriter, r *http.Request) {
	reply := map[string]string{"gameVersion": s.settings.RequiredGameVersion}
	if s.settings.RequiredGameVersion != "" {
		reply["message"] = UpdateMessage(s.settings.RequiredGameVersion)
	}
	writeJSON(w, http.StatusOK, reply)
}

func (s *Server) registerPlayer(w http.ResponseWriter, r *http.Request) {
	var player Player
	if err := json.NewDecoder(r.Body).Decode(&player); err != nil || player.XUID == "" {
		http.Error(w, "bad player", http.StatusBadRequest)
		return
	}
	if required := s.settings.RequiredGameVersion; required != "" && player.GameVersion != required {
		s.settings.Logf("player %s %q refused: game version %q, this server takes %q",
			strings.ToUpper(player.XUID), player.Gamertag, player.GameVersion, required)
		writeJSON(w, http.StatusUpgradeRequired, map[string]string{"error": "update required",
			"gameVersion": required, "message": UpdateMessage(required)})
		return
	}
	player.XUID = strings.ToUpper(player.XUID)
	var token [16]byte
	rand.Read(token[:])
	from, _ := netip.ParseAddr(s.clientIP(r))
	s.mu.Lock()
	s.players[player.XUID] = player
	s.tokens[player.XUID] = relayGrant{token: token, from: from.Unmap()}
	s.mu.Unlock()
	s.settings.Logf("player %s %q at %s, local %s (request from %s)", player.XUID,
		player.Gamertag, player.HostAddress, player.LocalAddress, s.clientIP(r))
	reply := map[string]any{"relayToken": hex.EncodeToString(token[:]),
		"relayAddress": s.settings.RelayAddress}
	if reachable, probed := s.probeMappedPort(r, player); probed {
		reply["reachable"] = reachable
	}
	writeJSON(w, http.StatusCreated, reply)
}

// probeKey is the SDK's probe port (relay::kProbePort) in mappedPorts.
const probeKey = "9101"

// probeMappedPort checks a UPnP player's mapped probe port at the address the request
// came from, never at an address the player claims.
func (s *Server) probeMappedPort(r *http.Request, player Player) (reachable, probed bool) {
	port, ok := player.MappedPorts[probeKey]
	at, err := netip.ParseAddr(s.clientIP(r))
	if !ok || err != nil || port <= 0 || port > 0xFFFF || s.settings.ProbePlayer == nil {
		return false, false
	}
	to := netip.AddrPortFrom(at.Unmap(), uint16(port))
	reachable = s.settings.ProbePlayer(to)
	s.settings.Logf("player %s: mapped probe port %v reachable %v", player.XUID, to, reachable)
	return reachable, true
}

func xuidKey(xuid uint64) string { return fmt.Sprintf("%016X", xuid) }

// RelayToken is the token a player's last registration gave and the address that
// registration came from (relay.Directory).
func (s *Server) RelayToken(xuid uint64) ([16]byte, netip.Addr, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	grant, ok := s.tokens[xuidKey(xuid)]
	return grant.token, grant.from, ok
}

// MayRelay says whether two players may reach each other through the relay: both joined
// one session, or one hosts a session the other found in a search (the game reaches the
// host before it joins). relay.Directory.
func (s *Server) MayRelay(a, b uint64) bool {
	ka, kb := xuidKey(a), xuidKey(b)
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, sess := range s.sessions {
		_, hasA := sess.members[ka]
		_, hasB := sess.members[kb]
		host := strings.ToUpper(sess.XUID)
		if (hasA && hasB) || (host == ka && sess.searchers[kb]) || (host == kb && sess.searchers[ka]) {
			return true
		}
	}
	return false
}

func (s *Server) findPlayer(w http.ResponseWriter, r *http.Request) {
	// One of: an address (public or local), a MAC (an XNADDR's abEnet) or a XUID.
	var query struct {
		HostAddress string `json:"hostAddress"`
		MacAddress  string `json:"macAddress"`
		XUID        string `json:"xuid"`
	}
	if err := json.NewDecoder(r.Body).Decode(&query); err != nil {
		http.Error(w, "bad query", http.StatusBadRequest)
		return
	}
	if query.MacAddress != "" || query.XUID != "" {
		s.mu.Lock()
		defer s.mu.Unlock()
		for _, player := range s.players {
			if (query.XUID != "" && strings.EqualFold(player.XUID, query.XUID)) ||
				(query.MacAddress != "" && strings.EqualFold(player.MacAddress, query.MacAddress)) {
				writeJSON(w, http.StatusCreated, player)
				return
			}
		}
		http.Error(w, "no such player", http.StatusNotFound)
		return
	}
	if player, ok := s.findByAddress(query.HostAddress, s.clientIP(r)); ok {
		writeJSON(w, http.StatusCreated, player)
		return
	}
	http.Error(w, "no player at "+query.HostAddress, http.StatusNotFound)
}

// findByAddress finds the player at a public address or, behind the asker's own public
// address, at a local one (every home network reuses the same local addresses). Several
// players behind one public address are told apart only by their local addresses; asked
// for that public address, the lowest XUID answers.
func (s *Server) findByAddress(address, askerPublic string) (Player, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	var found Player
	ok := false
	for _, player := range s.players {
		if player.LocalAddress == address && player.HostAddress == askerPublic {
			return player, true
		}
		if player.HostAddress == address && (!ok || player.XUID < found.XUID) {
			found, ok = player, true
		}
	}
	return found, ok
}

type titleServer struct {
	Address     string `json:"address"`
	Flags       int    `json:"flags"`
	Description string `json:"description"`
}

func (s *Server) titleServers(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, []titleServer{{Address: s.settings.RendezVousAddress,
		Description: "WWE All Stars Rendez-Vous"}})
}

// Listen serves the API over plain HTTP.
func (s *Server) Listen(address string) error {
	log.Printf("XWS API on http://%s/", address)
	return http.ListenAndServe(address, s)
}
