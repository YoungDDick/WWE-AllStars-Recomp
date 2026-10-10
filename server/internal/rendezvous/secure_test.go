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

// kerberosEncrypt is the game's Kerberos layer: RC4 under the key, then HMAC-MD5 of the
// ciphertext.
func kerberosEncrypt(key, data []byte) []byte {
	cipher, _ := rc4.NewCipher(key)
	out := make([]byte, len(data))
	cipher.XORKeyStream(out, data)
	mac := hmac.New(md5.New, key)
	mac.Write(out)
	return mac.Sum(out)
}

func putBuffer(out, data []byte) []byte {
	return append(binary.LittleEndian.AppendUint32(out, uint32(len(data))), data...)
}

func putString(out []byte, s string) []byte {
	out = binary.LittleEndian.AppendUint16(out, uint16(len(s)+1))
	return append(append(out, s...), 0)
}

// secureSession logs in on the auth server, then connects to the secure server with the
// ticket the way the game does (analysis/session-online04cap/listener.log): SYN, then a
// CONNECT whose RC4 payload is the ratio byte, the internal ticket and the request
// (PID, CID, check value under the session key).
func secureSession(t *testing.T) (*client, uint32) {
	t.Helper()
	packets := fixtures(t)
	key, keySum := standInKey(t, packets["syn"])
	accounts := NewAccounts()
	secureAddress := startSecureWith(t, Settings{AccessKey: key, Accounts: accounts})
	settings := Settings{AccessKey: key, Accounts: accounts, SecurePort: secureAddress.Port}
	auth := dial(t, startAuthWith(t, settings), keySum)
	auth.exchange(packets["syn"])
	auth.exchange(packets["connect"])
	auth.exchange(packets["login"])
	r := &reader{t: t, data: auth.response()}
	header(t, r, 6, methodLogin)
	r.u32()
	pid := r.u32()
	ticket := r.buffer()

	userKey := deriveKey(pid, clientPassword)
	encrypted := ticket[:len(ticket)-16]
	cipher, _ := rc4.NewCipher(userKey)
	plain := make([]byte, len(encrypted))
	cipher.XORKeyStream(plain, encrypted)
	inner := &reader{t: t, data: plain}
	sessionKey := inner.take(16)
	inner.u32()
	internal := inner.buffer()

	c := dial(t, secureAddress, keySum)
	c.exchange(packets["syn"])
	request := binary.LittleEndian.AppendUint32(nil, pid)
	request = binary.LittleEndian.AppendUint32(request, 1)          // CID of the secure URL
	request = binary.LittleEndian.AppendUint32(request, 0x12345678) // check value
	payload := putBuffer(putBuffer([]byte{0}, internal), kerberosEncrypt(sessionKey, request))
	connect := []byte{0x3F, 0x31, (0x06 << 3) | typeConnect, 0xA1}
	connect = append(connect, c.serverSignature...)
	connect = binary.LittleEndian.AppendUint16(connect, 1)
	connect = append(connect, 0xF4, 0xFA, 0xA5, 0xBB) // the client's connection signature
	connect = append(connect, quazalRC4(payload)...)
	connect = append(connect, checksum(keySum, connect))
	ack := c.exchange(connect)
	if ack.kind != typeConnect || ack.flags&flagAck == 0 {
		t.Fatalf("reply type %d flags %#x, want CONNECT|ACK", ack.kind, ack.flags)
	}
	response := &reader{t: t, data: quazalRC4(ack.payload)}
	if ratio := response.u8(); ratio != 0 {
		t.Fatalf("CONNECT-ACK ratio byte %d, want 0", ratio)
	}
	if check := response.buffer(); !bytes.Equal(check, binary.LittleEndian.AppendUint32(nil, 0x12345679)) {
		t.Fatalf("CONNECT-ACK check % x, want the check value + 1", check)
	}
	return c, pid
}

func TestSecureConnectAcceptsTheLoginTicket(t *testing.T) {
	secureSession(t)
}

func TestRegisterGivesAConnectionIDAndThePublicURL(t *testing.T) {
	c, _ := secureSession(t)
	// vecMyURLs: one local URL, as Net-Z builds it.
	parameters := binary.LittleEndian.AppendUint32(nil, 1)
	parameters = putString(parameters, "prudp:/address=192.168.1.20;port=1001;sid=15;type=2")
	c.call(0x0B, 20, 1, parameters)
	r := &reader{t: t, data: c.response()}
	headerOf(t, r, 0x0B, 20, 1)
	if result := r.u32(); result != 0x00010001 {
		t.Fatalf("retval %#x, want success", result)
	}
	if cid := r.u32(); cid == 0 {
		t.Fatal("connection ID is 0")
	}
	public := r.str()
	local := c.conn.LocalAddr().String()
	host, port, _ := strings.Cut(local, ":")
	for _, part := range []string{"prudp:/", "address=" + host, "port=" + port} {
		if !strings.Contains(public, part) {
			t.Fatalf("public URL %q lacks %q", public, part)
		}
	}
	if len(r.data) != 0 {
		t.Fatalf("%d bytes left in the response", len(r.data))
	}
}

func TestRegisterExAnswersLikeRegister(t *testing.T) {
	c, _ := secureSession(t)
	parameters := binary.LittleEndian.AppendUint32(nil, 1)
	parameters = putString(parameters, "prudp:/address=192.168.1.20;port=1001;sid=15;type=2")
	parameters = putString(parameters, "XboxUserInfo") // hCustomData: a Data holder
	parameters = putBuffer(parameters, binary.LittleEndian.AppendUint32(nil, 0))
	c.call(0x0B, 21, 4, parameters)
	r := &reader{t: t, data: c.response()}
	headerOf(t, r, 0x0B, 21, 4)
	if result := r.u32(); result != 0x00010001 {
		t.Fatalf("retval %#x, want success", result)
	}
	if cid := r.u32(); cid == 0 {
		t.Fatal("connection ID is 0")
	}
	r.str()
}

func TestRequestConnectionDataAnswers(t *testing.T) {
	c, pid := secureSession(t)
	parameters := binary.LittleEndian.AppendUint32(nil, 1)
	parameters = binary.LittleEndian.AppendUint32(parameters, pid)
	c.call(0x0B, 22, 2, parameters)
	r := &reader{t: t, data: c.response()}
	headerOf(t, r, 0x0B, 22, 2)
	if ok := r.u8(); ok != 1 {
		t.Fatalf("retval %d, want true", ok)
	}
	r.u32() // the list of ConnectionData
}

func TestGameDataLogCallsGetAnEmptySuccess(t *testing.T) {
	c, _ := secureSession(t)
	for method := uint32(1); method <= 4; method++ {
		c.call(0x68, 30+method, method, []byte{1, 2, 3, 4})
		r := &reader{t: t, data: c.response()}
		headerOf(t, r, 0x68, 30+method, method)
		if len(r.data) != 0 {
			t.Fatalf("GameDataLog %d: %d bytes in the response, want none", method, len(r.data))
		}
	}
}
