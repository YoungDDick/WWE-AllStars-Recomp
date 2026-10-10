package rendezvous

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"net"
	"os"
	"strings"
	"testing"
	"time"
)

// PRUDP v0 (Quazal) packet types and flags, as the game uses them.
const (
	typeSyn     = 0
	typeConnect = 1
	typeData    = 2
	typePing    = 4

	flagAck     = 0x01
	flagHasSize = 0x08
)

// fixtures reads the captured packets in testdata/login-capture.txt by name.
func fixtures(t *testing.T) map[string][]byte {
	t.Helper()
	f, err := os.Open("testdata/login-capture.txt")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	packets := map[string][]byte{}
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || line[0] == '#' {
			continue
		}
		fields := strings.Fields(line)
		data, err := hex.DecodeString(strings.Join(fields[1:], ""))
		if err != nil {
			t.Fatal(err)
		}
		packets[fields[0]] = data
	}
	return packets
}

// checksum is the Quazal PRUDP v0 checksum: the key's byte sum, the bytes after the last
// whole word, and the bytes of the little-endian word sum.
func checksum(keySum byte, data []byte) byte {
	var words uint32
	for i := 0; i+4 <= len(data); i += 4 {
		words += binary.LittleEndian.Uint32(data[i:])
	}
	sum := keySum
	for _, b := range data[len(data)&^3:] {
		sum += b
	}
	var w [4]byte
	binary.LittleEndian.PutUint32(w[:], words)
	for _, b := range w {
		sum += b
	}
	return sum
}

// stand-in access key with the byte sum the captured packets were checksummed with, so the
// tests never need the title's real key.
func standInKey(t *testing.T, syn []byte) (string, byte) {
	t.Helper()
	body := syn[:len(syn)-1]
	keySum := syn[len(syn)-1] - checksum(0, body)
	if checksum(keySum, body) != syn[len(syn)-1] {
		t.Fatal("could not derive the key sum")
	}
	return string([]byte{keySum}), keySum
}

type v0Packet struct {
	src, dst, kind, flags, session byte
	signature                      []byte
	seq                            uint16
	connSignature                  []byte
	fragment                       byte
	payload                        []byte
}

func decode(t *testing.T, keySum byte, data []byte) v0Packet {
	t.Helper()
	if len(data) < 11 {
		t.Fatalf("reply too short: % x", data)
	}
	if got, want := data[len(data)-1], checksum(keySum, data[:len(data)-1]); got != want {
		t.Fatalf("reply checksum %#02x, want %#02x: % x", got, want, data)
	}
	p := v0Packet{src: data[0], dst: data[1], kind: data[2] & 7, flags: data[2] >> 3,
		session: data[3], signature: data[4:8], seq: binary.LittleEndian.Uint16(data[8:10])}
	rest := data[10 : len(data)-1]
	if p.kind == typeSyn || p.kind == typeConnect {
		p.connSignature, rest = rest[:4], rest[4:]
	}
	if p.kind == typeData {
		p.fragment, rest = rest[0], rest[1:]
	}
	if p.flags&flagHasSize != 0 {
		rest = rest[2:]
	}
	p.payload = rest
	return p
}

type client struct {
	t      *testing.T
	conn   *net.UDPConn
	keySum byte
	// serverSignature is the SYN-ACK's connection signature, which signs client packets.
	serverSignature []byte
	seq             uint16
	lastResponse    uint16
}

func startAuth(t *testing.T, key string, onCall func(Call)) *net.UDPAddr {
	t.Helper()
	return startAuthWith(t, Settings{AccessKey: key, OnCall: onCall})
}

func startAuthWith(t *testing.T, settings Settings) *net.UDPAddr {
	t.Helper()
	return start(t, NewAuthServer(settings))
}

func startSecureWith(t *testing.T, settings Settings) *net.UDPAddr {
	t.Helper()
	return start(t, NewSecureServer(settings))
}

func start(t *testing.T, server *Server) *net.UDPAddr {
	t.Helper()
	probe, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	port := probe.LocalAddr().(*net.UDPAddr).Port
	probe.Close()
	go server.Listen(port)
	time.Sleep(100 * time.Millisecond)
	return &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: port}
}

