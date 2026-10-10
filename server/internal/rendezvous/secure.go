package rendezvous

import (
	"fmt"
	"net"
	"regexp"
	"strconv"

	nex "github.com/PretendoNetwork/nex-go/v2"
	"github.com/PretendoNetwork/nex-go/v2/types"
)

// SecureConnection (0x0B) and GameDataLog (0x68) calls the game makes on the secure server
// (feasibility §6.2).
const (
	protocolSecureConnection    = 0x0B
	methodRegister              = 1
	methodRequestConnectionData = 2
	methodRegisterEx            = 4
	protocolGameDataLog         = 0x68
	firstGameDataLogMethod      = 1 // LogNonUniqueData
	lastGameDataLogMethod       = 4 // LogFirstTimeUseData
)

var urlAddress = regexp.MustCompile(`address=[^;]*`)
var urlPort = regexp.MustCompile(`port=[^;]*`)

// publicURL is the client's own station URL with the address and port the server sees it at.
func publicURL(local string, seen net.Addr) string {
	udp, ok := seen.(*net.UDPAddr)
	if !ok {
		return local
	}
	address := "address=" + udp.IP.String()
	port := "port=" + strconv.Itoa(udp.Port)
	if local == "" {
		return "prudp:/" + address + ";" + port + ";sid=15;type=3"
	}
	if urlAddress.MatchString(local) {
		local = urlAddress.ReplaceAllString(local, address)
	} else {
		local += ";" + address
	}
	if urlPort.MatchString(local) {
		return urlPort.ReplaceAllString(local, port)
	}
	return local + ";" + port
}

// handleRegister answers SecureConnection.Register(vecMyURLs) and RegisterEx(vecMyURLs,
// hCustomData) with success, the connection ID and the public URL.
func (s *Server) handleRegister(packet nex.PRUDPPacketInterface, request *nex.RMCMessage) error {
	connection := packet.Sender().(*nex.PRUDPConnection)
	endpoint := connection.Endpoint().(*nex.PRUDPEndPoint)
	parameters := nex.NewByteStreamIn(request.Parameters, endpoint.LibraryVersions(),
		endpoint.ByteStreamSettings())
	count, err := parameters.ReadUInt32LE()
	if err != nil {
		return fmt.Errorf("Register parameters: %w", err)
	}
	var local types.String
	if count > 0 {
		if err := local.ExtractFrom(parameters); err != nil {
			return fmt.Errorf("Register parameters: %w", err)
		}
	}
	public := publicURL(string(local), connection.Address())
	s.settings.Stations.Register(connection.PID(), connection.ID, public)

	out := nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings())
	types.NewQResultSuccess(nex.ResultCodes.Core.Unknown).WriteTo(out)
	types.NewUInt32(connection.ID).WriteTo(out)
	types.NewString(public).WriteTo(out)
	s.reply(packet, request, out.Bytes())
	return nil
}

// handleRequestConnectionData answers RequestConnectionData(cidTarget, pidTarget) with
// true and the target's registered URL, if it has one.
func (s *Server) handleRequestConnectionData(packet nex.PRUDPPacketInterface, request *nex.RMCMessage) error {
	endpoint := packet.Sender().(*nex.PRUDPConnection).Endpoint().(*nex.PRUDPEndPoint)
	parameters := nex.NewByteStreamIn(request.Parameters, endpoint.LibraryVersions(),
		endpoint.ByteStreamSettings())
	if _, err := parameters.ReadUInt32LE(); err != nil {
		return fmt.Errorf("RequestConnectionData parameters: %w", err)
	}
	target := types.NewPID(0)
	if err := target.ExtractFrom(parameters); err != nil {
		return fmt.Errorf("RequestConnectionData parameters: %w", err)
	}
	out := nex.NewByteStreamOut(endpoint.LibraryVersions(), endpoint.ByteStreamSettings())
	types.NewBool(true).WriteTo(out)
	station, ok := s.settings.Stations.Lookup(target)
	if !ok {
		out.WriteUInt32LE(0)
	} else {
		// One ConnectionData: the station URL and the connection ID.
		out.WriteUInt32LE(1)
		types.NewString(station.URL).WriteTo(out)
		types.NewUInt32(station.ConnectionID).WriteTo(out)
	}
	s.reply(packet, request, out.Bytes())
	return nil
}

// handleGameDataLog answers the four telemetry calls with an empty success: the game's
// queue waits on each one, so none may be dropped.
func (s *Server) handleGameDataLog(packet nex.PRUDPPacketInterface, request *nex.RMCMessage) error {
	s.reply(packet, request, nil)
	return nil
}

// reply sends a success response to a request with the given body.
func (s *Server) reply(packet nex.PRUDPPacketInterface, request *nex.RMCMessage, body []byte) {
	endpoint := packet.Sender().(*nex.PRUDPConnection).Endpoint().(*nex.PRUDPEndPoint)
	response := nex.NewRMCSuccess(endpoint, body)
	response.ProtocolID = request.ProtocolID
	response.MethodID = request.MethodID
	response.CallID = request.CallID
	respond(packet, response)
}
