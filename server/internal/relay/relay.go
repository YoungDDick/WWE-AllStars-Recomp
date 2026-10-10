// Package relay forwards peer datagrams between the players of one session (or a host and
// a player who found its session) when they cannot reach each other directly (ADR-0021, ticket 16). A player says hello with the token its
// XWS registration gave; from then on the address it said hello from speaks for it.
//
// Wire format (big-endian), every packet starting with "ASR1" and a type byte:
//
//	hello   1  xuid u64, token [16]       player -> relay; answered with welcome
//	welcome 2  xuid u64                   relay -> player
//	ping    3  stamp u64                  player -> relay; answered with pong
//	pong    4  stamp u64                  relay -> player
//	send    5  to xuid u64, src u16, dst u16, payload    player -> relay
//	deliver 6  from xuid u64, src u16, dst u16, payload  relay -> player
//
// src and dst are the game's own UDP ports (VDP 1000, 9100).
package relay

import (
	"encoding/binary"
	"log"
	"net"
	"net/netip"
	"sync"
	"time"
)

// TokenSize is the length of a relay token.
const TokenSize = 16

const magic = "ASR1"

const (
	typeHello byte = 1 + iota
	typeWelcome
	typePing
	typePong
	typeSend
	typeDeliver
)

const headerSize = len(magic) + 1

// Directory is what the relay needs from the XWS API: registered players' tokens and the
// public address each registered from, and which players may reach each other (one
// session, or a host and a player who found it).
type Directory interface {
	Token(xuid uint64) (token [TokenSize]byte, public netip.Addr, ok bool)
	MayRelay(a, b uint64) bool
}

// Limits is each player's token bucket.
type Limits struct {
	PacketsPerSecond float64
	Burst            float64
}

// DefaultLimits leave room for a 4-player match (a few dozen packets per second each).
var DefaultLimits = Limits{PacketsPerSecond: 400, Burst: 800}

// Datagram is a packet the relay sends.
type Datagram struct {
	To   netip.AddrPort
	Data []byte
}

type player struct {
	xuid   uint64
	at     netip.AddrPort
	tokens float64
	filled time.Time
}

// Relay is the forwarding state; Handle is the whole protocol, Listen puts it on a port.
type Relay struct {
	directory Directory
	limits    Limits
	now       func() time.Time

	mu     sync.Mutex
	byXUID map[uint64]*player
	byAddr map[netip.AddrPort]*player
}

// New creates a relay; now is the clock (time.Now outside tests).
func New(directory Directory, limits Limits, now func() time.Time) *Relay {
	return &Relay{directory: directory, limits: limits, now: now,
		byXUID: map[uint64]*player{}, byAddr: map[netip.AddrPort]*player{}}
}

func packet(kind byte, size int) []byte {
	b := make([]byte, headerSize, headerSize+size)
	copy(b, magic)
	b[len(magic)] = kind
	return b
}

// Hello is a player's hello.
func Hello(xuid uint64, token [TokenSize]byte) []byte {
	return append(binary.BigEndian.AppendUint64(packet(typeHello, 8+TokenSize), xuid), token[:]...)
}

// Welcome answers a hello.
func Welcome(xuid uint64) []byte {
	return binary.BigEndian.AppendUint64(packet(typeWelcome, 8), xuid)
}

// Ping and Pong carry the sender's timestamp.
func Ping(stamp uint64) []byte { return binary.BigEndian.AppendUint64(packet(typePing, 8), stamp) }
func Pong(stamp uint64) []byte { return binary.BigEndian.AppendUint64(packet(typePong, 8), stamp) }

func data(kind byte, xuid uint64, src, dst uint16, payload []byte) []byte {
	b := binary.BigEndian.AppendUint64(packet(kind, 12+len(payload)), xuid)
	b = binary.BigEndian.AppendUint16(b, src)
	b = binary.BigEndian.AppendUint16(b, dst)
	return append(b, payload...)
}

// Send asks the relay to pass a datagram to a player.
func Send(to uint64, src, dst uint16, payload []byte) []byte {
	return data(typeSend, to, src, dst, payload)
}

// ParseDeliver reads a datagram the relay passed on.
func ParseDeliver(b []byte) (from uint64, src, dst uint16, payload []byte, ok bool) {
	kind, body, ok := parse(b)
	if !ok || kind != typeDeliver || len(body) < 12 {
		return 0, 0, 0, nil, false
	}
	return binary.BigEndian.Uint64(body), binary.BigEndian.Uint16(body[8:]),
		binary.BigEndian.Uint16(body[10:]), body[12:], true
}

func parse(b []byte) (byte, []byte, bool) {
	if len(b) < headerSize || string(b[:len(magic)]) != magic {
		return 0, nil, false
	}
	return b[len(magic)], b[headerSize:], true
}

// allow takes a token from the player's bucket.
func (r *Relay) allow(p *player) bool {
	now := r.now()
	p.tokens = min(r.limits.Burst, p.tokens+now.Sub(p.filled).Seconds()*r.limits.PacketsPerSecond)
	p.filled = now
	if p.tokens < 1 {
		return false
	}
	p.tokens--
	return true
}

// Handle answers one packet from an address: what to send, and where.
func (r *Relay) Handle(from netip.AddrPort, b []byte) []Datagram {
	kind, body, ok := parse(b)
	if !ok {
		return nil
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	switch kind {
	case typeHello:
		if len(body) != 8+TokenSize {
			return nil
		}
		// A hello's source can be forged, so it counts only from the address the player
		// registered from over HTTPS: the relay never sends to a third party.
		xuid := binary.BigEndian.Uint64(body)
		token, public, known := r.directory.Token(xuid)
		if !known || [TokenSize]byte(body[8:]) != token || from.Addr() != public {
			return nil
		}
		p := r.byXUID[xuid]
		if p == nil {
			p = &player{xuid: xuid, tokens: r.limits.Burst, filled: r.now()}
			r.byXUID[xuid] = p
		}
		if p.at != from {
			delete(r.byAddr, p.at)
			p.at = from
			r.byAddr[from] = p
		}
		return []Datagram{{To: from, Data: Welcome(xuid)}}
	case typePing:
		if r.byAddr[from] == nil || len(body) != 8 {
			return nil
		}
		return []Datagram{{To: from, Data: Pong(binary.BigEndian.Uint64(body))}}
	case typeSend:
		sender := r.byAddr[from]
		if sender == nil || len(body) < 12 {
			return nil
		}
		to := r.byXUID[binary.BigEndian.Uint64(body)]
		// The bucket first: a flood costs no session lookups.
		if to == nil || !r.allow(sender) || !r.directory.MayRelay(sender.xuid, to.xuid) {
			return nil
		}
		return []Datagram{{To: to.at, Data: data(typeDeliver, sender.xuid,
			binary.BigEndian.Uint16(body[8:]), binary.BigEndian.Uint16(body[10:]), body[12:])}}
	}
	return nil
}

// Listen serves the relay on a UDP port.
func (r *Relay) Listen(port int) error {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{Port: port})
	if err != nil {
		return err
	}
	log.Printf("relay on UDP %d", port)
	buffer := make([]byte, 2048)
	for {
		n, from, err := conn.ReadFromUDPAddrPort(buffer)
		if err != nil {
			return err
		}
		for _, d := range r.Handle(netip.AddrPortFrom(from.Addr().Unmap(), from.Port()), buffer[:n]) {
			conn.WriteToUDPAddrPort(d.Data, d.To)
		}
	}
}
