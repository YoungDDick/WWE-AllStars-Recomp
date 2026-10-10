package rendezvous

import (
	"bytes"
	"crypto/hmac"
	"crypto/md5"
	"crypto/rc4"
	"encoding/binary"
	"strings"
	"testing"
)

// What the game does with a Login reply, written from the protocol, not from nex-go: the
// DATA payload is RC4 ("CD&ML", a fresh cipher per packet) over a compression-ratio byte
// and the RMC response; the ticket is RC4 under the user's key with an HMAC-MD5 after it.

const clientPassword = "LSP PASSWORD" // JobLSPLogin's password (0x837F1FA4)

func quazalRC4(data []byte) []byte {
	cipher, _ := rc4.NewCipher([]byte("CD&ML"))
	out := make([]byte, len(data))
	cipher.XORKeyStream(out, data)
	return out
}

// deriveKey is the game's key derivation (object at 0x837F1F88: 65000 and 1024).
func deriveKey(pid uint32, password string) []byte {
	key := []byte(password)
	for i := 0; i < int(65000+pid%1024); i++ {
		sum := md5.Sum(key)
		key = sum[:]
	}
	return key
}

type reader struct {
	t    *testing.T
	data []byte
}

func (r *reader) take(n int) []byte {
	r.t.Helper()
	if len(r.data) < n {
		r.t.Fatalf("response too short: need %d more bytes, have %d", n, len(r.data))
	}
	out := r.data[:n]
	r.data = r.data[n:]
	return out
}
func (r *reader) u8() byte    { return r.take(1)[0] }
func (r *reader) u16() uint16 { return binary.LittleEndian.Uint16(r.take(2)) }
func (r *reader) u32() uint32 { return binary.LittleEndian.Uint32(r.take(4)) }
func (r *reader) buffer() []byte {
	return r.take(int(r.u32()))
}
func (r *reader) str() string {
	n := int(r.u16())
	if n == 0 {
		return ""
	}
	return string(r.take(n)[:n-1])
}

// call sends an RMC request as the game does: a reliable DATA packet on the next sequence
// number, RC4 over a zero ratio byte and the request.
func (c *client) call(protocol byte, callID, method uint32, parameters []byte) {
	c.t.Helper()
	rmc := binary.LittleEndian.AppendUint32(nil, uint32(9+len(parameters)))
	rmc = append(rmc, protocol|0x80)
	rmc = binary.LittleEndian.AppendUint32(rmc, callID)
	rmc = binary.LittleEndian.AppendUint32(rmc, method)
	rmc = append(rmc, parameters...)
	c.seq++
	packet := []byte{0x3F, 0x31, (0x06 << 3) | typeData, 0xA1}
	packet = append(packet, c.serverSignature...)
	packet = binary.LittleEndian.AppendUint16(packet, c.seq)
	packet = append(packet, 0) // fragment
	packet = append(packet, quazalRC4(append([]byte{0}, rmc...))...)
	packet = append(packet, checksum(c.keySum, packet))
	if _, err := c.conn.Write(packet); err != nil {
		c.t.Fatal(err)
	}
}

// ack acknowledges a reliable DATA packet from the server.
func (c *client) ack(seq uint16) {
	packet := []byte{0x3F, 0x31, (flagAck << 3) | typeData, 0xA1}
	packet = append(packet, c.serverSignature...)
	packet = binary.LittleEndian.AppendUint16(packet, seq)
	packet = append(packet, 0)
	packet = append(packet, checksum(c.keySum, packet))
	c.conn.Write(packet)
}

// response reads packets until a DATA request (not an ACK) arrives and returns its RMC body.
func (c *client) response() []byte {
	c.t.Helper()
	for {
		p := c.read()
		if p.kind != typeData || p.flags&flagAck != 0 {
			continue
		}
		c.ack(p.seq)
		if p.seq <= c.lastResponse {
			continue // a retransmit
		}
		c.lastResponse = p.seq
		plain := quazalRC4(p.payload)
		if plain[0] != 0 {
			c.t.Fatalf("compressed response (ratio %d)", plain[0])
		}
		return plain[1:]
	}
}

