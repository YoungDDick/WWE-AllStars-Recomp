package xws

import (
	"encoding/json"
	"io"
	"net/http"
	"sort"
	"strings"
	"time"
)

// Xbox LIVE context IDs every session search matches on.
const (
	contextGameType = 0x800A // X_CONTEXT_GAME_TYPE: standard or ranked
	contextGameMode = 0x800B // X_CONTEXT_GAME_MODE
)

// SessionIdleLimit is how long a session lives without a call about it. A host that crashes
// or quits a failed match never deletes its session (issue #2); a healthy one lived at most
// 21 minutes on the VPS, with a call at each step.
const SessionIdleLimit = 30 * time.Minute

// Context is an Xbox LIVE context (a user's game type, game mode, ...).
type Context struct {
	ID    uint32 `json:"id"`
	Value uint32 `json:"value"`
}

// Property is an Xbox LIVE property; one of Int, Double and Bytes (base64) carries the
// value, by its type.
type Property struct {
	ID     uint32   `json:"id"`
	Type   int      `json:"type"`
	Int    *int64   `json:"int,omitempty"`
	Double *float64 `json:"double,omitempty"`
	Bytes  string   `json:"bytes,omitempty"`
}

// Session is a hosted session, as the host creates it.
type Session struct {
	SessionID         string `json:"sessionId"`
	XUID              string `json:"xuid"`
	Flags             uint32 `json:"flags"`
	PublicSlotsCount  int    `json:"publicSlotsCount"`
	PrivateSlotsCount int    `json:"privateSlotsCount"`
	UserIndex         int    `json:"userIndex"`
	HostAddress       string `json:"hostAddress"`
	LocalAddress      string `json:"localAddress,omitempty"`
	MacAddress        string `json:"macAddress"`
	Port              int    `json:"port"`
	BuildID           string `json:"buildId"`
}

// sessionView is a session as the API returns it: with its slot counts and, in search
// results, its contexts and properties.
type sessionView struct {
	Session
	OpenPublicSlotsCount    int        `json:"openPublicSlotsCount"`
	OpenPrivateSlotsCount   int        `json:"openPrivateSlotsCount"`
	FilledPublicSlotsCount  int        `json:"filledPublicSlotsCount"`
	FilledPrivateSlotsCount int        `json:"filledPrivateSlotsCount"`
	Players                 []string   `json:"players"`
	Contexts                []Context  `json:"contexts,omitempty"`
	Properties              []Property `json:"properties,omitempty"`
}

type session struct {
	Session
	title      string
	created    time.Time
	active     time.Time       // the last call about the session
	members    map[string]bool // XUID -> private slot
	searchers  map[string]bool // XUIDs a search gave this session to
	contexts   []Context
	properties []Property
	qos        []byte
}

func (s *session) filled(private bool) int {
	n := 0
	for _, p := range s.members {
		if p == private {
			n++
		}
	}
	return n
}

func (s *session) view(withProperties bool) sessionView {
	v := sessionView{Session: s.Session}
	v.FilledPublicSlotsCount = s.filled(false)
	v.FilledPrivateSlotsCount = s.filled(true)
	v.OpenPublicSlotsCount = max(0, s.PublicSlotsCount-v.FilledPublicSlotsCount)
	v.OpenPrivateSlotsCount = max(0, s.PrivateSlotsCount-v.FilledPrivateSlotsCount)
	v.Players = []string{}
	for xuid := range s.members {
		v.Players = append(v.Players, xuid)
	}
	sort.Strings(v.Players)
	if withProperties {
		v.Contexts = s.contexts
		v.Properties = s.properties
	}
	return v
}

func (s *session) context(id uint32) (uint32, bool) {
	for _, c := range s.contexts {
		if c.ID == id {
			return c.Value, true
		}
	}
	return 0, false
}

func sessionKey(title, id string) string {
	return strings.ToUpper(title) + "/" + strings.ToLower(id)
}

