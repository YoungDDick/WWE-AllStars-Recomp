package rendezvous

import (
	"crypto/rand"
	"fmt"

	nex "github.com/PretendoNetwork/nex-go/v2"
	"github.com/PretendoNetwork/nex-go/v2/constants"
	"github.com/PretendoNetwork/nex-go/v2/types"
)

// Quazal protocol and method IDs the stand-in answers (feasibility §6.2).
const (
	protocolAuthentication = 0x0A
	methodLogin            = 1
	methodRequestTicket    = 3
)

// secureURL is the station URL of the secure server, in the order the game builds its own
// fallback (0x830E35B8).
func secureURL(host string, port int) string {
	return fmt.Sprintf("prudps:/address=%s;port=%d;stream=3;sid=1;PID=%d;CID=1", host, port,
		SecureServerPID)
}

// handleLogin answers Authentication.Login(strUserName) with success, the user's PID, a
// Kerberos ticket for the secure server and the secure server's URL.
func (s *Server) handleLogin(packet nex.PRUDPPacketInterface, request *nex.RMCMessage) error {
	endpoint := packet.Sender().(*nex.PRUDPConnection).Endpoint().(*nex.PRUDPEndPoint)
	parameters := nex.NewByteStreamIn(request.Parameters, endpoint.LibraryVersions(),
		endpoint.ByteStreamSettings())
	var username types.String
	if err := username.ExtractFrom(parameters); err != nil {
		return fmt.Errorf("Login parameters: %w", err)
	}
	pid := s.settings.Accounts.NewPID(string(username))
	ticket, err := s.ticket(endpoint, pid, SecureServerPID)
	if err != nil {
		return err
	}

	out := nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings())
	types.NewQResultSuccess(nex.ResultCodes.Core.Unknown).WriteTo(out)
	pid.WriteTo(out)
	types.NewBuffer(ticket).WriteTo(out)
	// RVConnectionData (no structure header in this Quazal version): the secure URL, no
	// special protocols, an empty special URL.
	types.NewString(secureURL(s.settings.SecureHost, s.settings.SecurePort)).WriteTo(out)
	types.NewList[types.UInt8]().WriteTo(out)
	types.NewString("").WriteTo(out)
	types.NewString("").WriteTo(out) // strReturnMsg

	response := nex.NewRMCSuccess(endpoint, out.Bytes())
	response.ProtocolID = protocolAuthentication
	response.MethodID = methodLogin
	response.CallID = request.CallID
	respond(packet, response)
	return nil
}

// handleRequestTicket answers Authentication.RequestTicket(idSource, idTarget) with a
// ticket for the target: the secure server, or another player.
func (s *Server) handleRequestTicket(packet nex.PRUDPPacketInterface, request *nex.RMCMessage) error {
	endpoint := packet.Sender().(*nex.PRUDPConnection).Endpoint().(*nex.PRUDPEndPoint)
	parameters := nex.NewByteStreamIn(request.Parameters, endpoint.LibraryVersions(),
		endpoint.ByteStreamSettings())
	source, target := types.NewPID(0), types.NewPID(0)
	if err := source.ExtractFrom(parameters); err != nil {
		return fmt.Errorf("RequestTicket parameters: %w", err)
	}
	if err := target.ExtractFrom(parameters); err != nil {
		return fmt.Errorf("RequestTicket parameters: %w", err)
	}
	ticket, err := s.ticket(endpoint, source, target)
	if err != nil {
		return err
	}
	out := nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings())
	types.NewQResultSuccess(nex.ResultCodes.Core.Unknown).WriteTo(out)
	types.NewBuffer(ticket).WriteTo(out)
	response := nex.NewRMCSuccess(endpoint, out.Bytes())
	response.ProtocolID = protocolAuthentication
	response.MethodID = methodRequestTicket
	response.CallID = request.CallID
	respond(packet, response)
	return nil
}

// ticket is the Kerberos ticket the source gets for the target: under the source's key,
// the session key, the target PID and the internal ticket (under the target's key: issue
// time, the source PID, the session key). Every player's key comes from UserPassword.
func (s *Server) ticket(endpoint *nex.PRUDPEndPoint, source, target types.PID) ([]byte, error) {
	sessionKey := make([]byte, s.settings.SessionKeyLength)
	if _, err := rand.Read(sessionKey); err != nil {
		return nil, err
	}
	targetPassword := UserPassword
	if target == SecureServerPID {
		targetPassword = s.settings.Accounts.SecureServer().Password
	}

	internal := nex.NewKerberosTicketInternalData(endpoint.Server)
	internal.Issued = types.NewDateTime(0).Now()
	internal.SourcePID = source
	internal.SessionKey = sessionKey
	internalData, err := internal.Encrypt(nex.DeriveKerberosKey(target, []byte(targetPassword)),
		nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings()))
	if err != nil {
		return nil, err
	}

	ticket := nex.NewKerberosTicket()
	ticket.SessionKey = sessionKey
	ticket.TargetPID = target
	ticket.InternalData = types.NewBuffer(internalData)
	return ticket.Encrypt(nex.DeriveKerberosKey(source, []byte(UserPassword)),
		nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings()))
}

// respond sends an RMC response on the request's connection, reliably.
func respond(request nex.PRUDPPacketInterface, message *nex.RMCMessage) {
	connection := request.Sender().(*nex.PRUDPConnection)
	endpoint := connection.Endpoint().(*nex.PRUDPEndPoint)
	packet, _ := nex.NewPRUDPPacketV0(endpoint.Server, connection, nil)
	packet.SetType(constants.DataPacket)
	packet.AddFlag(constants.PacketFlagReliable)
	packet.AddFlag(constants.PacketFlagNeedsAck)
	packet.SetSourceVirtualPortStreamType(request.DestinationVirtualPortStreamType())
	packet.SetSourceVirtualPortStreamID(request.DestinationVirtualPortStreamID())
	packet.SetDestinationVirtualPortStreamType(request.SourceVirtualPortStreamType())
	packet.SetDestinationVirtualPortStreamID(request.SourceVirtualPortStreamID())
	packet.SetSubstreamID(request.SubstreamID())
	packet.SetPayload(message.Bytes())
	endpoint.Send(packet)
}