// header checks an Authentication success response's header.
func header(t *testing.T, r *reader, callID, method uint32) {
	t.Helper()
	headerOf(t, r, 0x0A, callID, method)
}

// headerOf checks an RMC success response's header.
func headerOf(t *testing.T, r *reader, protocol byte, callID, method uint32) {
	t.Helper()
	if size := r.u32(); int(size) != len(r.data) {
		t.Fatalf("RMC size %d, body %d", size, len(r.data))
	}
	if p := r.u8(); p != protocol {
		t.Fatalf("response protocol %#02x, want %#02x", p, protocol)
	}
	if success := r.u8(); success != 1 {
		t.Fatalf("response is an error (%#x)", r.u32())
	}
	if id := r.u32(); id != callID {
		t.Fatalf("call id %d, want %d", id, callID)
	}
	if m := r.u32(); m != method|0x8000 {
		t.Fatalf("method %#x, want %#x", m, method|0x8000)
	}
}

// openTicket checks a ticket under the user's key and returns its session key and target.
func openTicket(t *testing.T, ticket []byte, pid uint32) ([]byte, uint32) {
	t.Helper()
	key := deriveKey(pid, clientPassword)
	encrypted, sum := ticket[:len(ticket)-16], ticket[len(ticket)-16:]
	mac := hmac.New(md5.New, key)
	mac.Write(encrypted)
	if !hmac.Equal(mac.Sum(nil), sum) {
		t.Fatal("ticket HMAC does not match the key the game derives")
	}
	cipher, _ := rc4.NewCipher(key)
	plain := make([]byte, len(encrypted))
	cipher.XORKeyStream(plain, encrypted)
	inner := &reader{t: t, data: plain}
	sessionKey := inner.take(16)
	target := inner.u32()
	if internal := inner.buffer(); len(internal) <= 16 {
		t.Fatalf("internal ticket %d bytes, want encrypted data and an HMAC", len(internal))
	}
	if len(inner.data) != 0 {
		t.Fatalf("%d bytes left in the ticket", len(inner.data))
	}
	return sessionKey, target
}

func login(t *testing.T, settings Settings) (*client, *reader) {
	t.Helper()
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	settings.AccessKey = key
	c := dial(t, startAuthWith(t, settings), keySum)
	c.exchange(packets["syn"])
	c.exchange(packets["connect"])
	c.exchange(packets["login"])

	r := &reader{t: t, data: c.response()}
	header(t, r, 6, methodLogin)
	return c, r
}

func TestLoginRepliesWithATicketTheClientCanDecrypt(t *testing.T) {
	_, r := login(t, Settings{SecurePort: 32481})
	if result := r.u32(); result != 0x00010001 {
		t.Fatalf("retval %#x, want success 0x00010001", result)
	}
	pid := r.u32()
	if pid < 0x1000 {
		t.Fatalf("user PID %#x, want one of the server's user PIDs", pid)
	}
	ticket := r.buffer()

	sessionKey, target := openTicket(t, ticket, pid)
	if bytes.Equal(sessionKey, make([]byte, 16)) {
		t.Fatal("session key is zero")
	}
	if target != SecureServerPID {
		t.Fatalf("ticket target PID %d, want the secure server's %d", target, SecureServerPID)
	}

	url := r.str()
	for _, part := range []string{"prudps:/", "port=32481", "PID=2", "sid=1", "stream=3"} {
		if !strings.Contains(url, part) {
			t.Fatalf("secure URL %q lacks %q", url, part)
		}
	}
	if special := r.u32(); special != 0 {
		t.Fatalf("%d special protocols, want none", special)
	}
	r.str() // special protocols URL
	r.str() // return message
	if len(r.data) != 0 {
		t.Fatalf("%d bytes left in the response", len(r.data))
	}
}

func TestEveryLoginGetsItsOwnPIDEvenWithTheSameName(t *testing.T) {
	// Two copies of the game can both be "User": they must still be two principals.
	accounts := NewAccounts()
	a, b := accounts.NewPID("User"), accounts.NewPID("User")
	if a == b {
		t.Fatal("two logins share a PID")
	}
	if name, _ := accounts.Name(a); name != "User" {
		t.Fatalf("PID %d is %q, want User", a, name)
	}
}