// lookup finds a live session; s.mu is held.
func (s *Server) lookup(r *http.Request) (*session, bool) {
	s.expireIdle()
	sess, ok := s.sessions[sessionKey(r.PathValue("title"), r.PathValue("id"))]
	return sess, ok
}

// touch finds a live session and marks it active: a call from its host or a member, not a
// searcher's look at it; s.mu is held.
func (s *Server) touch(r *http.Request) (*session, bool) {
	sess, ok := s.lookup(r)
	if ok {
		sess.active = s.settings.Now()
	}
	return sess, ok
}

// expireIdle drops the sessions nobody called about for SessionIdleLimit; s.mu is held.
func (s *Server) expireIdle() {
	now := s.settings.Now()
	for key, sess := range s.sessions {
		if idle := now.Sub(sess.active); idle >= SessionIdleLimit {
			delete(s.sessions, key)
			s.settings.Logf("session %s expired (idle %v)", sess.SessionID, idle.Round(time.Second))
		}
	}
}

// dropHostedBy drops the sessions a host left behind when it signs in again; s.mu is held.
func (s *Server) dropHostedBy(xuid string) {
	for key, sess := range s.sessions {
		if strings.EqualFold(sess.XUID, xuid) {
			delete(s.sessions, key)
			s.settings.Logf("session %s dropped: its host %s signed in again", sess.SessionID, xuid)
		}
	}
}

func (s *Server) createSession(w http.ResponseWriter, r *http.Request) {
	var created Session
	if err := json.NewDecoder(r.Body).Decode(&created); err != nil || created.SessionID == "" {
		http.Error(w, "bad session", http.StatusBadRequest)
		return
	}
	created.SessionID = strings.ToLower(created.SessionID)
	created.XUID = strings.ToUpper(created.XUID)
	title := r.PathValue("title")
	now := s.settings.Now()
	s.mu.Lock()
	s.sessions[sessionKey(title, created.SessionID)] = &session{Session: created, title: title,
		created: now, active: now, members: map[string]bool{}, searchers: map[string]bool{}}
	s.mu.Unlock()
	s.settings.Logf("session %s created by %s at %s (flags %#x, %d public, %d private, build %.12s)",
		created.SessionID, created.XUID, created.HostAddress, created.Flags,
		created.PublicSlotsCount, created.PrivateSlotsCount, created.BuildID)
	writeJSON(w, http.StatusCreated, map[string]string{})
}

func (s *Server) setProperties(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Contexts   []Context  `json:"contexts"`
		Properties []Property `json:"properties"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad properties", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.touch(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	sess.contexts = body.Contexts
	sess.properties = body.Properties
	s.settings.Logf("session %s: %d contexts, %d properties", sess.SessionID, len(body.Contexts),
		len(body.Properties))
	writeJSON(w, http.StatusCreated, map[string]string{})
}

func (s *Server) getSession(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.lookup(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	writeJSON(w, http.StatusOK, sess.view(true))
}

func (s *Server) modifySession(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Flags             uint32 `json:"flags"`
		PublicSlotsCount  int    `json:"publicSlotsCount"`
		PrivateSlotsCount int    `json:"privateSlotsCount"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad modify", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.touch(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	sess.Flags = body.Flags
	sess.PublicSlotsCount = body.PublicSlotsCount
	sess.PrivateSlotsCount = body.PrivateSlotsCount
	s.settings.Logf("session %s modified: flags %#x, %d public, %d private", sess.SessionID,
		body.Flags, body.PublicSlotsCount, body.PrivateSlotsCount)
	writeJSON(w, http.StatusCreated, map[string]string{})
}

func (s *Server) deleteSession(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expireIdle()
	key := sessionKey(r.PathValue("title"), r.PathValue("id"))
	if _, ok := s.sessions[key]; !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	delete(s.sessions, key)
	s.settings.Logf("session %s deleted", strings.ToLower(r.PathValue("id")))
	writeJSON(w, http.StatusOK, map[string]string{})
}

