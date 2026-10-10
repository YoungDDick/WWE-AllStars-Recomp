package rendezvous

import (
	"crypto/rand"
	"encoding/hex"
	"sync"

	nex "github.com/PretendoNetwork/nex-go/v2"
	"github.com/PretendoNetwork/nex-go/v2/types"
)

const (
	// SecureServerPID is the secure server's principal; the game's own fallback URL uses 2.
	SecureServerPID = 2
	// SecureServerName is the secure server's account name (as on Quazal's servers).
	SecureServerName = "Quazal Rendez-Vous"
	// UserPassword is every player's password: JobLSPLogin logs in with "LSP PASSWORD"
	// (0x837F1FA4, stored as the job's password at +0x5C by 0x833AF910).
	UserPassword = "LSP PASSWORD"
	firstUserPID = 0x1000
)

// Accounts accepts every username, so AccountManagement (0x19) is never needed. Every
// Login gets a new PID (two copies of the game may share a gamertag), kept in memory.
type Accounts struct {
	mu     sync.Mutex
	names  map[types.PID]string
	next   types.PID
	secure *nex.Account
}

// NewAccounts creates the account table with a secure server account whose password is
// random for this process.
func NewAccounts() *Accounts {
	secret := make([]byte, 16)
	rand.Read(secret)
	return &Accounts{
		names:  map[types.PID]string{},
		next:   firstUserPID,
		secure: nex.NewAccount(SecureServerPID, SecureServerName, hex.EncodeToString(secret), false),
	}
}

// NewPID gives a Login its PID.
func (a *Accounts) NewPID(username string) types.PID {
	a.mu.Lock()
	defer a.mu.Unlock()
	pid := a.next
	a.next++
	a.names[pid] = username
	return pid
}

// Name returns the username a PID logged in with.
func (a *Accounts) Name(pid types.PID) (string, bool) {
	a.mu.Lock()
	defer a.mu.Unlock()
	name, ok := a.names[pid]
	return name, ok
}

// SecureServer is the secure server's account.
func (a *Accounts) SecureServer() *nex.Account { return a.secure }

// ByUsername is nex-go's account lookup; the secure endpoint only looks up itself.
func (a *Accounts) ByUsername(username string) (*nex.Account, *nex.Error) {
	if username == SecureServerName {
		return a.secure, nil
	}
	return nil, nex.NewError(nex.ResultCodes.RendezVous.InvalidUsername, "unknown account")
}
