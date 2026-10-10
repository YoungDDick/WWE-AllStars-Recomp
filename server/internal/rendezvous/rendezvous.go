// Package rendezvous is the Rendez-Vous stand-in: the Quazal PRUDP v0 servers the game logs
// in to (docs/research/online-mode-feasibility.md §6.2), built on nex-go's Quazal mode.
package rendezvous

import (
	"log"

	nex "github.com/PretendoNetwork/nex-go/v2"
	"github.com/PretendoNetwork/nex-go/v2/encryption"
)

// StreamID is the virtual port the game's RV streams use (src 3:15 -> dst 3:1).
const StreamID = 1

// Call is a decoded RMC request.
type Call struct {
	Protocol   uint16
	Method     uint32
	CallID     uint32
	Parameters []byte
}

// Settings configures a server.
type Settings struct {
	// AccessKey is the title's [Quazal] AccessKey (PRUDP checksums).
	AccessKey string
	// OnCall, when set, sees every RMC request.
	OnCall func(Call)
	// Accounts is shared by the authentication and secure servers (a new table if nil).
	Accounts *Accounts
	// Stations holds the secure server's registered clients (a new table if nil).
	Stations *Stations
	// SecureHost and SecurePort locate the secure server in the Login reply.
	SecureHost string
	SecurePort int
	// SessionKeyLength is the Kerberos session key size (16 when zero).
	SessionKeyLength int
}

// Server is one PRUDP v0 server on one UDP port.
type Server struct {
	prudp    *nex.PRUDPServer
	settings Settings
	handlers map[uint16]map[uint32]func(nex.PRUDPPacketInterface, *nex.RMCMessage) error
}

// Listen serves on the UDP port until the process exits.
func (s *Server) Listen(port int) {
	s.prudp.ListenUDP(port)
}

func (s *Server) handle(protocol uint16, method uint32,
	handler func(nex.PRUDPPacketInterface, *nex.RMCMessage) error) {
	if s.handlers[protocol] == nil {
		s.handlers[protocol] = map[uint32]func(nex.PRUDPPacketInterface, *nex.RMCMessage) error{}
	}
	s.handlers[protocol][method] = handler
}

// newServer builds a Quazal-mode PRUDP v0 server with one endpoint for the RV stream.
func newServer(settings Settings, secure bool) (*Server, *nex.PRUDPEndPoint) {
	if settings.Accounts == nil {
		settings.Accounts = NewAccounts()
	}
	if settings.Stations == nil {
		settings.Stations = NewStations()
	}
	if settings.SessionKeyLength == 0 {
		settings.SessionKeyLength = 16
	}
	if settings.SecureHost == "" {
		settings.SecureHost = "127.0.0.1"
	}
	prudp := nex.NewPRUDPServer()
	prudp.AccessKey = settings.AccessKey
	prudp.SessionKeyLength = settings.SessionKeyLength
	prudp.PRUDPV0Settings.IsQuazalMode = true
	// The game signs every packet with the other side's connection signature.
	prudp.PRUDPV0Settings.LegacyConnectionSignature = true
	// The secure CONNECT payload (ticket and request) is RC4-encrypted like DATA.
	prudp.PRUDPV0Settings.EncryptedConnect = true
	// Old Quazal: payloads up to 962 bytes per fragment (an MTU of 1000).
	prudp.SetFragmentSize(962)
	server := &Server{prudp: prudp, settings: settings,
		handlers: map[uint16]map[uint32]func(nex.PRUDPPacketInterface, *nex.RMCMessage) error{}}

	endpoint := nex.NewPRUDPEndPoint(StreamID)
	endpoint.IsSecureEndPoint = secure
	endpoint.ServerAccount = settings.Accounts.SecureServer()
	endpoint.AccountDetailsByUsername = settings.Accounts.ByUsername
	endpoint.DefaultStreamSettings.EncryptionAlgorithm = encryption.NewQuazalRC4Encryption()
	// A compression-ratio byte leads every DATA payload (0 = not compressed).
	endpoint.DefaultStreamSettings.CompressionAlgorithm = newRatioByte()
	endpoint.OnData(func(packet nex.PacketInterface) {
		message := packet.RMCMessage()
		if !message.IsRequest {
			return
		}
		if settings.OnCall != nil {
			settings.OnCall(Call{Protocol: message.ProtocolID, Method: message.MethodID,
				CallID: message.CallID, Parameters: message.Parameters})
		}
		handler := server.handlers[message.ProtocolID][message.MethodID]
		if handler == nil {
			return
		}
		if err := handler(packet.(nex.PRUDPPacketInterface), message); err != nil {
			log.Printf("protocol %#02x method %d: %v", message.ProtocolID, message.MethodID, err)
		}
	})
	prudp.BindPRUDPEndPoint(endpoint)
	return server, endpoint
}

// NewAuthServer is the authentication server (the [Quazal] ServerPort).
func NewAuthServer(settings Settings) *Server {
	server, _ := newServer(settings, false)
	server.handle(protocolAuthentication, methodLogin, server.handleLogin)
	server.handle(protocolAuthentication, methodRequestTicket, server.handleRequestTicket)
	return server
}

// NewSecureServer is the secure server the Login ticket is for (the auth port + 1).
func NewSecureServer(settings Settings) *Server {
	server, endpoint := newServer(settings, true)
	endpoint.OnError(func(err *nex.Error) { log.Printf("secure server: %v", err) })
	endpoint.OnConnectionEnded(func(connection *nex.PRUDPConnection) {
		server.settings.Stations.Forget(connection.PID())
	})
	server.handle(protocolSecureConnection, methodRegister, server.handleRegister)
	server.handle(protocolSecureConnection, methodRegisterEx, server.handleRegister)
	server.handle(protocolSecureConnection, methodRequestConnectionData,
		server.handleRequestConnectionData)
	for method := uint32(firstGameDataLogMethod); method <= lastGameDataLogMethod; method++ {
		server.handle(protocolGameDataLog, method, server.handleGameDataLog)
	}
	return server
}
