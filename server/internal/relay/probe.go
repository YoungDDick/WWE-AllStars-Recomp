package relay

import (
	"crypto/rand"
	"encoding/binary"
	"net"
	"net/netip"
	"time"
)

// The SDK's direct probe (relay_protocol.h): "ASP1", a type byte, then an 8-byte nonce.
const (
	probeMagic  = "ASP1"
	probeAsk    = 1
	probeAnswer = 2
	probeSize   = len(probeMagic) + 1 + 8
)

// ProbeDirect asks a player's probe port whether it answers from outside its network (the
// SDK's direct probe: "ASP1", type 1 and a nonce; the answer is type 2 with the nonce). A
// few tries until the timeout.
func ProbeDirect(to netip.AddrPort, timeout time.Duration) bool {
	conn, err := net.ListenUDP("udp4", nil)
	if err != nil {
		return false
	}
	defer conn.Close()
	var nonce [8]byte
	rand.Read(nonce[:])
	probe := append(append([]byte(probeMagic), probeAsk), nonce[:]...)
	deadline := time.Now().Add(timeout)
	buffer := make([]byte, 64)
	for time.Now().Before(deadline) {
		conn.WriteToUDPAddrPort(probe, to)
		next := time.Now().Add(200 * time.Millisecond)
		if next.After(deadline) {
			next = deadline
		}
		conn.SetReadDeadline(next)
		for {
			n, _, err := conn.ReadFromUDPAddrPort(buffer)
			if err != nil {
				break
			}
			if n == probeSize && string(buffer[:len(probeMagic)]) == probeMagic &&
				buffer[len(probeMagic)] == probeAnswer &&
				binary.BigEndian.Uint64(buffer[len(probeMagic)+1:]) == binary.BigEndian.Uint64(nonce[:]) {
				return true
			}
		}
	}
	return false
}
