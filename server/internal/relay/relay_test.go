package relay

import (
	"bytes"
	"net/netip"
	"testing"
	"time"
)

const (
	alpha   uint64 = 0x0009000000000A01
	bravo   uint64 = 0x0009000000000B02
	charlie uint64 = 0x0009000000000C03
)

var (
	alphaAt   = netip.MustParseAddrPort("203.0.113.7:50001")
	bravoAt   = netip.MustParseAddrPort("203.0.113.7:50002")
	charlieAt = netip.MustParseAddrPort("198.51.100.4:40000")
)

// directory: alpha and bravo share a session behind one public address; charlie is
// registered but elsewhere.
type directory struct{ lookups *int }

func tokenOf(xuid uint64) [TokenSize]byte {
	var t [TokenSize]byte
	for i := range t {
		t[i] = byte(xuid) + byte(i)
	}
	return t
}

func (directory) Token(xuid uint64) ([TokenSize]byte, netip.Addr, bool) {
	switch xuid {
	case alpha, bravo:
		return tokenOf(xuid), alphaAt.Addr(), true
	case charlie:
		return tokenOf(xuid), charlieAt.Addr(), true
	}
	return [TokenSize]byte{}, netip.Addr{}, false
}

func (d directory) MayRelay(a, b uint64) bool {
	if d.lookups != nil {
		*d.lookups++
	}
	return (a == alpha && b == bravo) || (a == bravo && b == alpha)
}

type clock struct{ t time.Time }

func (c *clock) now() time.Time { return c.t }

func newRelay() (*Relay, *clock) {
	c := &clock{t: time.Unix(1_800_000_000, 0)}
	return New(directory{}, Limits{PacketsPerSecond: 100, Burst: 10}, c.now), c
}

// A hello is a UDP packet, so its source can be forged: it counts only from the address
// the player registered from over HTTPS.
func TestAHelloFromAnotherAddressIsIgnored(t *testing.T) {
	r, _ := newRelay()
	victim := netip.MustParseAddrPort("192.0.2.66:53")
	if out := r.Handle(victim, Hello(alpha, tokenOf(alpha))); len(out) != 0 {
		t.Fatalf("a hello from outside the player's registered address was welcomed: %v", out)
	}
	hello(t, r, bravo, bravoAt)
	if out := r.Handle(bravoAt, Send(alpha, 1000, 1000, []byte("x"))); len(out) != 0 {
		t.Fatalf("data went to a forged address: %v", out)
	}
}

func TestAFloodOverTheRateCostsNoSessionLookups(t *testing.T) {
	lookups := 0
	c := &clock{t: time.Unix(1_800_000_000, 0)}
	r := New(directory{lookups: &lookups}, Limits{PacketsPerSecond: 100, Burst: 10}, c.now)
	hello(t, r, alpha, alphaAt)
	hello(t, r, bravo, bravoAt)
	for i := 0; i < 1000; i++ {
		r.Handle(alphaAt, Send(bravo, 1000, 1000, []byte("x")))
	}
	if lookups > 10 {
		t.Fatalf("%d session lookups for 1000 packets over a burst of 10", lookups)
	}
}

func hello(t *testing.T, r *Relay, xuid uint64, at netip.AddrPort) {
	t.Helper()
	out := r.Handle(at, Hello(xuid, tokenOf(xuid)))
	if len(out) != 1 || out[0].To != at || !bytes.Equal(out[0].Data, Welcome(xuid)) {
		t.Fatalf("hello from %v: %v, want a welcome", at, out)
	}
}

func TestDataIsForwardedBetweenMembersOfOneSession(t *testing.T) {
	r, _ := newRelay()
	hello(t, r, alpha, alphaAt)
	hello(t, r, bravo, bravoAt)
	out := r.Handle(alphaAt, Send(bravo, 1000, 1000, []byte("vdp")))
	if len(out) != 1 || out[0].To != bravoAt {
		t.Fatalf("forwarded %v, want one datagram to bravo", out)
	}
	from, src, dst, payload, ok := ParseDeliver(out[0].Data)
	if !ok || from != alpha || src != 1000 || dst != 1000 || string(payload) != "vdp" {
		t.Fatalf("delivered from %x %d->%d %q (ok %v)", from, src, dst, payload, ok)
	}
}

