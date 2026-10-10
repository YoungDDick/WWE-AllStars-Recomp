package rendezvous

import (
	"sync"

	"github.com/PretendoNetwork/nex-go/v2/types"
)

// Station is a registered client: its public URL and connection ID.
type Station struct {
	URL          string
	ConnectionID uint32
}

// Stations holds the secure server's registered clients by PID, in memory.
type Stations struct {
	mu    sync.Mutex
	byPID map[types.PID]Station
}

// NewStations creates an empty table.
func NewStations() *Stations { return &Stations{byPID: map[types.PID]Station{}} }

// Register records (or replaces) a client's station.
func (s *Stations) Register(pid types.PID, connectionID uint32, url string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.byPID[pid] = Station{URL: url, ConnectionID: connectionID}
}

// Lookup returns a client's station.
func (s *Stations) Lookup(pid types.PID) (Station, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	station, ok := s.byPID[pid]
	return station, ok
}

// Forget drops a client's station when its connection ends.
func (s *Stations) Forget(pid types.PID) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.byPID, pid)
}