func dial(t *testing.T, server *net.UDPAddr, keySum byte) *client {
	t.Helper()
	conn, err := net.DialUDP("udp", nil, server)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	return &client{t: t, conn: conn, keySum: keySum}
}

// exchange sends a captured packet and returns the first reply.
func (c *client) exchange(packet []byte) v0Packet {
	c.t.Helper()
	if _, err := c.conn.Write(packet); err != nil {
		c.t.Fatal(err)
	}
	reply := c.read()
	if reply.kind == typeSyn {
		c.serverSignature = reply.connSignature
	}
	c.seq = binary.LittleEndian.Uint16(packet[8:10])
	return reply
}

func (c *client) read() v0Packet {
	c.t.Helper()
	c.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	buffer := make([]byte, 1500)
	n, err := c.conn.Read(buffer)
	if err != nil {
		c.t.Fatalf("no reply: %v", err)
	}
	return decode(c.t, c.keySum, buffer[:n])
}

func TestSynGetsASynAck(t *testing.T) {
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	c := dial(t, startAuth(t, key, nil), keySum)

	reply := c.exchange(packets["syn"])
	if reply.kind != typeSyn || reply.flags&flagAck == 0 {
		t.Fatalf("reply type %d flags %#x, want SYN|ACK", reply.kind, reply.flags)
	}
	if reply.src != 0x31 || reply.dst != 0x3F {
		t.Fatalf("reply ports %#02x -> %#02x, want 0x31 -> 0x3f", reply.src, reply.dst)
	}
	if reply.seq != 0 || reply.session != 0 {
		t.Fatalf("reply seq %d session %#02x, want 0 and 0", reply.seq, reply.session)
	}
	if len(reply.connSignature) != 4 || bytes.Equal(reply.connSignature, make([]byte, 4)) {
		t.Fatalf("reply connection signature % x, want 4 non-zero bytes", reply.connSignature)
	}
}

func TestConnectGetsAConnectAckSignedWithTheClientSignature(t *testing.T) {
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	c := dial(t, startAuth(t, key, nil), keySum)
	c.exchange(packets["syn"])

	reply := c.exchange(packets["connect"])
	if reply.kind != typeConnect || reply.flags&flagAck == 0 {
		t.Fatalf("reply type %d flags %#x, want CONNECT|ACK", reply.kind, reply.flags)
	}
	clientSignature := packets["connect"][10:14]
	if !bytes.Equal(reply.signature, clientSignature) {
		t.Fatalf("reply signature % x, want the client's % x", reply.signature, clientSignature)
	}
	if reply.seq != 1 {
		t.Fatalf("reply seq %d, want 1", reply.seq)
	}
}

func TestLoginIsAcknowledgedAndDecoded(t *testing.T) {
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	calls := make(chan Call, 4)
	c := dial(t, startAuth(t, key, func(call Call) { calls <- call }), keySum)
	c.exchange(packets["syn"])
	c.exchange(packets["connect"])

	ack := c.exchange(packets["login"])
	if ack.kind != typeData || ack.flags&flagAck == 0 || ack.seq != 2 {
		t.Fatalf("reply type %d flags %#x seq %d, want DATA|ACK for seq 2", ack.kind, ack.flags,
			ack.seq)
	}
	select {
	case call := <-calls:
		if call.Protocol != 0x0A || call.Method != 1 || call.CallID != 6 {
			t.Fatalf("call %#02x/%d id %d, want Authentication.Login (0x0a/1) id 6",
				call.Protocol, call.Method, call.CallID)
		}
		if want := []byte{5, 0, 'U', 's', 'e', 'r', 0}; !bytes.Equal(call.Parameters, want) {
			t.Fatalf("parameters % x, want % x", call.Parameters, want)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("the Login call was not decoded")
	}
}

func TestPingIsAcknowledged(t *testing.T) {
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	c := dial(t, startAuth(t, key, nil), keySum)
	c.exchange(packets["syn"])
	c.exchange(packets["connect"])

	reply := c.exchange(packets["ping"])
	if reply.kind != typePing || reply.flags&flagAck == 0 {
		t.Fatalf("reply type %d flags %#x, want PING|ACK", reply.kind, reply.flags)
	}
}