// searchSessions applies XWS's rules: the title and build match, the session is
// advertised (it has contexts and properties), it has room for every searching user, it
// isn't the searcher's own, and its game mode and type match the searcher's.
func (s *Server) searchSessions(w http.ResponseWriter, r *http.Request) {
	var query struct {
		SearchIndex  int       `json:"searchIndex"`
		ResultsCount int       `json:"resultsCount"`
		NumUsers     int       `json:"numUsers"`
		SearcherXUID string    `json:"searcher_xuid"`
		BuildID      string    `json:"buildId"`
		Contexts     []Context `json:"contexts"`
	}
	if err := json.NewDecoder(r.Body).Decode(&query); err != nil {
		http.Error(w, "bad search", http.StatusBadRequest)
		return
	}
	title := strings.ToUpper(r.PathValue("title"))
	wanted := map[uint32]uint32{}
	for _, c := range query.Contexts {
		if c.ID == contextGameMode || c.ID == contextGameType {
			wanted[c.ID] = c.Value
		}
	}
	numUsers := max(1, query.NumUsers)

	s.mu.Lock()
	s.expireIdle()
	var found []*session
	for _, sess := range s.sessions {
		if strings.ToUpper(sess.title) != title || sess.BuildID != query.BuildID ||
			strings.EqualFold(sess.XUID, query.SearcherXUID) ||
			len(sess.contexts) == 0 || len(sess.properties) == 0 ||
			sess.PublicSlotsCount-sess.filled(false) < numUsers {
			continue
		}
		matches := true
		for id, value := range wanted {
			if have, ok := sess.context(id); !ok || have != value {
				matches = false
			}
		}
		if matches {
			found = append(found, sess)
		}
	}
	sort.Slice(found, func(i, j int) bool { return found[i].created.Before(found[j].created) })
	if query.ResultsCount > 0 && len(found) > query.ResultsCount {
		found = found[:query.ResultsCount]
	}
	results := make([]sessionView, 0, len(found))
	for _, sess := range found {
		results = append(results, sess.view(true))
		sess.searchers[strings.ToUpper(query.SearcherXUID)] = true
	}
	s.mu.Unlock()
	s.settings.Logf("search by %s (query %d, build %.12s): %d sessions", query.SearcherXUID,
		query.SearchIndex, query.BuildID, len(results))
	writeJSON(w, http.StatusCreated, results)
}

func (s *Server) joinSession(w http.ResponseWriter, r *http.Request) {
	var body struct {
		XUIDs        []string `json:"xuids"`
		PrivateSlots []bool   `json:"privateSlots"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad join", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.touch(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	for i, xuid := range body.XUIDs {
		private := i < len(body.PrivateSlots) && body.PrivateSlots[i]
		sess.members[strings.ToUpper(xuid)] = private
		s.settings.Logf("session %s: %s joined", sess.SessionID, strings.ToUpper(xuid))
	}
	writeJSON(w, http.StatusCreated, map[string]string{})
}

func (s *Server) leaveSession(w http.ResponseWriter, r *http.Request) {
	var body struct {
		XUIDs []string `json:"xuids"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		http.Error(w, "bad leave", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.touch(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	for _, xuid := range body.XUIDs {
		delete(sess.members, strings.ToUpper(xuid))
		s.settings.Logf("session %s: %s left", sess.SessionID, strings.ToUpper(xuid))
	}
	writeJSON(w, http.StatusCreated, map[string]string{})
}

// QoS data is the host's XNetQosListen blob, handed to searchers' XNetQosLookup as is.
func (s *Server) setQoS(w http.ResponseWriter, r *http.Request) {
	data, err := io.ReadAll(io.LimitReader(r.Body, 4096))
	if err != nil {
		http.Error(w, "bad QoS data", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.touch(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	sess.qos = data
	writeJSON(w, http.StatusCreated, map[string]string{})
}

func (s *Server) getQoS(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.lookup(r)
	if !ok {
		http.Error(w, "no such session", http.StatusNotFound)
		return
	}
	if len(sess.qos) == 0 {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Write(sess.qos)
}