func TestStrangersAreNotForwarded(t *testing.T) {
	r, _ := newRelay()
	hello(t, r, alpha, alphaAt)
	hello(t, r, charlie, charlieAt)
	if out := r.Handle(charlieAt, Send(alpha, 1000, 1000, []byte("x"))); len(out) != 0 {
		t.Fatalf("a player outside the session reached alpha: %v", out)
	}
	if out := r.Handle(alphaAt, Send(charlie, 1000, 1000, []byte("x"))); len(out) != 0 {
		t.Fatalf("alpha reached a player outside its session: %v", out)
	}
}

func TestAWrongTokenRegistersNothing(t *testing.T) {
	r, _ := newRelay()
	hello(t, r, bravo, bravoAt)
	if out := r.Handle(charlieAt, Hello(alpha, tokenOf(charlie))); len(out) != 0 {
		t.Fatalf("a hello with someone else's token was welcomed: %v", out)
	}
	if out := r.Handle(charlieAt, Send(bravo, 1000, 1000, []byte("x"))); len(out) != 0 {
		t.Fatalf("an unregistered address sent through the relay: %v", out)
	}
}

func TestUnknownPlayersAndAddressesAreDropped(t *testing.T) {
	r, _ := newRelay()
	if out := r.Handle(alphaAt, Hello(0x0009000000000F0F, tokenOf(0x0009000000000F0F))); len(out) != 0 {
		t.Fatalf("an unregistered XUID was welcomed: %v", out)
	}
	hello(t, r, alpha, alphaAt)
	if out := r.Handle(alphaAt, Send(bravo, 1000, 1000, []byte("x"))); len(out) != 0 {
		t.Fatalf("data went to a member who never said hello: %v", out)
	}
	if out := r.Handle(alphaAt, []byte("garbage")); len(out) != 0 {
		t.Fatalf("garbage was answered: %v", out)
	}
}

func TestAPlayerThatMovesIsReachedAtItsNewAddress(t *testing.T) {
	r, _ := newRelay()
	hello(t, r, alpha, alphaAt)
	hello(t, r, bravo, bravoAt)
	moved := netip.MustParseAddrPort("203.0.113.7:50999")
	hello(t, r, bravo, moved)
	out := r.Handle(alphaAt, Send(bravo, 9100, 9100, []byte("x")))
	if len(out) != 1 || out[0].To != moved {
		t.Fatalf("forwarded %v, want bravo's new address", out)
	}
	if out := r.Handle(bravoAt, Send(alpha, 9100, 9100, []byte("x"))); len(out) != 0 {
		t.Fatalf("bravo's old address still sends: %v", out)
	}
}

func TestASenderOverItsRateIsDropped(t *testing.T) {
	r, c := newRelay()
	hello(t, r, alpha, alphaAt)
	hello(t, r, bravo, bravoAt)
	forwarded := 0
	for i := 0; i < 50; i++ {
		forwarded += len(r.Handle(alphaAt, Send(bravo, 1000, 1000, []byte("x"))))
	}
	if forwarded != 10 {
		t.Fatalf("%d of 50 instant packets forwarded, want the burst of 10", forwarded)
	}
	c.t = c.t.Add(100 * time.Millisecond) // refills 10 at 100/s
	if out := r.Handle(alphaAt, Send(bravo, 1000, 1000, []byte("x"))); len(out) != 1 {
		t.Fatal("nothing forwarded after the bucket refilled")
	}
}

func TestAPingIsAnsweredWithItsTimestamp(t *testing.T) {
	r, _ := newRelay()
	hello(t, r, alpha, alphaAt)
	out := r.Handle(alphaAt, Ping(123456789))
	if len(out) != 1 || out[0].To != alphaAt || !bytes.Equal(out[0].Data, Pong(123456789)) {
		t.Fatalf("ping answered with %v", out)
	}
}
