package relay

import (
	"net"
	"net/netip"
	"testing"
	"time"
)

// answerProbes is a player's probe responder (the SDK's relay::ListenForProbes).
func answerProbes(t *testing.T) netip.AddrPort {
	t.Helper()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	go func() {
		buffer := make([]byte, 64)
		for {
			n, from, err := conn.ReadFromUDPAddrPort(buffer)
			if err != nil {
				return
			}
			if n == 13 && string(buffer[:4]) == "ASP1" && buffer[4] == 1 {
				answer := append([]byte("ASP1\x02"), buffer[5:13]...)
				conn.WriteToUDPAddrPort(answer, from)
			}
		}
	}()
	return conn.LocalAddr().(*net.UDPAddr).AddrPort()
}

func TestAProbeIsAnsweredByAListeningPlayer(t *testing.T) {
	if !ProbeDirect(answerProbes(t), time.Second) {
		t.Fatal("the probe went unanswered")
	}
}

func TestAProbeToNobodyTimesOut(t *testing.T) {
	conn, _ := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	silent := conn.LocalAddr().(*net.UDPAddr).AddrPort()
	defer conn.Close()
	start := time.Now()
	if ProbeDirect(silent, 300*time.Millisecond) {
		t.Fatal("a silent port answered")
	}
	if waited := time.Since(start); waited > time.Second {
		t.Fatalf("the probe waited %v", waited)
	}
}
